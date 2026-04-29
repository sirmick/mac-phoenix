//! Multi-port L2 switch over Unix sockets.
//!
//! Each connected guest gets a `Port`. Frames are classified per-tick:
//! - dst MAC matches another guest → forwarded directly (inter-guest)
//! - dst MAC is broadcast / AppleTalk multicast → flooded to other guests,
//!   AND queued for local handling (ARP/DHCP/NAT)
//! - dst MAC == GW MAC → queued for local handling only (smoltcp + NAT)
//!
//! Wire format on each Unix socket: 4-byte big-endian length prefix
//! followed by the raw ethernet frame.

use std::io::{Read, Write};
use std::os::unix::net::UnixStream;

use smoltcp::phy::{self, Device, DeviceCapabilities, Medium};
use smoltcp::time::Instant;
use smoltcp::wire::{
    EthernetAddress, EthernetFrame, EthernetProtocol, IpProtocol,
    Ipv4Address, Ipv4Packet, UdpPacket,
};

/// Maximum ethernet frame size
const MTU: usize = 1514;
/// Frame header: 4-byte big-endian length prefix
const HEADER_LEN: usize = 4;
/// Gateway MAC — what the bridge looks like to guests.
pub const GW_MAC: EthernetAddress = EthernetAddress([0x02, 0x50, 0x48, 0x58, 0x00, 0x02]);
/// Gateway IP (10.0.2.1) — for classifying frames as gateway-bound vs external.
pub const GW_IP: Ipv4Address = Ipv4Address::new(10, 0, 2, 1);

/// One connected guest on the virtual switch.
struct Port {
    stream: UnixStream,
    rx_buf: Vec<u8>,
    /// Source MAC learned from the first egress frame this guest sent.
    /// Used as the unicast address when other guests / the gateway
    /// address frames to this guest.
    learned_mac: Option<EthernetAddress>,
    /// IP assigned via DHCP. Filled in by `dhcp_server` once a lease is
    /// granted. Used by NAT to look up "which port owns this IP".
    assigned_ip: Option<Ipv4Address>,
    /// Frames buffered for transmit (bridge → guest). Drained by `flush_tx`.
    tx: Vec<Vec<u8>>,
    /// Index used for log messages (1-based).
    id: usize,
    /// Per-port frame counter (just for log throttling).
    rx_count: u64,
}

impl Port {
    fn new(stream: UnixStream, id: usize) -> Self {
        Self {
            stream,
            rx_buf: Vec::with_capacity(4096),
            learned_mac: None,
            assigned_ip: None,
            tx: Vec::new(),
            id,
            rx_count: 0,
        }
    }
}

/// Multi-port L2 switch + smoltcp Device adapter.
///
/// `nat_frames` and `rx_frames` are populated by `drain_sockets` from any
/// port's incoming traffic. The smoltcp Device trait reads from
/// `rx_frames` (one shared queue is fine because smoltcp's IP is the
/// single gateway IP, not per-guest). Outgoing frames produced by smoltcp
/// or the NAT layers go through `send_frame`, which routes by dst MAC.
pub struct SocketDevice {
    ports: Vec<Port>,
    /// Frames for smoltcp (ARP-to-GW, ICMP-echo-to-GW, broadcast ARP).
    rx_frames: Vec<Vec<u8>>,
    /// Frames for NAT (TCP/UDP/ICMP to non-gateway IPs, DHCP).
    /// The accompanying source MAC tells NAT which port to send replies
    /// to (NAT keys flows by src_ip but DHCP needs the chaddr/src_mac).
    pub nat_frames: Vec<Vec<u8>>,
    next_port_id: usize,
}

impl SocketDevice {
    pub fn new() -> Self {
        Self {
            ports: Vec::new(),
            rx_frames: Vec::new(),
            nat_frames: Vec::new(),
            next_port_id: 1,
        }
    }

    /// Attach a freshly-accepted UnixStream (already set non-blocking).
    pub fn add_port(&mut self, stream: UnixStream) {
        let id = self.next_port_id;
        self.next_port_id += 1;
        log::info!("port#{}: client connected ({} total)", id, self.ports.len() + 1);
        self.ports.push(Port::new(stream, id));
    }

