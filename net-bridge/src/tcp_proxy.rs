//! TCP NAT proxy: intercepts TCP frames from the Mac, manages host connections,
//! and crafts response frames using smoltcp's wire module.

use std::collections::HashMap;
use std::io::{self, Read, Write};
use std::net::{Ipv4Addr, SocketAddrV4, TcpStream};
use std::sync::Arc;

use smoltcp::wire::{
    EthernetAddress, EthernetFrame, EthernetProtocol, EthernetRepr,
    IpProtocol, Ipv4Address, Ipv4Packet, Ipv4Repr,
    TcpControl, TcpPacket, TcpRepr, TcpSeqNumber,
};

use crate::device::SocketDevice;
use crate::tls_listener::TlsBridge;
use crate::tls_mitm::{MitmCa, MitmRuntime, MitmSession, MitmState};

/// MAC addresses
const MAC_ADDR: EthernetAddress = EthernetAddress([0x02, 0x50, 0x48, 0x58, 0x00, 0x01]);
const GW_MAC: EthernetAddress = EthernetAddress([0x02, 0x50, 0x48, 0x58, 0x00, 0x02]);

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

/// Byte-pipe mode of a TCP connection. Raw is the default NAT; the MITM
/// variants take over once the dst_port matches the MITM config.
enum Pipe {
    /// Non-MITM: bytes flow guest ↔ real TCP socket unchanged.
    Raw(TcpStream),
    /// MITM-enabled: sniffing SNI from the first ClientHello. Guest bytes
    /// are buffered in `session` (not yet forwarded) so the real
    /// ClientHello stays on our side until we know the SNI and can mint
    /// the leaf cert. Transitions to `Bridging` or `Raw` (abandon).
    Sniffing {
        stream: TcpStream,
        session: MitmSession,
    },
    /// Full TLS MITM: guest talks legacy TLS to `bridge`; `bridge` talks
    /// modern TLS to the real server. The TcpStream is owned by the
    /// bridge now.
    Bridging(Box<TlsBridge>),
    /// Dummy variant used only as a short-lived placeholder during state
    /// transitions (so we can move the previous value out of the enum).
    Closed,
}

impl Default for Pipe {
    fn default() -> Self {
        Pipe::Closed
    }
}

/// A single TCP proxy connection
struct TcpConn {
    state: TcpState,
    pipe: Pipe,
    /// Our sequence number (gateway → Mac)
    our_seq: u32,
    /// Their sequence number (Mac → gateway)
    their_seq: u32,
    /// Original addresses for building response frames
    mac_ip: Ipv4Address,
    dst_ip: Ipv4Address,
    mac_port: u16,
    dst_port: u16,
}

/// Manages all TCP NAT connections.
pub struct TcpNat {
    conns: HashMap<ConnKey, TcpConn>,
    mitm: Option<MitmRuntime>,
}

