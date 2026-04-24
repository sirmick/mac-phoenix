//! In-bridge bulk-data TCP server on the gateway IP, port 8.
//!
//! On each new connection, streams a predictable pattern — byte `i` is
//! `i & 0xff` — for `BULK_BYTES` total, then closes. Lets guest tests
//! exercise the Mac-side receive-window path without depending on an
//! external host. Specifically: if the NAT's flow control is broken and
//! it pushes past the Mac's advertised window, the guest app sees fewer
//! bytes than we sent and the pattern check fails at a specific offset.

use smoltcp::iface::{SocketHandle, SocketSet};
use smoltcp::socket::tcp;

const BULK_PORT: u16 = 8;
/// How many bytes each accepted connection receives before close. Chosen
/// well above classic Mac TCP's default receive window (4-8 KB) so that
/// any NAT-side flow-control bug shows up as a truncated transfer.
pub const BULK_BYTES: usize = 64 * 1024;
const BULK_TX_BUF: usize = 8 * 1024;
const BULK_RX_BUF: usize = 512; // we never receive much

pub struct BulkServer {
    sock: SocketHandle,
    /// Bytes pushed into the socket's TX buffer so far on the current
    /// connection. Reset on each new accept.
    bytes_sent: usize,
    /// Whether we're currently streaming a connection.
    active: bool,
}

impl BulkServer {
    pub fn new(sockets: &mut SocketSet<'_>) -> Self {
        let rx = tcp::SocketBuffer::new(vec![0u8; BULK_RX_BUF]);
        let tx = tcp::SocketBuffer::new(vec![0u8; BULK_TX_BUF]);
        let mut s = tcp::Socket::new(rx, tx);
        s.listen(BULK_PORT).expect("bulk listen :8");
        Self {
            sock: sockets.add(s),
            bytes_sent: 0,
            active: false,
        }
    }

    pub fn poll(&mut self, sockets: &mut SocketSet<'_>) {
        let sock = sockets.get_mut::<tcp::Socket>(self.sock);

        // Detect new accepted connection.
        if !self.active && sock.may_send() {
            self.bytes_sent = 0;
            self.active = true;
            log::info!(
                "bulk server: accepted connection; streaming {} bytes",
                BULK_BYTES
            );
        }

        // Push pattern bytes as fast as smoltcp will take them. smoltcp
        // honors the peer's advertised receive window internally, so
        // this naturally throttles when the Mac stops acking.
        if self.active && sock.can_send() {
            let chunk_cap = 1024;
            let mut pushed_this_tick = 0usize;
            while self.bytes_sent < BULK_BYTES && pushed_this_tick < chunk_cap {
                let remaining = BULK_BYTES - self.bytes_sent;
                let want = remaining.min(chunk_cap - pushed_this_tick);
                let start = self.bytes_sent;
                let result = sock.send(|buf| {
                    let n = buf.len().min(want);
                    for i in 0..n {
                        buf[i] = ((start + i) & 0xff) as u8;
                    }
                    (n, n)
                });
                match result {
                    Ok(n) if n > 0 => {
                        self.bytes_sent += n;
                        pushed_this_tick += n;
                    }
                    _ => break,
                }
            }

            if self.bytes_sent >= BULK_BYTES {
                log::info!(
                    "bulk server: finished streaming {} bytes, closing",
                    BULK_BYTES
                );
                sock.close();
            }
        }

        // Connection fully torn down → re-arm listen for next client.
        if !sock.is_open() {
            if self.active {
                self.active = false;
                self.bytes_sent = 0;
            }
            let _ = sock.listen(BULK_PORT);
        }
    }
}
