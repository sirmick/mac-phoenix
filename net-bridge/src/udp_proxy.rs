//! UDP NAT proxy: intercepts UDP frames from the Mac, manages host DGRAM sockets,
//! and crafts response frames using smoltcp's wire module.
//! Includes DNS proxy: redirects gateway DNS (port 53) to host's real DNS server.

use std::collections::HashMap;
use std::io;
use std::net::{Ipv4Addr, SocketAddrV4, UdpSocket};
use std::os::fd::AsRawFd;
use std::time::Instant;

use smoltcp::wire::{
    EthernetAddress, EthernetFrame, EthernetProtocol, EthernetRepr,
    IpProtocol, Ipv4Address, Ipv4Packet, Ipv4Repr,
    UdpPacket, UdpRepr,
};

use crate::device::{SocketDevice, GW_IP, GW_MAC};

/// Connection timeout (seconds)
const CONN_TIMEOUT_SECS: u64 = 60;

/// Key for tracking UDP flows. Includes the guest's source IP so two
/// guests on the same bridge can pick identical (src_port, dst) tuples
/// without colliding.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
struct UdpFlowKey {
    src_ip: Ipv4Address,
    src_port: u16,
    dst_ip: Ipv4Address,
    dst_port: u16,
}

/// A single UDP NAT flow
struct UdpFlow {
    host_socket: UdpSocket,
    /// Original addresses for building response frames
    mac_ip: Ipv4Address,
    /// MAC address of the originating guest, for L2 routing on replies.
    mac_addr: EthernetAddress,
    /// The IP that the Mac thinks it's talking to (for response src_ip)
    apparent_dst_ip: Ipv4Address,
    mac_port: u16,
    dst_port: u16,
    last_activity: Instant,
}

/// Manages all UDP NAT flows.
pub struct UdpNat {
    flows: HashMap<UdpFlowKey, UdpFlow>,
    /// Cached host DNS server address
    dns_server: Option<Ipv4Addr>,
}

impl UdpNat {
    pub fn new() -> Self {
        Self {
            flows: HashMap::new(),
            dns_server: None,
        }
    }

    /// Process a NAT'd UDP frame from the Mac.
    pub fn handle_frame(&mut self, frame: &[u8], _device: &mut SocketDevice) {
        let Ok(eth) = EthernetFrame::new_checked(frame) else { return };
        let Ok(ip) = Ipv4Packet::new_checked(eth.payload()) else { return };

        if ip.next_header() != IpProtocol::Udp {
            return;
        }

        let ip_hdr_len = ip.header_len() as usize;
        let Ok(udp) = UdpPacket::new_checked(&eth.payload()[ip_hdr_len..]) else { return };

        let src_mac = eth.src_addr();
        let src_ip = ip.src_addr();
        let dst_ip = ip.dst_addr();
        let src_port = udp.src_port();
        let dst_port = udp.dst_port();
        let hop_limit = ip.hop_limit();
        let payload = &eth.payload()[ip_hdr_len + 8..ip_hdr_len + udp.len() as usize];

        // Determine actual destination: DNS to gateway gets redirected to host DNS
        let is_dns_to_gateway = dst_port == 53 && dst_ip == GW_IP;
        let actual_dst_ip = if is_dns_to_gateway {
            let dns = self.resolve_host_dns();
            Ipv4Address(dns.octets())
        } else {
            dst_ip
        };

        let key = UdpFlowKey { src_ip, src_port, dst_ip, dst_port };

        // Look up or create flow
        if !self.flows.contains_key(&key) {
            let actual_dst_std = to_std_ip(actual_dst_ip);
            let addr = SocketAddrV4::new(actual_dst_std, dst_port);

            match UdpSocket::bind("0.0.0.0:0") {
                Ok(sock) => {
                    sock.set_nonblocking(true).unwrap();
                    if let Err(e) = sock.connect(std::net::SocketAddr::V4(addr)) {
                        log::warn!("UDP NAT: connect to {}:{} failed: {}", actual_dst_ip, dst_port, e);
                        return;
                    }
                    // Set TTL
                    let ttl = if hop_limit > 1 { hop_limit - 1 } else { 1 };
                    let _ = sock.set_ttl(ttl as u32);

                    // Enable ICMP error reporting for traceroute
                    let val: libc::c_int = 1;
                    unsafe {
                        libc::setsockopt(
                            sock.as_raw_fd(),
                            libc::SOL_IP,
                            libc::IP_RECVERR,
                            &val as *const _ as *const libc::c_void,
                            std::mem::size_of_val(&val) as libc::socklen_t,
                        );
                    }

                    log::info!("UDP NAT: new flow {}:{} -> {}:{} (actual {}:{})",
                        src_ip, src_port, dst_ip, dst_port, actual_dst_ip, dst_port);

                    self.flows.insert(key, UdpFlow {
                        host_socket: sock,
                        mac_ip: src_ip,
                        mac_addr: src_mac,
                        apparent_dst_ip: dst_ip,
                        mac_port: src_port,
                        dst_port,
                        last_activity: Instant::now(),
                    });
                }
                Err(e) => {
                    log::warn!("UDP NAT: bind failed: {}", e);
                    return;
                }
            }
        }

        let flow = self.flows.get_mut(&key).unwrap();
        flow.last_activity = Instant::now();

        // Update TTL per packet (traceroute increments TTL each round)
        let ttl = if hop_limit > 1 { hop_limit - 1 } else { 1 };
        let _ = flow.host_socket.set_ttl(ttl as u32);

        // Send payload to host
        match flow.host_socket.send(payload) {
            Ok(_) => {
                log::debug!("UDP NAT: sent {} bytes to {}:{}", payload.len(), dst_ip, dst_port);
            }
            Err(e) => {
                log::warn!("UDP NAT: send failed: {}", e);
            }
        }
    }

