//! Minimal DHCP server for the NAT gateway with a per-MAC lease pool.
//!
//! Hands out 10.0.2.15 .. 10.0.2.250 from a small pool keyed by client
//! chaddr (the guest's ethernet MAC). Once a MAC has a lease, the same
//! IP is returned for renewals — no rotation, no expiry. Suitable for
//! the handful of guests this bridge ever sees.

use std::collections::HashMap;

use smoltcp::wire::{
    EthernetAddress, EthernetFrame, EthernetProtocol, EthernetRepr,
    IpProtocol, Ipv4Address, Ipv4Packet, Ipv4Repr,
    UdpPacket, UdpRepr,
};

use crate::device::{SocketDevice, GW_IP, GW_MAC};

const SUBNET_MASK: [u8; 4] = [255, 255, 255, 0];
const LEASE_TIME: u32 = 86400; // 1 day

// Lease pool: 10.0.2.15 through 10.0.2.250 inclusive.
const POOL_FIRST: u8 = 15;
const POOL_LAST: u8 = 250;

// DHCP message types
const DHCP_DISCOVER: u8 = 1;
const DHCP_OFFER: u8 = 2;
const DHCP_REQUEST: u8 = 3;
const DHCP_ACK: u8 = 5;

// BOOTP constants
const BOOTP_REQUEST: u8 = 1;
const BOOTP_REPLY: u8 = 2;
const BOOTP_MIN_LEN: usize = 236;
const MAGIC_COOKIE: [u8; 4] = [99, 130, 83, 99];

/// Per-MAC lease state. One instance lives in `main.rs` and is threaded
/// through `handle_dhcp_frame` so leases persist across DISCOVER →
/// REQUEST → renewal.
pub struct LeasePool {
    leases: HashMap<EthernetAddress, Ipv4Address>,
}

impl LeasePool {
    pub fn new() -> Self {
        Self { leases: HashMap::new() }
    }

    /// Return this MAC's lease, allocating a new IP from the pool if
    /// needed. Returns None only if the pool is fully exhausted.
    pub fn lease_for(&mut self, mac: EthernetAddress) -> Option<Ipv4Address> {
        if let Some(&ip) = self.leases.get(&mac) {
            return Some(ip);
        }
        // Find lowest free octet.
        let used: std::collections::HashSet<u8> =
            self.leases.values().map(|ip| ip.0[3]).collect();
        for octet in POOL_FIRST..=POOL_LAST {
            if !used.contains(&octet) {
                let ip = Ipv4Address::new(10, 0, 2, octet);
                self.leases.insert(mac, ip);
                log::info!("DHCP: new lease {} -> {}", mac, ip);
                return Some(ip);
            }
        }
        log::warn!("DHCP: lease pool exhausted (256 entries)");
        None
    }
}

/// Check if a raw ethernet frame is a DHCP request and handle it.
/// Returns true if the frame was consumed (was DHCP).
pub fn handle_dhcp_frame(
    frame: &[u8],
    device: &mut SocketDevice,
    pool: &mut LeasePool,
) -> bool {
    let Ok(eth) = EthernetFrame::new_checked(frame) else { return false };

    if eth.ethertype() != EthernetProtocol::Ipv4 {
        return false;
    }

    let Ok(ip) = Ipv4Packet::new_checked(eth.payload()) else { return false };

    if ip.next_header() != IpProtocol::Udp {
        return false;
    }

    let ip_hdr_len = ip.header_len() as usize;
    let Ok(udp) = UdpPacket::new_checked(&eth.payload()[ip_hdr_len..]) else { return false };

    // DHCP: client port 68, server port 67
    if udp.src_port() != 68 || udp.dst_port() != 67 {
        return false;
    }

    let udp_payload_offset = ip_hdr_len + 8;
    let udp_payload_len = udp.len() as usize - 8;
    if eth.payload().len() < udp_payload_offset + udp_payload_len {
        return false;
    }
    let bootp = &eth.payload()[udp_payload_offset..udp_payload_offset + udp_payload_len];

    if bootp.len() < BOOTP_MIN_LEN + 4 {
        return false;
    }

    if bootp[0] != BOOTP_REQUEST {
        return false;
    }

    if bootp[236..240] != MAGIC_COOKIE {
        return false;
    }

    let xid = [bootp[4], bootp[5], bootp[6], bootp[7]];

    // BOOTP flags (offset 10-11). Bit 0 of the high byte = broadcast flag:
    // the client is asking us to broadcast the reply because it has no IP
    // yet and its IP stack may not accept unicast before configuration
    // (classic Open Transport requires this; MacTCP is lax about it).
    let broadcast_requested = (bootp[10] & 0x80) != 0;

    // Extract client MAC (chaddr at offset 28, 6 bytes for ethernet)
    let mut client_mac = [0u8; 6];
    client_mac.copy_from_slice(&bootp[28..34]);
    let client_mac_addr = EthernetAddress(client_mac);

    let msg_type = find_dhcp_option(&bootp[240..], 53)
        .and_then(|data| data.first().copied());

    let msg_type = match msg_type {
        Some(DHCP_DISCOVER) | Some(DHCP_REQUEST) => msg_type.unwrap(),
        _ => return false,
    };

    let reply_type = if msg_type == DHCP_DISCOVER { DHCP_OFFER } else { DHCP_ACK };

    let client_ip = match pool.lease_for(client_mac_addr) {
        Some(ip) => ip,
        None => {
            // Out of leases — drop silently rather than NAK; the guest
            // will retry and a freed entry might appear later.
            return true;
        }
    };

    // Tell the device which port owns this IP so NAT replies to flows
    // initiated from this guest go to the right port.
    device.record_lease(client_mac_addr, client_ip);

    log::info!(
        "DHCP: {} from {} -> {} {} ({})",
        if msg_type == DHCP_DISCOVER { "DISCOVER" } else { "REQUEST" },
        client_mac_addr,
        if reply_type == DHCP_OFFER { "OFFER" } else { "ACK" },
        client_ip,
        if broadcast_requested { "bcast" } else { "ucast" },
    );

    let dhcp_payload = build_dhcp_reply(reply_type, &xid, &client_mac, client_ip);
    let reply_frame = build_dhcp_frame(&dhcp_payload, &client_mac, client_ip, broadcast_requested);
    device.send_frame(&reply_frame);

    true
}

