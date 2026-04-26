//! smoltcp Device implementation over a Unix socket.
//!
//! Wire protocol: each ethernet frame is prefixed with a 4-byte big-endian length.
//! Both directions use the same format.
//!
//! Frames from the Mac are classified:
//! - ARP, ICMP, gateway-destined → smoltcp (rx_frames)
//! - TCP/UDP to external IPs → NAT handler (nat_frames)

use std::io::{Read, Write};
use std::os::unix::net::UnixStream;

use smoltcp::phy::{self, Device, DeviceCapabilities, Medium};
use smoltcp::time::Instant;
use smoltcp::wire::{EthernetFrame, EthernetProtocol, IpProtocol, Ipv4Packet, UdpPacket};

fn log_frame_summary(n: u64, frame: &[u8]) {
    let Ok(eth) = EthernetFrame::new_checked(frame) else {
        log::info!("rx#{n} len={} [malformed]", frame.len());
        return;
    };
    let src = eth.src_addr();
    let dst = eth.dst_addr();
    match eth.ethertype() {
        EthernetProtocol::Arp => log::info!("rx#{n} ARP src={src} dst={dst}"),
        EthernetProtocol::Ipv4 => {
            let Ok(ip) = Ipv4Packet::new_checked(eth.payload()) else {
                log::info!("rx#{n} IPv4 [malformed]"); return;
            };
            let proto = ip.next_header();
            let sip = ip.src_addr();
            let dip = ip.dst_addr();
            if proto == IpProtocol::Udp {
                let ip_hdr_len = ip.header_len() as usize;
                if let Ok(udp) = UdpPacket::new_checked(&eth.payload()[ip_hdr_len..]) {
                    log::info!("rx#{n} UDP {sip}:{} -> {dip}:{}",
                        udp.src_port(), udp.dst_port());
                    return;
                }
            }
            log::info!("rx#{n} IPv4 {sip} -> {dip} proto={proto}");
        }
        other => log::info!("rx#{n} ethertype={other} src={src} dst={dst}"),
    }
}

/// Maximum ethernet frame size
const MTU: usize = 1514;
/// Frame header: 4-byte big-endian length prefix
const HEADER_LEN: usize = 4;
/// Gateway IP (for classification)
const GW_IP_BYTES: [u8; 4] = [10, 0, 2, 1];

pub struct SocketDevice {
    stream: UnixStream,
    rx_buf: Vec<u8>,
    /// Frames for smoltcp (ARP, ICMP, gateway traffic)
    rx_frames: Vec<Vec<u8>>,
    /// Frames for NAT handler (TCP/UDP to external IPs)
    pub nat_frames: Vec<Vec<u8>>,
    /// Outgoing frames (from smoltcp or NAT handler)
    tx_frames: Vec<Vec<u8>>,
    /// Debug: number of frames received so far (Mac → bridge)
    rx_count: u64,
}

impl SocketDevice {
    pub fn new(stream: UnixStream) -> Self {
        Self {
            stream,
            rx_buf: Vec::with_capacity(4096),
            rx_frames: Vec::new(),
            nat_frames: Vec::new(),
            tx_frames: Vec::new(),
            rx_count: 0,
        }
    }

