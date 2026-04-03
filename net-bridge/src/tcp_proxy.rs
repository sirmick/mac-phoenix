//! TCP NAT proxy: intercepts TCP frames from the Mac, manages host connections,
//! and crafts response frames using smoltcp's wire module.

use std::collections::HashMap;
use std::io::{self, Read, Write};
use std::net::{Ipv4Addr, SocketAddrV4, TcpStream};

use smoltcp::wire::{
    EthernetAddress, EthernetFrame, EthernetProtocol, EthernetRepr,
    IpProtocol, Ipv4Address, Ipv4Packet, Ipv4Repr,
    TcpControl, TcpPacket, TcpRepr, TcpSeqNumber,
};

use crate::device::SocketDevice;

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

/// A single TCP proxy connection
struct TcpConn {
    state: TcpState,
    host_stream: TcpStream,
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
}

impl TcpNat {
    pub fn new() -> Self {
        Self {
            conns: HashMap::new(),
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

                    log::info!("TCP NAT: {} {}:{} -> {}:{}",
                        "connect", src_ip, src_port, dst_ip, dst_port);

                    self.conns.insert(key, TcpConn {
                        state: TcpState::SynReceived,
                        host_stream: stream,
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
            match conn.host_stream.write_all(payload) {
                Ok(()) => {
                    conn.their_seq += payload.len() as u32;
                    // ACK the data
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
                    log::warn!("TCP NAT: host write failed: {}", e);
                    conn.state = TcpState::Closed;
                }
            }
        }

        // FIN from Mac
        if tcp.fin() {
            conn.their_seq += 1; // FIN consumes 1
            let _ = conn.host_stream.shutdown(std::net::Shutdown::Write);

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

            // Read from host → send to Mac
            let mut buf = [0u8; 4096];
            match conn.host_stream.read(&mut buf) {
                Ok(0) => {
                    // Host closed: send FIN to Mac
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
                Ok(n) => {
                    // Send data to Mac
                    let data_frame = build_tcp_frame(
                        conn.dst_ip, conn.dst_port, conn.mac_ip, conn.mac_port,
                        TcpSeqNumber(conn.our_seq as i32),
                        TcpSeqNumber(conn.their_seq as i32),
                        TcpControl::None,
                        true,
                        &buf[..n],
                    );
                    device.send_frame(&data_frame);
                    conn.our_seq += n as u32;
                }
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {}
                Err(e) => {
                    log::warn!("TCP NAT: host read error: {}", e);
                    conn.state = TcpState::Closed;
                }
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
