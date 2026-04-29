//! ICMP NAT proxy: handles external ping (echo request/reply relay)
//! and traceroute TTL-exceeded generation.

use std::collections::HashMap;
use std::os::fd::{AsRawFd, FromRawFd, OwnedFd};
use std::time::Instant;

use smoltcp::wire::{
    EthernetAddress, EthernetFrame, EthernetProtocol, EthernetRepr,
    Icmpv4Packet, IpProtocol, Ipv4Address, Ipv4Packet, Ipv4Repr,
};

use crate::device::{SocketDevice, GW_IP, GW_MAC};

/// Connection timeout
const ICMP_TIMEOUT_SECS: u64 = 10;

/// ICMP types
const ICMP_ECHO_REQUEST: u8 = 8;
const ICMP_ECHO_REPLY: u8 = 0;
const ICMP_TIME_EXCEEDED: u8 = 11;

/// Key for tracking ICMP echo sessions. Includes guest src IP so two
/// guests pinging the same target with the same id+seq don't collide.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
struct IcmpKey {
    src_ip: Ipv4Address,
    dst_ip: Ipv4Address,
    /// Original ICMP id from the Mac
    orig_id: u16,
    seq: u16,
}

/// A pending ICMP echo request
struct IcmpPending {
    /// The raw host socket fd
    fd: OwnedFd,
    /// Original ICMP id (Mac's id, which the kernel replaces)
    orig_id: u16,
    /// Source IP of the Mac
    mac_ip: Ipv4Address,
    /// MAC address of the originating guest, for L2 routing on replies.
    mac_addr: EthernetAddress,
    /// Destination IP
    dst_ip: Ipv4Address,
    /// Original sequence number
    seq: u16,
    /// When the request was sent
    sent_at: Instant,
}

/// Manages ICMP NAT (external ping proxy).
pub struct IcmpNat {
    pending: HashMap<IcmpKey, IcmpPending>,
}

impl IcmpNat {
    pub fn new() -> Self {
        Self {
            pending: HashMap::new(),
        }
    }

    /// Handle an ICMP frame from the Mac destined for an external IP.
    pub fn handle_frame(&mut self, frame: &[u8], _device: &mut SocketDevice) {
        let Ok(eth) = EthernetFrame::new_checked(frame) else { return };
        let Ok(ip) = Ipv4Packet::new_checked(eth.payload()) else { return };

        if ip.next_header() != IpProtocol::Icmp {
            return;
        }

        let ip_hdr_len = ip.header_len() as usize;
        let icmp_data = &eth.payload()[ip_hdr_len..];
        if icmp_data.len() < 8 {
            return;
        }

        let Ok(icmp) = Icmpv4Packet::new_checked(icmp_data) else { return };

        // Only handle echo requests (type 8)
        let msg_type: u8 = icmp.msg_type().into();
        if msg_type != ICMP_ECHO_REQUEST {
            return;
        }

        // Parse id and seq from echo header (bytes 4-7 of ICMP)
        let orig_id = u16::from_be_bytes([icmp_data[4], icmp_data[5]]);
        let seq = u16::from_be_bytes([icmp_data[6], icmp_data[7]]);
        let payload = &icmp_data[8..];

        let src_mac = eth.src_addr();
        let src_ip = ip.src_addr();
        let dst_ip = ip.dst_addr();

        log::info!(
            "ICMP NAT: echo request from {} to {}, id={:#06x} seq={}",
            src_ip, dst_ip, orig_id, seq
        );

        // Open unprivileged ICMP socket
        let fd = unsafe {
            libc::socket(libc::AF_INET, libc::SOCK_DGRAM | libc::SOCK_NONBLOCK, libc::IPPROTO_ICMP)
        };
        if fd < 0 {
            log::warn!("ICMP NAT: failed to create ICMP socket: {}", std::io::Error::last_os_error());
            return;
        }
        let fd = unsafe { OwnedFd::from_raw_fd(fd) };

        // Build ICMP echo request packet (type=8, code=0, checksum, id, seq, payload)
        let mut icmp_buf = Vec::with_capacity(8 + payload.len());
        icmp_buf.push(ICMP_ECHO_REQUEST); // type
        icmp_buf.push(0);                  // code
        icmp_buf.push(0);                  // checksum (placeholder)
        icmp_buf.push(0);
        icmp_buf.extend_from_slice(&orig_id.to_be_bytes()); // id
        icmp_buf.extend_from_slice(&seq.to_be_bytes());     // seq
        icmp_buf.extend_from_slice(payload);

        // Compute checksum
        let cksum = internet_checksum(&icmp_buf);
        icmp_buf[2] = (cksum >> 8) as u8;
        icmp_buf[3] = (cksum & 0xff) as u8;

        // Send via sendto
        let dst_octets = dst_ip.0;
        let mut addr: libc::sockaddr_in = unsafe { std::mem::zeroed() };
        addr.sin_family = libc::AF_INET as u16;
        addr.sin_addr.s_addr = u32::from_ne_bytes(dst_octets);

        let ret = unsafe {
            libc::sendto(
                fd.as_raw_fd(),
                icmp_buf.as_ptr() as *const libc::c_void,
                icmp_buf.len(),
                0,
                &addr as *const libc::sockaddr_in as *const libc::sockaddr,
                std::mem::size_of::<libc::sockaddr_in>() as u32,
            )
        };

        if ret < 0 {
            log::warn!("ICMP NAT: sendto failed: {}", std::io::Error::last_os_error());
            return;
        }

        let key = IcmpKey { src_ip, dst_ip, orig_id, seq };
        self.pending.insert(key, IcmpPending {
            fd,
            orig_id,
            mac_ip: src_ip,
            mac_addr: src_mac,
            dst_ip,
            seq,
            sent_at: Instant::now(),
        });
    }