    /// DHCP calls this after granting a lease so the switch knows which
    /// port owns which IP (used for log readability — actual frame
    /// routing always goes through dst-MAC matching).
    pub fn record_lease(&mut self, mac: EthernetAddress, ip: Ipv4Address) {
        if let Some(p) = self.ports.iter_mut().find(|p| p.learned_mac == Some(mac)) {
            p.assigned_ip = Some(ip);
            log::info!("port#{}: lease {} → {}", p.id, mac, ip);
        }
    }

    /// Read available data from every port; classify learned frames into
    /// the smoltcp / NAT / inter-guest queues. Disconnected ports are
    /// dropped from the table.
    pub fn drain_sockets(&mut self) {
        let mut tmp = [0u8; 8192];
        let mut to_drop: Vec<usize> = Vec::new();

        // Phase 1: read raw bytes per port.
        for port in &mut self.ports {
            loop {
                match port.stream.read(&mut tmp) {
                    Ok(0) => {
                        log::info!("port#{}: client disconnected", port.id);
                        to_drop.push(port.id);
                        break;
                    }
                    Ok(n) => port.rx_buf.extend_from_slice(&tmp[..n]),
                    Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                    Err(e) => {
                        log::warn!("port#{}: read error: {}", port.id, e);
                        to_drop.push(port.id);
                        break;
                    }
                }
            }
        }

        // Phase 2: parse complete frames out of each port's buffer.
        // We pull frames into a temporary list with their source port id,
        // then in phase 3 classify and route. (Two-phase because routing
        // needs `&mut self.ports` while we no longer hold a borrow on a
        // single port.)
        let mut decoded: Vec<(usize, Vec<u8>)> = Vec::new();
        for port in &mut self.ports {
            while port.rx_buf.len() >= HEADER_LEN {
                let frame_len = u32::from_be_bytes([
                    port.rx_buf[0], port.rx_buf[1], port.rx_buf[2], port.rx_buf[3],
                ]) as usize;

                if frame_len == 0 || frame_len > MTU {
                    log::warn!("port#{}: invalid frame length: {}", port.id, frame_len);
                    port.rx_buf.clear();
                    break;
                }

                if port.rx_buf.len() < HEADER_LEN + frame_len {
                    break;
                }

                let frame = port.rx_buf[HEADER_LEN..HEADER_LEN + frame_len].to_vec();
                port.rx_buf.drain(..HEADER_LEN + frame_len);

                port.rx_count += 1;
                if port.rx_count <= 8 {
                    log_frame_summary(port.id, port.rx_count, &frame);
                }

                // Learn source MAC on this port from the first frame.
                if port.learned_mac.is_none() {
                    if let Ok(eth) = EthernetFrame::new_checked(&frame) {
                        let src = eth.src_addr();
                        // Sanity: don't learn broadcast/multicast as a port MAC.
                        if !is_multicast_or_broadcast(src) {
                            log::info!("port#{}: learned MAC {}", port.id, src);
                            port.learned_mac = Some(src);
                        }
                    }
                }

                decoded.push((port.id, frame));
            }
        }

        // Phase 3: classify + route each decoded frame.
        for (src_port_id, frame) in decoded {
            self.dispatch_frame(src_port_id, frame);
        }

        // Phase 4: prune dead ports.
        if !to_drop.is_empty() {
            self.ports.retain(|p| !to_drop.contains(&p.id));
        }
    }

