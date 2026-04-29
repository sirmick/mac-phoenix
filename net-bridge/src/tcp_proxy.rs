//! TCP NAT proxy: intercepts TCP frames from the Mac, manages host connections,
//! and crafts response frames using smoltcp's wire module.

use std::collections::HashMap;
use std::io::{self, Read, Write};
use std::net::{Ipv4Addr, SocketAddrV4, TcpStream};
use std::time::Instant;

use smoltcp::wire::{
    EthernetAddress, EthernetFrame, EthernetProtocol, EthernetRepr,
    IpProtocol, Ipv4Address, Ipv4Packet, Ipv4Repr,
    TcpControl, TcpPacket, TcpRepr, TcpSeqNumber,
};

use crate::device::SocketDevice;

/// MAC addresses
const MAC_ADDR: EthernetAddress = EthernetAddress([0x02, 0x50, 0x48, 0x58, 0x00, 0x01]);
const GW_MAC: EthernetAddress = EthernetAddress([0x02, 0x50, 0x48, 0x58, 0x00, 0x02]);

/// Maximum TCP payload per frame sent to the Mac. Stay below MSS 1460 so
/// the resulting Ethernet frame (14 eth + 20 ip + 20 tcp + payload) sits
/// safely under the 1514-byte MTU our device.rs enforces on the Unix
/// socket transport. Larger frames silently dropped on the guest side —
/// classic symptom: HTTP response truncates mid-body.
const TX_MAX_PAYLOAD: usize = 1400;

/// TCP connection state
#[derive(Debug, Clone, Copy, PartialEq)]
enum TcpState {
    SynReceived,
    Established,
    FinWait,
    Closed,
}

/// Key for tracking TCP connections
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
struct ConnKey {
    src_port: u16,
    dst_ip: Ipv4Address,
    dst_port: u16,
}

/// A single TCP proxy connection
struct TcpConn {
    state: TcpState,
    stream: Option<TcpStream>,
    /// Our sequence number (gateway → Mac)
    our_seq: u32,
    /// Their sequence number (Mac → gateway)
    their_seq: u32,
    /// Original addresses for building response frames
    mac_ip: Ipv4Address,
    dst_ip: Ipv4Address,
    mac_port: u16,
    dst_port: u16,
    /// Observability: conn lifetime, bytes each way, why it closed.
    opened_at: Instant,
    bytes_in: u64,   // guest → host
    bytes_out: u64,  // host → guest
    close_reason: Option<&'static str>,

    /// TCP flow control (what we know about the Mac's receive window).
    /// `last_mac_ack`: highest ack-number we've seen from the Mac; anything
    /// between this and `our_seq` is data we've sent but the Mac hasn't
    /// yet acknowledged.
    /// `mac_window`: the Mac's most recently advertised receive window.
    /// If we send more than `mac_window` bytes without an ACK, the Mac
    /// silently drops the overflow — catastrophic for large transfers,
    /// invisible to the user except "transfer truncated mysteriously".
    last_mac_ack: u32,
    mac_window: u16,
}

/// Manages all TCP NAT connections.
pub struct TcpNat {
    conns: HashMap<ConnKey, TcpConn>,
    // Lifetime counters for periodic stats logs.
    total_opened: u64,
    total_closed: u64,
    total_connect_failed: u64,
    last_stats_log: Instant,
    // Per-peer connection budget: when this exceeds a soft threshold we
    // log loudly so "works for a few then stops" shows up as "N stuck
    // connections in Established with no byte flow".
    last_activity: Instant,
}

impl TcpNat {
    pub fn new() -> Self {
        Self {
            conns: HashMap::new(),
            total_opened: 0,
            total_closed: 0,
            total_connect_failed: 0,
            last_stats_log: Instant::now(),
            last_activity: Instant::now(),
        }
    }