    /// Poll host UDP sockets for incoming data and relay to Mac.
    pub fn poll(&mut self, device: &mut SocketDevice) {
        let keys: Vec<UdpFlowKey> = self.flows.keys().copied().collect();
        let mut to_remove: Vec<UdpFlowKey> = Vec::new();

        for key in keys {
            let flow = self.flows.get_mut(&key).unwrap();

            // Check timeout
            if flow.last_activity.elapsed().as_secs() > CONN_TIMEOUT_SECS {
                log::debug!("UDP NAT: timeout {}:{}", flow.apparent_dst_ip, flow.dst_port);
                to_remove.push(key);
                continue;
            }

            // Try to receive data
            let mut buf = [0u8; 65535];
            match flow.host_socket.recv(&mut buf) {
                Ok(n) => {
                    flow.last_activity = Instant::now();
                    log::debug!("UDP NAT: recv {} bytes from {}:{}", n, flow.apparent_dst_ip, flow.dst_port);

                    let resp = build_udp_frame(
                        flow.apparent_dst_ip,
                        flow.dst_port,
                        flow.mac_ip,
                        flow.mac_port,
                        flow.mac_addr,
                        &buf[..n],
                    );
                    device.send_frame(&resp);
                }
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {}
                Err(e) => {
                    log::warn!("UDP NAT: recv error: {}", e);
                    to_remove.push(key);
                }
            }

            // Check for ICMP errors (traceroute: Time Exceeded from intermediate routers)
            if let Some(icmp_frame) = check_icmp_error(flow) {
                device.send_frame(&icmp_frame);
            }
        }

        for key in to_remove {
            self.flows.remove(&key);
        }
    }

    /// Resolve the host's real DNS server from /etc/resolv.conf.
    /// Skips localhost entries (127.0.0.53, 127.0.0.1). Caches the result.
    /// Falls back to 8.8.8.8.
    fn resolve_host_dns(&mut self) -> Ipv4Addr {
        if let Some(cached) = self.dns_server {
            return cached;
        }

        let dns = read_resolv_conf().unwrap_or(Ipv4Addr::new(8, 8, 8, 8));
        log::info!("UDP NAT: using DNS server {}", dns);
        self.dns_server = Some(dns);
        dns
    }
}