    /// Decide where a frame from `src_port_id` goes:
    /// (a) directly to another port whose learned MAC matches dst,
    /// (b) flooded to other ports + queued locally (broadcast/multicast),
    /// (c) queued locally only (unicast to gateway).
    fn dispatch_frame(&mut self, src_port_id: usize, frame: Vec<u8>) {
        let Ok(eth) = EthernetFrame::new_checked(&frame) else {
            return;
        };
        let dst = eth.dst_addr();

        // Unicast to gateway → bridge handles it (smoltcp/NAT/DHCP).
        if dst == GW_MAC {
            self.classify_local(frame);
            return;
        }

        // Broadcast / multicast → flood to other ports AND give to local
        // handlers. Broadcast covers ARP / DHCP. AppleTalk multicast
        // (09:00:07:*) and IPv4 multicast (01:00:5e:*) only matter to
        // peer guests; the bridge ignores them locally but flooding is
        // harmless (smoltcp will reject what it doesn't recognise).
        if is_multicast_or_broadcast(dst) {
            self.flood_to_other_ports(src_port_id, &frame);
            self.classify_local(frame);
            return;
        }

        // Unicast to another guest's MAC → direct deliver, do NOT touch
        // the NAT / smoltcp layers. This is what makes intra-segment
        // IP-between-guests AND AppleTalk DDP between guests work.
        if let Some(port) = self.ports.iter_mut().find(|p| p.learned_mac == Some(dst)) {
            if port.id != src_port_id {
                port.tx.push(frame);
                return;
            }
        }

        // Unknown unicast — flood (standard learning-bridge behaviour).
        // This handles the brief window before we've learned every guest's
        // MAC. Once everyone's spoken, this path goes cold.
        self.flood_to_other_ports(src_port_id, &frame);
    }

    /// Push `frame` into every port's tx queue except the one it came from.
    fn flood_to_other_ports(&mut self, except_id: usize, frame: &[u8]) {
        for p in &mut self.ports {
            if p.id != except_id {
                p.tx.push(frame.to_vec());
            }
        }
    }

    /// Classify a gateway-bound or broadcast frame into the smoltcp queue
    /// vs the NAT queue (DHCP / external IP traffic).
    fn classify_local(&mut self, frame: Vec<u8>) {
        if is_nat_frame(&frame) {
            self.nat_frames.push(frame);
        } else {
            self.rx_frames.push(frame);
        }
    }

    /// Send a frame produced by smoltcp or a NAT layer. Routes by dst MAC.
    /// The NAT layers always set dst MAC to the originating guest's MAC
    /// (the one we learned at port-attach time), so this resolves cleanly.
    pub fn send_frame(&mut self, frame: &[u8]) {
        let Ok(eth) = EthernetFrame::new_checked(frame) else {
            log::warn!("send_frame: malformed");
            return;
        };
        let dst = eth.dst_addr();

        if is_multicast_or_broadcast(dst) {
            for p in &mut self.ports {
                p.tx.push(frame.to_vec());
            }
            return;
        }

        if let Some(p) = self.ports.iter_mut().find(|p| p.learned_mac == Some(dst)) {
            p.tx.push(frame.to_vec());
            return;
        }

        log::warn!("send_frame: no port for dst MAC {}", dst);
    }

    /// Flush every port's tx queue to its socket.
    pub fn flush_tx(&mut self) {
        let mut to_drop: Vec<usize> = Vec::new();
        for port in &mut self.ports {
            for frame in port.tx.drain(..) {
                let len = frame.len() as u32;
                let header = len.to_be_bytes();
                if port.stream.write_all(&header).is_err()
                    || port.stream.write_all(&frame).is_err()
                {
                    log::info!("port#{}: write failed, dropping", port.id);
                    to_drop.push(port.id);
                    break;
                }
            }
        }
        if !to_drop.is_empty() {
            self.ports.retain(|p| !to_drop.contains(&p.id));
        }
    }

    /// Number of currently-connected ports. Mostly for logs.
    pub fn port_count(&self) -> usize {
        self.ports.len()
    }
}

fn is_multicast_or_broadcast(mac: EthernetAddress) -> bool {
    // The least-significant bit of the first octet is the I/G bit:
    // 1 = group address (multicast OR broadcast). Covers ff:ff:ff:ff:ff:ff,
    // IPv4 multicast (01:00:5e:*), and AppleTalk multicast (09:00:07:*).
    (mac.0[0] & 0x01) != 0
}