    /// Process a NAT'd TCP frame from the Mac.
    pub fn handle_frame(&mut self, frame: &[u8], device: &mut SocketDevice) {
        let Ok(eth) = EthernetFrame::new_checked(frame) else { return };
        let Ok(ip) = Ipv4Packet::new_checked(eth.payload()) else { return };

        if ip.next_header() != IpProtocol::Tcp {
            return;
        }

        let ip_hdr_len = ip.header_len() as usize;
        let Ok(tcp) = TcpPacket::new_checked(&eth.payload()[ip_hdr_len..]) else { return };

        let src_ip = ip.src_addr();
        let dst_ip = ip.dst_addr();
        let src_port = tcp.src_port();
        let dst_port = tcp.dst_port();
        let tcp_data_offset = tcp.header_len() as usize;
        let payload = &eth.payload()[ip_hdr_len + tcp_data_offset..];

        let key = ConnKey { src_port, dst_ip, dst_port };

        // SYN: new connection
        if tcp.syn() && !tcp.ack() {
            if self.conns.contains_key(&key) {
                // Retransmitted SYN — resend SYN-ACK
                let conn = self.conns.get(&key).unwrap();
                if conn.state == TcpState::SynReceived {
                    let resp = build_tcp_frame(
                        dst_ip, dst_port, src_ip, src_port,
                        TcpSeqNumber(conn.our_seq as i32),
                        TcpSeqNumber(conn.their_seq as i32),
                        TcpControl::Syn,
                        true, // ACK
                        &[],
                    );
                    device.send_frame(&resp);
                }
                return;
            }

            // Open host connection
            let dst_std = to_std_ip(dst_ip);
            let addr = SocketAddrV4::new(dst_std, dst_port);
            match TcpStream::connect_timeout(
                &std::net::SocketAddr::V4(addr),
                std::time::Duration::from_secs(5),
            ) {
                Ok(stream) => {
                    stream.set_nonblocking(true).unwrap();
                    let our_seq: u32 = 1000; // Simple starting sequence
                    let their_seq = TcpSeqNumber(tcp.seq_number().0 + 1).0 as u32; // SYN consumes 1

                    log::info!(
                        "TCP NAT: connect {}:{} -> {}:{}",
                        src_ip, src_port, dst_ip, dst_port
                    );

                    self.conns.insert(key, TcpConn {
                        state: TcpState::SynReceived,
                        stream: Some(stream),
                        our_seq,
                        their_seq,
                        mac_ip: src_ip,
                        dst_ip,
                        mac_port: src_port,
                        dst_port,
                        opened_at: Instant::now(),
                        bytes_in: 0,
                        bytes_out: 0,
                        close_reason: None,
                        last_mac_ack: our_seq,   // we haven't sent any data yet, so nothing outstanding
                        mac_window: tcp.window_len(),
                    });
                    self.total_opened += 1;
                    self.last_activity = Instant::now();

                    // Send SYN-ACK
                    let resp = build_tcp_frame(
                        dst_ip, dst_port, src_ip, src_port,
                        TcpSeqNumber(our_seq as i32),
                        TcpSeqNumber(their_seq as i32),
                        TcpControl::Syn,
                        true,
                        &[],
                    );
                    device.send_frame(&resp);
                }
                Err(e) => {
                    self.total_connect_failed += 1;
                    log::warn!(
                        "TCP NAT: connect to {}:{} failed: {} (total_failed={})",
                        dst_ip, dst_port, e, self.total_connect_failed
                    );
                    // Send RST
                    let resp = build_tcp_frame(
                        dst_ip, dst_port, src_ip, src_port,
                        TcpSeqNumber(0),
                        TcpSeqNumber(tcp.seq_number().0 + 1),
                        TcpControl::Rst,
                        true,
                        &[],
                    );
                    device.send_frame(&resp);
                }
            }
            return;
        }

        let Some(conn) = self.conns.get_mut(&key) else { return };

        // Learn Mac's current receive window from EVERY incoming frame.
        // Their window shrinks as their TCP buffer fills; expands as the
        // app drains it. Ignoring this means we send past their buffer
        // and they silently drop — THE large-transfer truncation bug.
        conn.mac_window = tcp.window_len();
        if tcp.ack() {
            // TcpSeqNumber is signed i32 but wraps like TCP seq. Cast
            // through u32 for our unsigned arithmetic below.
            let ack = tcp.ack_number().0 as u32;
            // Only advance `last_mac_ack` — never move backwards even if
            // a reordered segment shows an older ack number.
            if ack.wrapping_sub(conn.last_mac_ack) < 0x8000_0000 {
                conn.last_mac_ack = ack;
            }
        }

        // ACK of our SYN-ACK: connection established
        if conn.state == TcpState::SynReceived && tcp.ack() && !tcp.syn() {
            conn.state = TcpState::Established;
            conn.our_seq += 1; // SYN-ACK consumed 1 seq
            conn.last_mac_ack = conn.our_seq; // they've acked up through our SYN-ACK
            log::debug!(
                "TCP NAT: established {}:{} (mac_window={})",
                dst_ip, dst_port, conn.mac_window
            );
        }

        // Data from Mac → host
        if conn.state == TcpState::Established && !payload.is_empty() {
            let write_res = conn
                .stream
                .as_mut()
                .ok_or_else(|| io::Error::new(io::ErrorKind::NotConnected, "stream gone"))
                .and_then(|s| s.write_all(payload));
            match write_res {
                Ok(()) => {
                    conn.their_seq += payload.len() as u32;
                    conn.bytes_in += payload.len() as u64;
                    self.last_activity = Instant::now();
                    let ack = build_tcp_frame(
                        conn.dst_ip, conn.dst_port, conn.mac_ip, conn.mac_port,
                        TcpSeqNumber(conn.our_seq as i32),
                        TcpSeqNumber(conn.their_seq as i32),
                        TcpControl::None,
                        true,
                        &[],
                    );
                    device.send_frame(&ack);
                }
                Err(e) => {
                    log::warn!(
                        "TCP NAT: stream write failed for {}:{} -> {}:{}: {}",
                        conn.mac_ip, conn.mac_port, conn.dst_ip, conn.dst_port, e
                    );
                    conn.close_reason = Some("pipe_write_err");
                    conn.state = TcpState::Closed;
                }
            }
        }

        // FIN from Mac
        if tcp.fin() {
            conn.their_seq += 1; // FIN consumes 1
            if let Some(s) = conn.stream.as_ref() {
                let _ = s.shutdown(std::net::Shutdown::Write);
            }
            conn.close_reason = Some("guest_fin");

            // ACK the FIN + send our FIN
            let fin_ack = build_tcp_frame(
                conn.dst_ip, conn.dst_port, conn.mac_ip, conn.mac_port,
                TcpSeqNumber(conn.our_seq as i32),
                TcpSeqNumber(conn.their_seq as i32),
                TcpControl::Fin,
                true,
                &[],
            );
            device.send_frame(&fin_ack);
            conn.our_seq += 1; // Our FIN consumes 1
            conn.state = TcpState::FinWait;
        }
    }