/// Build BOOTP/DHCP reply payload.
fn build_dhcp_reply(
    msg_type: u8,
    xid: &[u8; 4],
    client_mac: &[u8; 6],
    client_ip: Ipv4Address,
) -> Vec<u8> {
    let mut reply = vec![0u8; 300];

    reply[0] = BOOTP_REPLY;
    reply[1] = 1; // htype: ethernet
    reply[2] = 6; // hlen
    reply[3] = 0;

    reply[4..8].copy_from_slice(xid);

    // yiaddr: offered IP
    reply[16..20].copy_from_slice(&client_ip.0);

    // siaddr: server IP (gateway)
    reply[20..24].copy_from_slice(&GW_IP.0);

    // chaddr
    reply[28..34].copy_from_slice(client_mac);

    // Magic cookie
    reply[236..240].copy_from_slice(&MAGIC_COOKIE);

    let mut pos = 240;

    // Option 53: DHCP Message Type
    reply[pos] = 53; reply[pos + 1] = 1; reply[pos + 2] = msg_type;
    pos += 3;

    // Option 54: Server Identifier
    reply[pos] = 54; reply[pos + 1] = 4;
    reply[pos + 2..pos + 6].copy_from_slice(&GW_IP.0);
    pos += 6;

    // Option 51: Lease Time
    reply[pos] = 51; reply[pos + 1] = 4;
    reply[pos + 2..pos + 6].copy_from_slice(&LEASE_TIME.to_be_bytes());
    pos += 6;

    // Option 1: Subnet Mask
    reply[pos] = 1; reply[pos + 1] = 4;
    reply[pos + 2..pos + 6].copy_from_slice(&SUBNET_MASK);
    pos += 6;

    // Option 3: Router (Gateway)
    reply[pos] = 3; reply[pos + 1] = 4;
    reply[pos + 2..pos + 6].copy_from_slice(&GW_IP.0);
    pos += 6;

    // Option 6: DNS Server
    reply[pos] = 6; reply[pos + 1] = 4;
    reply[pos + 2..pos + 6].copy_from_slice(&GW_IP.0);
    pos += 6;

    // Option 255: End
    reply[pos] = 255;
    pos += 1;

    reply.truncate(pos);
    reply
}

/// Build the complete ethernet frame for a DHCP reply.
fn build_dhcp_frame(
    dhcp_payload: &[u8],
    client_mac: &[u8; 6],
    client_ip: Ipv4Address,
    broadcast: bool,
) -> Vec<u8> {
    let udp_repr = UdpRepr {
        src_port: 67,
        dst_port: 68,
    };

    let udp_len = udp_repr.header_len() + dhcp_payload.len();

    let (dst_ip, dst_mac) = if broadcast {
        (Ipv4Address::BROADCAST, EthernetAddress([0xff; 6]))
    } else {
        // Unicast reply: dst is the offered IP, addressed to chaddr.
        (client_ip, EthernetAddress(*client_mac))
    };

    let ip_repr = Ipv4Repr {
        src_addr: GW_IP,
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
        &GW_IP.into(),
        &dst_ip.into(),
        dhcp_payload.len(),
        |buf| buf.copy_from_slice(dhcp_payload),
        &smoltcp::phy::ChecksumCapabilities::default(),
    );

    buf
}

/// Find a DHCP option by tag in the options section.
fn find_dhcp_option(options: &[u8], tag: u8) -> Option<&[u8]> {
    let mut pos = 0;
    while pos < options.len() {
        let opt = options[pos];
        if opt == 255 { break; }
        if opt == 0 { pos += 1; continue; }
        if pos + 1 >= options.len() { break; }
        let len = options[pos + 1] as usize;
        if pos + 2 + len > options.len() { break; }
        if opt == tag {
            return Some(&options[pos + 2..pos + 2 + len]);
        }
        pos += 2 + len;
    }
    None
}