impl TcpNat {
    pub fn new(mitm: Option<MitmRuntime>) -> Self {
        Self {
            conns: HashMap::new(),
            mitm,
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
            let is_mitm = self.mitm.as_ref().is_some_and(|m| m.matches(dst_port));
            match TcpStream::connect_timeout(
                &std::net::SocketAddr::V4(addr),
                std::time::Duration::from_secs(5),
            ) {
                Ok(stream) => {
                    stream.set_nonblocking(true).unwrap();
                    let our_seq: u32 = 1000; // Simple starting sequence
                    let their_seq = TcpSeqNumber(tcp.seq_number().0 + 1).0 as u32; // SYN consumes 1

                    if is_mitm {
                        log::info!(
                            "TCP NAT [MITM]: connect {}:{} -> {}:{}",
                            src_ip, src_port, dst_ip, dst_port
                        );
                    } else {
                        log::info!(
                            "TCP NAT: connect {}:{} -> {}:{}",
                            src_ip, src_port, dst_ip, dst_port
                        );
                    }

                    let pipe = if is_mitm {
                        Pipe::Sniffing {
                            stream,
                            session: MitmSession::new(dst_std, dst_port),
                        }
                    } else {
                        Pipe::Raw(stream)
                    };

                    self.conns.insert(key, TcpConn {
                        state: TcpState::SynReceived,
                        pipe,
                        our_seq,
                        their_seq,
                        mac_ip: src_ip,
                        dst_ip,
                        mac_port: src_port,
                        dst_port,
                    });

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
                    log::warn!("TCP NAT: connect to {}:{} failed: {}", dst_ip, dst_port, e);
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

        // ACK of our SYN-ACK: connection established
        if conn.state == TcpState::SynReceived && tcp.ack() && !tcp.syn() {
            conn.state = TcpState::Established;
            conn.our_seq += 1; // SYN-ACK consumed 1 seq
            log::debug!("TCP NAT: established {}:{}", dst_ip, dst_port);
        }

        // Data from Mac → host
        if conn.state == TcpState::Established && !payload.is_empty() {
            let ca = self.mitm.as_ref().map(|m| Arc::clone(&m.ca));
            match handle_guest_data(conn, payload, ca.as_ref()) {
                Ok(to_guest) => {
                    conn.their_seq += payload.len() as u32;
                    // ACK the data (at TCP layer) regardless of pipe mode.
                    let ack = build_tcp_frame(
                        conn.dst_ip, conn.dst_port, conn.mac_ip, conn.mac_port,
                        TcpSeqNumber(conn.our_seq as i32),
                        TcpSeqNumber(conn.their_seq as i32),
                        TcpControl::None,
                        true,
                        &[],
                    );
                    device.send_frame(&ack);
                    // Bridge-produced ciphertext back to Mac rides its own frame
                    // so the window is advanced correctly.
                    if !to_guest.is_empty() {
                        let data = build_tcp_frame(
                            conn.dst_ip, conn.dst_port, conn.mac_ip, conn.mac_port,
                            TcpSeqNumber(conn.our_seq as i32),
                            TcpSeqNumber(conn.their_seq as i32),
                            TcpControl::None,
                            true,
                            &to_guest,
                        );
                        device.send_frame(&data);
                        conn.our_seq += to_guest.len() as u32;
                    }
                }
                Err(e) => {
                    log::warn!("TCP NAT: pipe write failed: {}", e);
                    conn.state = TcpState::Closed;
                }
            }
        }

        // FIN from Mac
        if tcp.fin() {
            conn.their_seq += 1; // FIN consumes 1
            shutdown_pipe_write(&mut conn.pipe);

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

            let outcome = poll_pipe(&mut conn.pipe);
            match outcome {
                PollOutcome::Bytes(bytes) => {
                    if !bytes.is_empty() {
                        let data_frame = build_tcp_frame(
                            conn.dst_ip, conn.dst_port, conn.mac_ip, conn.mac_port,
                            TcpSeqNumber(conn.our_seq as i32),
                            TcpSeqNumber(conn.their_seq as i32),
                            TcpControl::None,
                            true,
                            &bytes,
                        );
                        device.send_frame(&data_frame);
                        conn.our_seq += bytes.len() as u32;
                    }
                }
                PollOutcome::Eof => {
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
                    conn.state = TcpState::FinWait;
                }
                PollOutcome::Err(e) => {
                    log::warn!("TCP NAT: pipe read error: {}", e);
                    conn.state = TcpState::Closed;
                }
                PollOutcome::Idle => {}
            }
        }

        for key in to_remove {
            self.conns.remove(&key);
        }
    }
}

/// Convert smoltcp Ipv4Address to std Ipv4Addr
fn to_std_ip(ip: Ipv4Address) -> Ipv4Addr {
    let o = ip.octets();
    Ipv4Addr::new(o[0], o[1], o[2], o[3])
}

enum PollOutcome {
    Idle,
    Bytes(Vec<u8>),
    Eof,
    Err(String),
}

/// Feed a Mac→host TCP payload into whatever byte pipe this connection
/// currently uses. Returns bytes that should be sent back to Mac on the
/// same tick (bridge-produced ciphertext; empty for Raw/Sniffing).
fn handle_guest_data(
    conn: &mut TcpConn,
    payload: &[u8],
    ca: Option<&Arc<MitmCa>>,
) -> io::Result<Vec<u8>> {
    let old = std::mem::take(&mut conn.pipe);
    let (new, bytes) = match old {
        Pipe::Raw(mut s) => {
            s.write_all(payload)?;
            (Pipe::Raw(s), Vec::new())
        }
        Pipe::Sniffing { mut stream, mut session } => {
            session.observe_guest_bytes(payload);
            match session.state().clone() {
                MitmState::Sniffing => {
                    // Still collecting; don't forward yet — bytes stay in `session.buf`.
                    (Pipe::Sniffing { stream, session }, Vec::new())
                }
                s @ (MitmState::SniKnown(_) | MitmState::SniAbsent) => {
                    let host = match s {
                        MitmState::SniKnown(h) => h,
                        _ => session.resolved_host().unwrap_or_else(|| {
                            conn.dst_ip.to_string()
                        }),
                    };
                    let buffered = session.take_buffer();
                    log::info!(
                        "MITM TLS begin: {}:{} (host={})",
                        conn.dst_ip, conn.dst_port, host
                    );
                    let ca = ca.ok_or_else(|| io::Error::new(
                        io::ErrorKind::Other, "MITM triggered but CA unavailable",
                    ))?;
                    let mut bridge = match TlsBridge::new(ca.as_ref(), &host, stream) {
                        Ok(b) => Box::new(b),
                        Err(e) => {
                            return Err(io::Error::new(io::ErrorKind::Other,
                                format!("TlsBridge::new: {}", e)));
                        }
                    };
                    bridge.feed_from_guest(&buffered);
                    let tick = bridge.drive().map_err(|e|
                        io::Error::new(io::ErrorKind::Other, format!("bridge drive: {}", e))
                    )?;
                    (Pipe::Bridging(bridge), tick.bytes_to_guest)
                }
                MitmState::Abandoned => {
                    // Fall back to raw: flush buffered bytes + current payload.
                    let buffered = session.take_buffer();
                    if !buffered.is_empty() {
                        stream.write_all(&buffered)?;
                    }
                    stream.write_all(payload)?;
                    (Pipe::Raw(stream), Vec::new())
                }
            }
        }
        Pipe::Bridging(mut bridge) => {
            bridge.feed_from_guest(payload);
            let tick = bridge.drive().map_err(|e|
                io::Error::new(io::ErrorKind::Other, format!("bridge drive: {}", e))
            )?;
            (Pipe::Bridging(bridge), tick.bytes_to_guest)
        }
        Pipe::Closed => (Pipe::Closed, Vec::new()),
    };
    conn.pipe = new;
    Ok(bytes)
}

/// Drain any bytes the pipe has ready to send to the Mac. Called each tick.
fn poll_pipe(pipe: &mut Pipe) -> PollOutcome {
    match pipe {
        Pipe::Raw(s) | Pipe::Sniffing { stream: s, .. } => {
            let mut buf = [0u8; 4096];
            match s.read(&mut buf) {
                Ok(0) => PollOutcome::Eof,
                Ok(n) => PollOutcome::Bytes(buf[..n].to_vec()),
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => PollOutcome::Idle,
                Err(e) => PollOutcome::Err(e.to_string()),
            }
        }
        Pipe::Bridging(bridge) => {
            match bridge.drive() {
                Ok(tick) => {
                    if tick.upstream_eof && tick.downstream_eof {
                        PollOutcome::Eof
                    } else if !tick.bytes_to_guest.is_empty() {
                        PollOutcome::Bytes(tick.bytes_to_guest)
                    } else {
                        PollOutcome::Idle
                    }
                }
                Err(e) => PollOutcome::Err(format!("bridge drive: {}", e)),
            }
        }
        Pipe::Closed => PollOutcome::Idle,
    }
}

fn shutdown_pipe_write(pipe: &mut Pipe) {
    match pipe {
        Pipe::Raw(s) | Pipe::Sniffing { stream: s, .. } => {
            let _ = s.shutdown(std::net::Shutdown::Write);
        }
        Pipe::Bridging(bridge) => bridge.close_downstream(),
        Pipe::Closed => {}
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
        timestamp: None,
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