/// Read /etc/resolv.conf and find the first non-localhost nameserver.
fn read_resolv_conf() -> Option<Ipv4Addr> {
    let contents = std::fs::read_to_string("/etc/resolv.conf").ok()?;
    for line in contents.lines() {
        let line = line.trim();
        if !line.starts_with("nameserver") {
            continue;
        }
        let parts: Vec<&str> = line.split_whitespace().collect();
        if parts.len() < 2 {
            continue;
        }
        if let Ok(ip) = parts[1].parse::<Ipv4Addr>() {
            // Skip localhost entries (systemd-resolved, etc.)
            if ip == Ipv4Addr::new(127, 0, 0, 53) || ip == Ipv4Addr::new(127, 0, 0, 1) {
                continue;
            }
            return Some(ip);
        }
    }
    None
}

/// Convert smoltcp Ipv4Address to std Ipv4Addr
fn to_std_ip(ip: Ipv4Address) -> Ipv4Addr {
    let o = ip.0;
    Ipv4Addr::new(o[0], o[1], o[2], o[3])
}

/// Check MSG_ERRQUEUE for ICMP errors (Time Exceeded, Dest Unreachable).
/// Returns an ICMP frame to send to the Mac if an error was found.
fn check_icmp_error(flow: &UdpFlow) -> Option<Vec<u8>> {
    let fd = flow.host_socket.as_raw_fd();

    let mut cbuf = [0u8; 512];
    let mut data = [0u8; 1];
    let mut iov = libc::iovec {
        iov_base: data.as_mut_ptr() as *mut libc::c_void,
        iov_len: data.len(),
    };
    let mut msg: libc::msghdr = unsafe { std::mem::zeroed() };
    msg.msg_iov = &mut iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf.as_mut_ptr() as *mut libc::c_void;
    msg.msg_controllen = cbuf.len();

    let n = unsafe { libc::recvmsg(fd, &mut msg, libc::MSG_ERRQUEUE | libc::MSG_DONTWAIT) };
    if n < 0 {
        return None;
    }

    // Parse control messages for the ICMP error
    let mut cmsg = unsafe { libc::CMSG_FIRSTHDR(&msg) };
    while !cmsg.is_null() {
        let cm = unsafe { &*cmsg };
        if cm.cmsg_level == libc::SOL_IP && cm.cmsg_type == libc::IP_RECVERR {
            let ee = unsafe { &*(libc::CMSG_DATA(cmsg) as *const libc::sock_extended_err) };
            if ee.ee_origin != libc::SO_EE_ORIGIN_ICMP as u8 {
                cmsg = unsafe { libc::CMSG_NXTHDR(&msg, cmsg) };
                continue;
            }

            // Get the offending router's IP from SO_EE_OFFENDER
            let offender = unsafe {
                &*((cmsg as *const u8).add(
                    std::mem::size_of::<libc::cmsghdr>() + std::mem::size_of::<libc::sock_extended_err>()
                ) as *const libc::sockaddr_in)
            };
            let router_ip_bytes = offender.sin_addr.s_addr.to_ne_bytes();
            let router_ip = Ipv4Address::new(
                router_ip_bytes[0], router_ip_bytes[1],
                router_ip_bytes[2], router_ip_bytes[3],
            );

            log::info!("UDP NAT: ICMP error from {}: type={} code={}",
                router_ip, ee.ee_type, ee.ee_code);

            // Build ICMP Time Exceeded or Dest Unreachable
            let icmp_type = ee.ee_type;
            let icmp_code = ee.ee_code;

            // Fabricate original IP+UDP header for the ICMP payload (28 bytes)
            let mut orig_hdr = [0u8; 28];
            orig_hdr[0] = 0x45; // IPv4, IHL=5
            orig_hdr[2] = 0; orig_hdr[3] = 28; // total length
            orig_hdr[8] = 1; // TTL (was 1 when it expired)
            orig_hdr[9] = 17; // UDP
            // src = Mac IP
            orig_hdr[12..16].copy_from_slice(&flow.mac_ip.0);
            // dst = original destination
            orig_hdr[16..20].copy_from_slice(&flow.apparent_dst_ip.0);
            // UDP src port
            orig_hdr[20] = (flow.mac_port >> 8) as u8;
            orig_hdr[21] = flow.mac_port as u8;
            // UDP dst port
            orig_hdr[22] = (flow.dst_port >> 8) as u8;
            orig_hdr[23] = flow.dst_port as u8;

            // ICMP message: type(1) + code(1) + checksum(2) + unused(4) + orig_hdr(28) = 36 bytes
            let mut icmp_msg = [0u8; 36];
            icmp_msg[0] = icmp_type;
            icmp_msg[1] = icmp_code;
            // checksum at [2..3], computed below
            icmp_msg[8..36].copy_from_slice(&orig_hdr);

            // Compute ICMP checksum
            let mut cksum: u32 = 0;
            for i in (0..36).step_by(2) {
                let word = ((icmp_msg[i] as u32) << 8) | (icmp_msg[i + 1] as u32);
                cksum += word;
            }
            while cksum >> 16 != 0 {
                cksum = (cksum & 0xFFFF) + (cksum >> 16);
            }
            let cksum = !(cksum as u16);
            icmp_msg[2] = (cksum >> 8) as u8;
            icmp_msg[3] = cksum as u8;

            // Build ethernet frame: Ether + IP + ICMP
            let ip_repr = Ipv4Repr {
                src_addr: router_ip,
                dst_addr: flow.mac_ip,
                next_header: IpProtocol::Icmp,
                payload_len: 36,
                hop_limit: 64,
            };
            let eth_repr = EthernetRepr {
                src_addr: GW_MAC,
                dst_addr: flow.mac_addr,
                ethertype: EthernetProtocol::Ipv4,
            };

            let total_len = eth_repr.buffer_len() + ip_repr.buffer_len() + 36;
            let mut buf = vec![0u8; total_len];

            let mut eth_frame = smoltcp::wire::EthernetFrame::new_unchecked(&mut buf);
            eth_repr.emit(&mut eth_frame);
            let mut ip_pkt = Ipv4Packet::new_unchecked(eth_frame.payload_mut());
            ip_repr.emit(&mut ip_pkt, &smoltcp::phy::ChecksumCapabilities::default());
            ip_pkt.payload_mut()[..36].copy_from_slice(&icmp_msg);

            return Some(buf);
        }
        cmsg = unsafe { libc::CMSG_NXTHDR(&msg, cmsg) };
    }
    None
}