    /// Poll for ICMP replies from the host and relay back to the Mac.
    pub fn poll(&mut self, device: &mut SocketDevice) {
        let keys: Vec<IcmpKey> = self.pending.keys().copied().collect();
        let mut to_remove: Vec<IcmpKey> = Vec::new();

        for key in &keys {
            let pending = self.pending.get(key).unwrap();

            // Check timeout
            if pending.sent_at.elapsed().as_secs() >= ICMP_TIMEOUT_SECS {
                log::debug!("ICMP NAT: timeout for {} id={:#06x}", pending.dst_ip, pending.orig_id);
                to_remove.push(*key);
                continue;
            }

            // Try recvfrom (non-blocking)
            let mut buf = [0u8; 1500];
            let mut from_addr: libc::sockaddr_in = unsafe { std::mem::zeroed() };
            let mut addr_len: libc::socklen_t = std::mem::size_of::<libc::sockaddr_in>() as u32;

            let n = unsafe {
                libc::recvfrom(
                    pending.fd.as_raw_fd(),
                    buf.as_mut_ptr() as *mut libc::c_void,
                    buf.len(),
                    0,
                    &mut from_addr as *mut libc::sockaddr_in as *mut libc::sockaddr,
                    &mut addr_len,
                )
            };

            if n <= 0 {
                continue;
            }

            let n = n as usize;
            if n < 8 {
                continue;
            }

            // The kernel returns an ICMP packet: type(1), code(1), checksum(2), id(2), seq(2), payload
            // For SOCK_DGRAM+IPPROTO_ICMP, kernel returns just the ICMP portion (no IP header)
            let reply_type = buf[0];
            if reply_type != ICMP_ECHO_REPLY {
                continue;
            }

            let reply_seq = u16::from_be_bytes([buf[6], buf[7]]);
            let reply_payload = &buf[8..n];

            log::info!(
                "ICMP NAT: echo reply from {}, id={:#06x} seq={}",
                pending.dst_ip, pending.orig_id, reply_seq
            );

            // Rebuild ICMP reply with the original id (kernel substituted its own)
            let mut icmp_reply = Vec::with_capacity(n);
            icmp_reply.push(ICMP_ECHO_REPLY);  // type=0
            icmp_reply.push(0);                  // code=0
            icmp_reply.push(0);                  // checksum placeholder
            icmp_reply.push(0);
            icmp_reply.extend_from_slice(&pending.orig_id.to_be_bytes()); // restore original id
            icmp_reply.extend_from_slice(&pending.seq.to_be_bytes());     // restore original seq
            icmp_reply.extend_from_slice(reply_payload);

            // Recompute checksum
            let cksum = internet_checksum(&icmp_reply);
            icmp_reply[2] = (cksum >> 8) as u8;
            icmp_reply[3] = (cksum & 0xff) as u8;

            // Build ethernet frame
            let frame = build_icmp_frame(
                pending.dst_ip,
                pending.mac_ip,
                pending.mac_addr,
                64,
                &icmp_reply,
            );
            device.send_frame(&frame);

            to_remove.push(*key);
        }

        for key in to_remove {
            self.pending.remove(&key);
        }
    }
}