/// Should this frame go to NAT instead of smoltcp?
fn is_nat_frame(frame: &[u8]) -> bool {
    if frame.len() < 14 {
        return false;
    }
    let Ok(eth) = EthernetFrame::new_checked(frame) else { return false };
    if eth.ethertype() != EthernetProtocol::Ipv4 {
        return false;
    }
    let Ok(ip) = Ipv4Packet::new_checked(eth.payload()) else { return false };

    let dst_ip = ip.dst_addr();

    // Gateway-bound: smoltcp handles ping etc.; DNS (UDP/53) goes to NAT.
    if dst_ip == GW_IP {
        if ip.next_header() == IpProtocol::Udp {
            let ip_hdr_len = ip.header_len() as usize;
            if let Ok(udp) = UdpPacket::new_checked(&eth.payload()[ip_hdr_len..]) {
                if udp.dst_port() == 53 {
                    return true;
                }
            }
        }
        return false;
    }

    // Broadcast: catch DHCP (UDP/67), let smoltcp ignore the rest.
    if dst_ip.0[0] >= 224 || dst_ip == Ipv4Address::BROADCAST {
        if ip.next_header() == IpProtocol::Udp {
            let ip_hdr_len = ip.header_len() as usize;
            if let Ok(udp) = UdpPacket::new_checked(&eth.payload()[ip_hdr_len..]) {
                if udp.dst_port() == 67 {
                    return true;
                }
            }
        }
        return false;
    }

    // External IP: route to NAT.
    matches!(
        ip.next_header(),
        IpProtocol::Tcp | IpProtocol::Udp | IpProtocol::Icmp
    )
}

fn log_frame_summary(port_id: usize, n: u64, frame: &[u8]) {
    let Ok(eth) = EthernetFrame::new_checked(frame) else {
        log::info!("port#{port_id} rx#{n} len={} [malformed]", frame.len());
        return;
    };
    let src = eth.src_addr();
    let dst = eth.dst_addr();
    match eth.ethertype() {
        EthernetProtocol::Arp => log::info!("port#{port_id} rx#{n} ARP src={src} dst={dst}"),
        EthernetProtocol::Ipv4 => {
            let Ok(ip) = Ipv4Packet::new_checked(eth.payload()) else {
                log::info!("port#{port_id} rx#{n} IPv4 [malformed]"); return;
            };
            let proto = ip.next_header();
            let sip = ip.src_addr();
            let dip = ip.dst_addr();
            if proto == IpProtocol::Udp {
                let ip_hdr_len = ip.header_len() as usize;
                if let Ok(udp) = UdpPacket::new_checked(&eth.payload()[ip_hdr_len..]) {
                    log::info!("port#{port_id} rx#{n} UDP {sip}:{} -> {dip}:{}",
                        udp.src_port(), udp.dst_port());
                    return;
                }
            }
            log::info!("port#{port_id} rx#{n} IPv4 {sip} -> {dip} proto={proto}");
        }
        // Show ethertype as hex for AppleTalk (0x809b/0x80f3) and friends.
        other => log::info!("port#{port_id} rx#{n} ethertype=0x{:04x} src={src} dst={dst}",
                            u16::from(other)),
    }
}

// --- smoltcp Device trait ---

impl Device for SocketDevice {
    type RxToken<'a> = RxToken;
    type TxToken<'a> = TxToken<'a>;

    fn receive(&mut self, _t: Instant) -> Option<(Self::RxToken<'_>, Self::TxToken<'_>)> {
        if self.rx_frames.is_empty() {
            return None;
        }
        let frame = self.rx_frames.remove(0);
        Some((
            RxToken { buffer: frame },
            TxToken { device: self },
        ))
    }

    fn transmit(&mut self, _t: Instant) -> Option<Self::TxToken<'_>> {
        Some(TxToken { device: self })
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
    device: &'a mut SocketDevice,
}

impl<'a> phy::TxToken for TxToken<'a> {
    fn consume<R, F>(self, len: usize, f: F) -> R
    where
        F: FnOnce(&mut [u8]) -> R,
    {
        let mut buffer = vec![0u8; len];
        let result = f(&mut buffer);
        self.device.send_frame(&buffer);
        result
    }
}