    /// Read available data from the socket, classify into smoltcp vs NAT queues.
    pub fn drain_socket(&mut self) {
        let mut tmp = [0u8; 8192];
        loop {
            match self.stream.read(&mut tmp) {
                Ok(0) => {
                    log::error!("Client disconnected");
                    std::process::exit(0);
                }
                Ok(n) => {
                    self.rx_buf.extend_from_slice(&tmp[..n]);
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                Err(e) => {
                    log::error!("Socket read error: {}", e);
                    std::process::exit(1);
                }
            }
        }

        while self.rx_buf.len() >= HEADER_LEN {
            let frame_len = u32::from_be_bytes([
                self.rx_buf[0], self.rx_buf[1], self.rx_buf[2], self.rx_buf[3],
            ]) as usize;

            if frame_len == 0 || frame_len > MTU {
                log::warn!("Invalid frame length: {}", frame_len);
                self.rx_buf.clear();
                break;
            }

            if self.rx_buf.len() < HEADER_LEN + frame_len {
                break;
            }

            let frame = self.rx_buf[HEADER_LEN..HEADER_LEN + frame_len].to_vec();
            self.rx_buf.drain(..HEADER_LEN + frame_len);

            self.rx_count += 1;
            if self.rx_count <= 30 {
                log_frame_summary(self.rx_count, &frame);
            } else if self.rx_count == 31 {
                log::info!("(further frames suppressed; seen 30)");
            }

            if self.is_nat_frame(&frame) {
                self.nat_frames.push(frame);
            } else {
                self.rx_frames.push(frame);
            }
        }
    }

    /// Classify: should this frame go to NAT handler instead of smoltcp?
    fn is_nat_frame(&self, frame: &[u8]) -> bool {
        if frame.len() < 14 {
            return false;
        }

        let Ok(eth) = EthernetFrame::new_checked(frame) else {
            return false;
        };

        // Only IPv4 can be NAT'd
        if eth.ethertype() != smoltcp::wire::EthernetProtocol::Ipv4 {
            return false;
        }

        let Ok(ip) = Ipv4Packet::new_checked(eth.payload()) else {
            return false;
        };

        let dst_ip = ip.dst_addr();

        // If destined for gateway, let smoltcp handle most traffic (ping, etc.)
        // Exception: UDP port 53 (DNS) goes to NAT for DNS proxy
        if dst_ip.0 == GW_IP_BYTES {
            if ip.next_header() == smoltcp::wire::IpProtocol::Udp {
                let ip_hdr_len = ip.header_len() as usize;
                if let Ok(udp) = smoltcp::wire::UdpPacket::new_checked(&eth.payload()[ip_hdr_len..]) {
                    if udp.dst_port() == 53 {
                        return true; // DNS → NAT proxy
                    }
                }
            }
            return false;
        }

        // Broadcast: intercept DHCP (UDP port 67), let smoltcp handle the rest
        if dst_ip.0[0] >= 224 || dst_ip == smoltcp::wire::Ipv4Address::BROADCAST {
            if ip.next_header() == smoltcp::wire::IpProtocol::Udp {
                let ip_hdr_len = ip.header_len() as usize;
                if let Ok(udp) = smoltcp::wire::UdpPacket::new_checked(&eth.payload()[ip_hdr_len..]) {
                    if udp.dst_port() == 67 {
                        return true; // DHCP → NAT handler
                    }
                }
            }
            return false;
        }

        // TCP or UDP to external IP → NAT
        let proto = ip.next_header();
        matches!(
            proto,
            smoltcp::wire::IpProtocol::Tcp
                | smoltcp::wire::IpProtocol::Udp
                | smoltcp::wire::IpProtocol::Icmp
        )
    }

    /// Send a frame to the client (from NAT handler).
    pub fn send_frame(&mut self, frame: &[u8]) {
        self.tx_frames.push(frame.to_vec());
    }

    /// Flush pending TX frames to the socket.
    pub fn flush_tx(&mut self) {
        for frame in self.tx_frames.drain(..) {
            let len = frame.len() as u32;
            let header = len.to_be_bytes();
            let _ = self.stream.write_all(&header);
            let _ = self.stream.write_all(&frame);
        }
    }
}

// --- smoltcp Device trait ---

impl Device for SocketDevice {
    type RxToken<'a> = RxToken;
    type TxToken<'a> = TxToken<'a>;

    fn receive(&mut self, _timestamp: Instant) -> Option<(Self::RxToken<'_>, Self::TxToken<'_>)> {
        // drain_socket is called explicitly from the main loop now
        if self.rx_frames.is_empty() {
            return None;
        }

        let frame = self.rx_frames.remove(0);
        Some((
            RxToken { buffer: frame },
            TxToken { tx_frames: &mut self.tx_frames },
        ))
    }

    fn transmit(&mut self, _timestamp: Instant) -> Option<Self::TxToken<'_>> {
        Some(TxToken { tx_frames: &mut self.tx_frames })
    }

    fn capabilities(&self) -> DeviceCapabilities {
        let mut caps = DeviceCapabilities::default();
        caps.medium = Medium::Ethernet;
        caps.max_transmission_unit = MTU;
        caps
    }
}

pub struct RxToken {
    buffer: Vec<u8>,
}

impl phy::RxToken for RxToken {
    fn consume<R, F>(mut self, f: F) -> R
    where
        F: FnOnce(&mut [u8]) -> R,
    {
        f(&mut self.buffer)
    }
}

pub struct TxToken<'a> {
    tx_frames: &'a mut Vec<Vec<u8>>,
}

impl<'a> phy::TxToken for TxToken<'a> {
    fn consume<R, F>(self, len: usize, f: F) -> R
    where
        F: FnOnce(&mut [u8]) -> R,
    {
        let mut buffer = vec![0u8; len];
        let result = f(&mut buffer);
        self.tx_frames.push(buffer);
        result
    }
}