/// Build a complete ethernet frame containing an IPv4/UDP packet.
/// `dst_mac` is the destination guest's MAC for L2 routing.
fn build_udp_frame(
    src_ip: Ipv4Address,
    src_port: u16,
    dst_ip: Ipv4Address,
    dst_port: u16,
    dst_mac: EthernetAddress,
    payload: &[u8],
) -> Vec<u8> {
    let udp_repr = UdpRepr {
        src_port,
        dst_port,
    };

    let udp_len = udp_repr.header_len() + payload.len();

    let ip_repr = Ipv4Repr {
        src_addr: src_ip,
        dst_addr: dst_ip,
        next_header: IpProtocol::Udp,
        payload_len: udp_len,
        hop_limit: 64,
    };

    let eth_repr = EthernetRepr {
        src_addr: GW_MAC,
        dst_addr: dst_mac,
        ethertype: EthernetProtocol::Ipv4,
    };

    let total_len = eth_repr.buffer_len() + ip_repr.buffer_len() + udp_len;
    let mut buf = vec![0u8; total_len];

    let mut eth_frame = EthernetFrame::new_unchecked(&mut buf);
    eth_repr.emit(&mut eth_frame);

    let mut ip_pkt = Ipv4Packet::new_unchecked(eth_frame.payload_mut());
    ip_repr.emit(&mut ip_pkt, &smoltcp::phy::ChecksumCapabilities::default());

    let mut udp_pkt = UdpPacket::new_unchecked(ip_pkt.payload_mut());
    udp_repr.emit(
        &mut udp_pkt,
        &src_ip.into(),
        &dst_ip.into(),
        payload.len(),
        |buf| buf.copy_from_slice(payload),
        &smoltcp::phy::ChecksumCapabilities::default(),
    );

    buf
}