    /// Poll host sockets for incoming data and relay to Mac.
    pub fn poll(&mut self, device: &mut SocketDevice) {
        let keys: Vec<ConnKey> = self.conns.keys().copied().collect();
        let mut to_remove: Vec<ConnKey> = Vec::new();

        for key in keys {
            let conn = self.conns.get_mut(&key).unwrap();

            if conn.state == TcpState::Closed || conn.state == TcpState::FinWait {
                to_remove.push(key);
                continue;
            }

            if conn.state != TcpState::Established {
                continue;
            }

            // Don't read more from upstream than we can send to the Mac
            // right now. If Mac's receive window is full, just stall this
            // tick — Linux TCP will exert back-pressure on upstream.
            let window_room = available_window(conn);
            if window_room == 0 {
                // Mac's buffer is full. Skip; next tick we'll re-check
                // after any ACK opens it up.
                continue;
            }
            let max_read = (window_room as usize).min(TX_MAX_PAYLOAD);
            let Some(stream) = conn.stream.as_mut() else { continue };
            let mut buf = [0u8; TX_MAX_PAYLOAD];
            let limit = max_read.min(buf.len());
            match stream.read(&mut buf[..limit]) {
                Ok(0) => {
                    log::debug!("TCP NAT: host closed {}:{}", conn.dst_ip, conn.dst_port);
                    let fin = build_tcp_frame(
                        conn.dst_ip, conn.dst_port, conn.mac_ip, conn.mac_port,
                        TcpSeqNumber(conn.our_seq as i32),
                        TcpSeqNumber(conn.their_seq as i32),
                        TcpControl::Fin,
                        true,
                        &[],
                    );
                    device.send_frame(&fin);
                    conn.our_seq += 1;
                    conn.close_reason = Some("host_eof");
                    conn.state = TcpState::FinWait;
                }
                Ok(n) => {
                    let bytes = buf[..n].to_vec();
                    send_data_in_segments(conn, &bytes, device);
                    self.last_activity = Instant::now();
                }
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {}
                Err(e) => {
                    log::warn!(
                        "TCP NAT: stream read error for {}:{} -> {}:{}: {}",
                        conn.mac_ip, conn.mac_port, conn.dst_ip, conn.dst_port, e
                    );
                    conn.close_reason = Some("pipe_read_err");
                    conn.state = TcpState::Closed;
                }
            }
        }

        // Log a close summary for each conn being reaped, then remove.
        for key in &to_remove {
            if let Some(conn) = self.conns.remove(key) {
                let elapsed = conn.opened_at.elapsed().as_millis();
                let reason = conn.close_reason.unwrap_or("unknown");
                log::info!(
                    "TCP close: {}:{} -> {}:{} ({}ms, in={}B out={}B, reason={})",
                    conn.mac_ip, conn.mac_port,
                    conn.dst_ip, conn.dst_port,
                    elapsed,
                    conn.bytes_in, conn.bytes_out,
                    reason
                );
                self.total_closed += 1;
            }
        }

        // Periodic top-of-hour stats + stall detector.
        self.maybe_log_stats();
    }

