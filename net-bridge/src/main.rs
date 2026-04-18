//! net-bridge: Standalone network bridge for Mac emulator
//!
//! Accepts ethernet frames over a Unix socket, processes them through
//! smoltcp (userspace TCP/IP stack) for ARP/ICMP, and handles TCP/UDP
//! NAT to the host network.
//!
//! Usage: net-bridge [--socket /tmp/mac-ether.sock]

mod device;
mod dhcp_server;
mod echo_server;
mod icmp_proxy;
mod nat;
mod tcp_proxy;
mod udp_proxy;

use std::os::unix::net::UnixListener;
use std::path::PathBuf;
use std::time::Instant;

use smoltcp::iface::{Config, Interface, SocketSet};
use smoltcp::wire::{EthernetAddress, HardwareAddress, IpAddress, IpCidr, Ipv4Address};

use device::SocketDevice;
use echo_server::EchoServer;
use icmp_proxy::IcmpNat;
use tcp_proxy::TcpNat;
use udp_proxy::UdpNat;

/// Gateway (bridge) MAC and IP
const GW_MAC: [u8; 6] = [0x02, 0x50, 0x48, 0x58, 0x00, 0x02];
const GW_IP: Ipv4Address = Ipv4Address::new(10, 0, 2, 1);
const NETMASK: u8 = 24;

fn main() {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info"))
        .format_timestamp_millis()
        .init();

    let sock_path = std::env::args()
        .skip_while(|a| a != "--socket")
        .nth(1)
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("/tmp/mac-ether.sock"));

    // Clean up stale socket
    let _ = std::fs::remove_file(&sock_path);

    let listener = UnixListener::bind(&sock_path).expect("bind Unix socket");
    log::info!("Listening on {}", sock_path.display());

    let (stream, _) = listener.accept().expect("accept");
    stream.set_nonblocking(true).expect("set nonblocking");
    log::info!("Client connected");

    let mut device = SocketDevice::new(stream);

    // Configure smoltcp: handles ARP, ICMP echo for gateway
    let hw_addr = HardwareAddress::Ethernet(EthernetAddress(GW_MAC));
    let mut config = Config::new(hw_addr);
    config.random_seed = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_nanos() as u64;

    let mut iface = Interface::new(config, &mut device, smoltcp::time::Instant::now());
    iface.update_ip_addrs(|addrs| {
        addrs
            .push(IpCidr::new(IpAddress::Ipv4(GW_IP), NETMASK))
            .unwrap();
    });

    let mut sockets = SocketSet::new(vec![]);
    let echo = EchoServer::new(&mut sockets);
    let mut tcp_nat = TcpNat::new();
    let mut udp_nat = UdpNat::new();
    let mut icmp_nat = IcmpNat::new();

    let startup = Instant::now();
    log::info!("Bridge ready: gateway 10.0.2.1/24 (echo on :7 TCP+UDP)");

    // Main poll loop
    loop {
        let timestamp = smoltcp::time::Instant::from_millis(
            startup.elapsed().as_millis() as i64,
        );

        // 1. Read frames from Unix socket, classify into smoltcp vs NAT queues
        device.drain_socket();

        // 2. Process NAT frames (TCP/UDP/ICMP to external IPs)
        let nat_frames: Vec<Vec<u8>> = device.nat_frames.drain(..).collect();
        for frame in &nat_frames {
            // DHCP first (broadcast UDP port 67)
            if dhcp_server::handle_dhcp_frame(frame, &mut device) {
                continue;
            }
            // TTL check: generate Time Exceeded and skip forwarding
            if icmp_proxy::check_ttl_exceeded(frame, &mut device) {
                continue;
            }
            tcp_nat.handle_frame(frame, &mut device);
            udp_nat.handle_frame(frame, &mut device);
            icmp_nat.handle_frame(frame, &mut device);
        }

        // 3. Let smoltcp process its frames (ARP, ICMP echo, etc.)
        let _result = iface.poll(timestamp, &mut device, &mut sockets);

        // 3b. Service the in-bridge echo daemon on GW:7
        echo.poll(&mut sockets);

        // Re-poll smoltcp so any outgoing echo frames get egressed this tick.
        let _ = iface.poll(timestamp, &mut device, &mut sockets);

        // 4. Poll host sockets for incoming data → relay to Mac
        tcp_nat.poll(&mut device);
        udp_nat.poll(&mut device);
        icmp_nat.poll(&mut device);

        // 5. Flush all outgoing frames to the client
        device.flush_tx();

        std::thread::sleep(std::time::Duration::from_millis(1));
    }
}
