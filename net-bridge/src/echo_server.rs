//! In-bridge echo service on the gateway IP, port 7 (TCP and UDP).
//!
//! Lets guest network tests round-trip data without depending on a host
//! `inetd`-style daemon. Sockets are owned by smoltcp, so the gateway port
//! never touches the real host network.

use smoltcp::iface::{SocketHandle, SocketSet};
use smoltcp::socket::{tcp, udp};
use smoltcp::wire::IpEndpoint;

const ECHO_PORT: u16 = 7;
const TCP_RX_BUF: usize = 2048;
const TCP_TX_BUF: usize = 2048;
const UDP_PAYLOAD_BUF: usize = 1500;
const UDP_META_SLOTS: usize = 8;

pub struct EchoServer {
    udp: SocketHandle,
    tcp: SocketHandle,
}

impl EchoServer {
    pub fn new<'a>(sockets: &mut SocketSet<'a>) -> Self {
        let udp_rx = udp::PacketBuffer::new(
            vec![udp::PacketMetadata::EMPTY; UDP_META_SLOTS],
            vec![0u8; UDP_PAYLOAD_BUF],
        );
        let udp_tx = udp::PacketBuffer::new(
            vec![udp::PacketMetadata::EMPTY; UDP_META_SLOTS],
            vec![0u8; UDP_PAYLOAD_BUF],
        );
        let mut udp_sock = udp::Socket::new(udp_rx, udp_tx);
        udp_sock.bind(ECHO_PORT).expect("udp bind :7");

        // Single-connection TCP listener; re-armed in poll() after close.
        let tcp_rx = tcp::SocketBuffer::new(vec![0u8; TCP_RX_BUF]);
        let tcp_tx = tcp::SocketBuffer::new(vec![0u8; TCP_TX_BUF]);
        let mut tcp_sock = tcp::Socket::new(tcp_rx, tcp_tx);
        tcp_sock.listen(ECHO_PORT).expect("tcp listen :7");

        Self {
            udp: sockets.add(udp_sock),
            tcp: sockets.add(tcp_sock),
        }
    }

    pub fn poll<'a>(&self, sockets: &mut SocketSet<'a>) {
        // UDP: bounce every pending datagram back to its sender.
        let udp_sock = sockets.get_mut::<udp::Socket>(self.udp);
        loop {
            let (data, endpoint): (Vec<u8>, IpEndpoint) = match udp_sock.recv() {
                Ok((buf, meta)) => (buf.to_vec(), meta.endpoint),
                Err(udp::RecvError::Exhausted) => break,
                #[allow(unreachable_patterns)]
                Err(e) => {
                    log::warn!("echo UDP recv error: {:?}", e);
                    break;
                }
            };
            if let Err(e) = udp_sock.send_slice(&data, endpoint) {
                log::warn!("echo UDP send error: {:?}", e);
                break;
            }
        }

        // TCP: copy rx → tx, then re-arm listen if the connection closed.
        let tcp_sock = sockets.get_mut::<tcp::Socket>(self.tcp);
        if tcp_sock.can_recv() && tcp_sock.can_send() {
            if let Ok(payload) = tcp_sock.recv(|buf| (buf.len(), buf.to_vec())) {
                if !payload.is_empty() {
                    let _ = tcp_sock.send_slice(&payload);
                }
            }
        }
        if !tcp_sock.is_open() {
            let _ = tcp_sock.listen(ECHO_PORT);
        }
    }
}