    /// Emit a one-line stats snapshot every ~5s, plus a warning if the
    /// NAT has open conns but saw no byte activity for a while (the
    /// "works for a few then stops" fingerprint).
    fn maybe_log_stats(&mut self) {
        let now = Instant::now();
        let since_stats = now.duration_since(self.last_stats_log);
        if since_stats.as_secs() < 5 {
            return;
        }
        self.last_stats_log = now;

        // Summarise open-conn state distribution.
        let mut n_syn_rcvd = 0u32;
        let mut n_est = 0u32;
        let mut n_fin = 0u32;
        let mut n_closed = 0u32;
        let mut per_state_pending: Vec<String> = Vec::new();
        for (_, c) in &self.conns {
            match c.state {
                TcpState::SynReceived => n_syn_rcvd += 1,
                TcpState::Established => {
                    n_est += 1;
                    if per_state_pending.len() < 5 {
                        let age_s = c.opened_at.elapsed().as_secs();
                        per_state_pending.push(format!(
                            "{}:{}→{}:{}(in={}B,out={}B,age={}s)",
                            c.mac_ip, c.mac_port, c.dst_ip, c.dst_port,
                            c.bytes_in, c.bytes_out, age_s
                        ));
                    }
                }
                TcpState::FinWait => n_fin += 1,
                TcpState::Closed => n_closed += 1,
            }
        }

        log::info!(
            "TCP stats: open={} (synrcv={} est={} fin={} closed={}) \
             opened_total={} closed_total={} connfail_total={}",
            self.conns.len(), n_syn_rcvd, n_est, n_fin, n_closed,
            self.total_opened, self.total_closed, self.total_connect_failed,
        );
        if !per_state_pending.is_empty() {
            log::info!("TCP established: [{}]", per_state_pending.join(", "));
        }

        // Stall detector: open Established conns but no activity recently.
        let idle = now.duration_since(self.last_activity);
        if n_est > 0 && idle.as_secs() >= 15 {
            log::warn!(
                "TCP NAT: {} established conns but no byte activity for {}s — possible stall",
                n_est, idle.as_secs()
            );
        }
    }
}

