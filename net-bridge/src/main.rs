//! net-bridge: Standalone network bridge for Mac emulator
//!
//! Accepts ethernet frames over a Unix socket, processes them through
//! smoltcp (userspace TCP/IP stack) for ARP/ICMP, and handles TCP/UDP
//! NAT to the host network. Multiple guests can connect to the same
//! socket — the bridge L2-switches non-IP traffic between them so two
//! emulators can ping each other directly and AppleTalk DDP/AARP frames
//! flow guest-to-guest unmolested.
//!
//! Usage: net-bridge [--socket /tmp/mac-ether.sock]

mod bulk_server;
mod device;
mod dhcp_server;
mod echo_server;
mod icmp_proxy;
mod tcp_proxy;
mod udp_proxy;

use std::os::unix::net::UnixListener;
use std::path::PathBuf;
use std::time::Instant;

use smoltcp::iface::{Config, Interface, SocketSet};
use smoltcp::wire::{HardwareAddress, IpAddress, IpCidr, Ipv4Address};

use bulk_server::BulkServer;
use device::{SocketDevice, GW_MAC};
use dhcp_server::LeasePool;
use echo_server::EchoServer;
use icmp_proxy::IcmpNat;
use tcp_proxy::TcpNat;
use udp_proxy::UdpNat;

/// Gateway IP and netmask (constants used here only for smoltcp's iface;
/// the canonical GW_IP lives in `device.rs`).
const GW_IP: Ipv4Address = Ipv4Address::new(10, 0, 2, 1);
const NETMASK: u8 = 24;

const NET_BRIDGE_BUILD_DATE: &str = env!("NET_BRIDGE_BUILD_DATE");

fn main() {
    let argv: Vec<String> = std::env::args().collect();

    if argv.iter().any(|a| a == "--version" || a == "-V") {
        println!("net-bridge {} built {}",
                 env!("CARGO_PKG_VERSION"),
                 NET_BRIDGE_BUILD_DATE);
        return;
    }

    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info"))
        .format_timestamp_millis()
        .init();

    log::info!("net-bridge {} built {}",
               env!("CARGO_PKG_VERSION"),
               NET_BRIDGE_BUILD_DATE);

    let sock_path = argv
        .iter()
        .skip_while(|a| a.as_str() != "--socket")
        .nth(1)
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("/tmp/mac-ether.sock"));

    // Clean up stale socket
    let _ = std::fs::remove_file(&sock_path);

    let listener = UnixListener::bind(&sock_path).expect("bind Unix socket");
    listener.set_nonblocking(true).expect("listener set_nonblocking");
    log::info!("Listening on {} (multi-guest)", sock_path.display());

    let mut device = SocketDevice::new();

    // Configure smoltcp: handles ARP, ICMP echo for the gateway. One
    // shared instance serves all guests because smoltcp only owns the
    // gateway IP — guest ARPs for peer IPs flood across the L2 switch
    // and are answered by the target guest, not by smoltcp.
    let hw_addr = HardwareAddress::Ethernet(GW_MAC);
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
    let mut bulk = BulkServer::new(&mut sockets);
    let mut tcp_nat = TcpNat::new();
    let mut udp_nat = UdpNat::new();
    let mut icmp_nat = IcmpNat::new();
    let mut dhcp = LeasePool::new();

    let startup = Instant::now();
    log::info!("Bridge ready: gateway 10.0.2.1/24, lease pool 10.0.2.15..250");

    // Main poll loop
    loop {
        let timestamp = smoltcp::time::Instant::from_millis(
            startup.elapsed().as_millis() as i64,
        );

        // 0. Accept any new guest connections.
        loop {
            match listener.accept() {
                Ok((stream, _)) => {
                    let _ = stream.set_nonblocking(true);
                    device.add_port(stream);
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                Err(e) => {
                    log::warn!("accept error: {}", e);
                    break;
                }
            }
        }

        // 1. Read frames from every connected guest, classify into
        //    smoltcp / NAT / inter-guest queues.
        device.drain_sockets();

        // 2. Process NAT frames (TCP/UDP/ICMP to external IPs, plus DHCP).
        let nat_frames: Vec<Vec<u8>> = device.nat_frames.drain(..).collect();
        for frame in &nat_frames {
            if dhcp_server::handle_dhcp_frame(frame, &mut device, &mut dhcp) {
                continue;
            }
            if icmp_proxy::check_ttl_exceeded(frame, &mut device) {
                continue;
            }
            tcp_nat.handle_frame(frame, &mut device);
            udp_nat.handle_frame(frame, &mut device);
            icmp_nat.handle_frame(frame, &mut device);
        }

        // 3. Let smoltcp process its frames (ARP, ICMP echo, etc.)
        let _ = iface.poll(timestamp, &mut device, &mut sockets);

        // 3b. Service the in-bridge diagnostic servers.
        echo.poll(&mut sockets);
        bulk.poll(&mut sockets);

        // Re-poll smoltcp so any outgoing frames egress this tick.
        let _ = iface.poll(timestamp, &mut device, &mut sockets);

        // 4. Poll host sockets for incoming data → relay to right guest.
        tcp_nat.poll(&mut device);
        udp_nat.poll(&mut device);
        icmp_nat.poll(&mut device);

        // 5. Flush every port's tx queue.
        device.flush_tx();

        // Idle guard: if no guests, sleep a bit longer to avoid busy-spin.
        let sleep_ms = if device.port_count() == 0 { 20 } else { 1 };
        std::thread::sleep(std::time::Duration::from_millis(sleep_ms));
    }
}