/// Check if an IP frame has TTL <= 1. If so, generate ICMP Time Exceeded
/// and return true (meaning the frame should NOT be forwarded).
/// Called from main.rs before dispatching to protocol-specific handlers.
pub fn check_ttl_exceeded(frame: &[u8], device: &mut SocketDevice) -> bool {
    let Ok(eth) = EthernetFrame::new_checked(frame) else { return false };
    if eth.ethertype() != EthernetProtocol::Ipv4 {
        return false;
    }
    let Ok(ip) = Ipv4Packet::new_checked(eth.payload()) else { return false };

    if ip.hop_limit() > 1 {
        return false;
    }

    let src_mac = eth.src_addr();
    let src_ip = ip.src_addr();
    let ip_payload = eth.payload();

    // Per RFC 792: ICMP Time Exceeded payload = original IP header + first 8 bytes of IP payload
    let ip_hdr_len = ip.header_len() as usize;
    let copy_len = std::cmp::min(ip_payload.len(), ip_hdr_len + 8);
    let original_data = &ip_payload[..copy_len];

    log::info!(
        "TTL exceeded: packet from {} to {} (proto={:?}, TTL={})",
        src_ip, ip.dst_addr(), ip.next_header(), ip.hop_limit()
    );

    // Build ICMP Time Exceeded message:
    // type(1)=11, code(1)=0, checksum(2), unused(4), then original IP header + 8 bytes
    let mut icmp_buf = Vec::with_capacity(8 + original_data.len());
    icmp_buf.push(ICMP_TIME_EXCEEDED); // type = 11
    icmp_buf.push(0);                   // code = 0 (TTL exceeded in transit)
    icmp_buf.push(0);                   // checksum placeholder
    icmp_buf.push(0);
    icmp_buf.extend_from_slice(&[0u8; 4]); // unused 4 bytes
    icmp_buf.extend_from_slice(original_data);

    // Compute checksum
    let cksum = internet_checksum(&icmp_buf);
    icmp_buf[2] = (cksum >> 8) as u8;
    icmp_buf[3] = (cksum & 0xff) as u8;

    // Build ethernet frame: from gateway to Mac
    let frame = build_icmp_frame(GW_IP, src_ip, src_mac, 64, &icmp_buf);
    device.send_frame(&frame);

    true
}

/// Build a complete ethernet frame containing an IPv4/ICMP packet.
/// `dst_mac` is the destination guest's MAC for L2 routing.
fn build_icmp_frame(
    src_ip: Ipv4Address,
    dst_ip: Ipv4Address,
    dst_mac: EthernetAddress,
    hop_limit: u8,
    icmp_payload: &[u8],
) -> Vec<u8> {
    let ip_repr = Ipv4Repr {
        src_addr: src_ip,
        dst_addr: dst_ip,
        next_header: IpProtocol::Icmp,
        payload_len: icmp_payload.len(),
        hop_limit,
    };

    let eth_repr = EthernetRepr {
        src_addr: GW_MAC,
        dst_addr: dst_mac,
        ethertype: EthernetProtocol::Ipv4,
    };

    let total_len = eth_repr.buffer_len() + ip_repr.buffer_len() + icmp_payload.len();
    let mut buf = vec![0u8; total_len];

    let mut eth_frame = smoltcp::wire::EthernetFrame::new_unchecked(&mut buf);
    eth_repr.emit(&mut eth_frame);

    let mut ip_pkt = Ipv4Packet::new_unchecked(eth_frame.payload_mut());
    ip_repr.emit(&mut ip_pkt, &smoltcp::phy::ChecksumCapabilities::default());

    // Copy raw ICMP data (already has correct checksum)
    ip_pkt.payload_mut()[..icmp_payload.len()].copy_from_slice(icmp_payload);

    buf
}

/// Standard internet checksum (RFC 1071).
fn internet_checksum(data: &[u8]) -> u16 {
    let mut sum: u32 = 0;
    let mut i = 0;
    while i + 1 < data.len() {
        sum += u16::from_be_bytes([data[i], data[i + 1]]) as u32;
        i += 2;
    }
    if i < data.len() {
        sum += (data[i] as u32) << 8;
    }
    while sum >> 16 != 0 {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    !sum as u16
}