/// Convert smoltcp Ipv4Address to std Ipv4Addr
fn to_std_ip(ip: Ipv4Address) -> Ipv4Addr {
    let o = ip.0;
    Ipv4Addr::new(o[0], o[1], o[2], o[3])
}

/// How many bytes of data we can send to the Mac right now without
/// overrunning its advertised receive window. The window shrinks as we
/// send data the Mac hasn't acked yet; grows as the Mac acks.
fn available_window(conn: &TcpConn) -> u32 {
    let in_flight = conn.our_seq.wrapping_sub(conn.last_mac_ack);
    (conn.mac_window as u32).saturating_sub(in_flight)
}

/// Send `payload` to the Mac as one or more TCP data frames, each no
/// larger than `TX_MAX_PAYLOAD`. Advances `conn.our_seq` by the total
/// bytes sent. Classic Mac Ethernet drivers silently drop oversize
/// frames, so segmenting here is load-bearing.
fn send_data_in_segments(conn: &mut TcpConn, payload: &[u8], device: &mut SocketDevice) {
    let mut offset = 0;
    while offset < payload.len() {
        let end = (offset + TX_MAX_PAYLOAD).min(payload.len());
        let chunk = &payload[offset..end];
        let frame = build_tcp_frame(
            conn.dst_ip, conn.dst_port, conn.mac_ip, conn.mac_port,
            TcpSeqNumber(conn.our_seq as i32),
            TcpSeqNumber(conn.their_seq as i32),
            TcpControl::None,
            true,
            chunk,
        );
        device.send_frame(&frame);
        conn.our_seq = conn.our_seq.wrapping_add(chunk.len() as u32);
        conn.bytes_out += chunk.len() as u64;
        offset = end;
    }
}

/// Build a complete ethernet frame containing an IPv4/TCP packet.
/// Uses smoltcp's wire module for correct checksums.
fn build_tcp_frame(
    src_ip: Ipv4Address,
    src_port: u16,
    dst_ip: Ipv4Address,
    dst_port: u16,
    seq: TcpSeqNumber,
    ack: TcpSeqNumber,
    control: TcpControl,
    ack_flag: bool,
    payload: &[u8],
) -> Vec<u8> {
    let tcp_repr = TcpRepr {
        src_port,
        dst_port,
        seq_number: seq,
        ack_number: if ack_flag { Some(ack) } else { None },
        window_len: 65535,
        window_scale: None,
        control,
        max_seg_size: if matches!(control, TcpControl::Syn) { Some(1460) } else { None },
        sack_permitted: false,
        sack_ranges: [None; 3],
        payload,
    };

    let tcp_len = tcp_repr.header_len() + payload.len();

    let ip_repr = Ipv4Repr {
        src_addr: src_ip,
        dst_addr: dst_ip,
        next_header: IpProtocol::Tcp,
        payload_len: tcp_len,
        hop_limit: 64,
    };

    let eth_repr = EthernetRepr {
        src_addr: GW_MAC,
        dst_addr: MAC_ADDR,
        ethertype: EthernetProtocol::Ipv4,
    };

    let total_len = eth_repr.buffer_len() + ip_repr.buffer_len() + tcp_len;
    let mut buf = vec![0u8; total_len];

    let mut eth_frame = EthernetFrame::new_unchecked(&mut buf);
    eth_repr.emit(&mut eth_frame);

    let mut ip_pkt = Ipv4Packet::new_unchecked(eth_frame.payload_mut());
    ip_repr.emit(&mut ip_pkt, &smoltcp::phy::ChecksumCapabilities::default());

    let mut tcp_pkt = TcpPacket::new_unchecked(ip_pkt.payload_mut());
    tcp_repr.emit(
        &mut tcp_pkt,
        &src_ip.into(),
        &dst_ip.into(),
        &smoltcp::phy::ChecksumCapabilities::default(),
    );

    buf
}
