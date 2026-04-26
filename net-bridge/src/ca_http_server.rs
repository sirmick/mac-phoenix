//! Serves the MITM root CA cert on the gateway IP over HTTP/1.0 with
//! `Content-Type: application/x-x509-ca-cert`. Classic-Mac browsers
//! (Netscape 2/3/4, early MSIE for Mac) recognise that MIME type as a
//! cert-import trigger and pop the "Accept this Certificate Authority"
//! dialog — which is the only reliable import path for NN3, since its
//! preferences UI has no file-import button.
//!
//! URL: http://10.0.2.1/MitmCA.crt (PEM) or /MitmCA.cer (DER).
//!
//! Runs inside smoltcp so port 80 on the gateway never touches the real
//! host network. Disabled when MITM is disabled.

use smoltcp::iface::{SocketHandle, SocketSet};
use smoltcp::socket::tcp;

const HTTP_PORT: u16 = 80;
const TCP_RX_BUF: usize = 1024;
const TCP_TX_BUF: usize = 8 * 1024; // enough for a tiny cert + headers

pub struct CaHttpServer {
    sock: SocketHandle,
    pem: Vec<u8>,
    der: Vec<u8>,
    /// Per-connection state: what we've pushed to the TX buffer so far.
    response: Vec<u8>,
    response_offset: usize,
    active: bool,
}

impl CaHttpServer {
    pub fn new(sockets: &mut SocketSet<'_>, ca_pem: Vec<u8>, ca_der: Vec<u8>) -> Self {
        let rx = tcp::SocketBuffer::new(vec![0u8; TCP_RX_BUF]);
        let tx = tcp::SocketBuffer::new(vec![0u8; TCP_TX_BUF]);
        let mut s = tcp::Socket::new(rx, tx);
        s.listen(HTTP_PORT).expect("ca-http listen :80");
        Self {
            sock: sockets.add(s),
            pem: ca_pem,
            der: ca_der,
            response: Vec::new(),
            response_offset: 0,
            active: false,
        }
    }

    pub fn poll(&mut self, sockets: &mut SocketSet<'_>) {
        let sock = sockets.get_mut::<tcp::Socket>(self.sock);

        // Peek at the request line(s) to decide which file (PEM vs DER).
        if !self.active && sock.may_send() && sock.can_recv() {
            // Grab whatever's in the RX buffer up to ~512 bytes.
            let req_line = sock
                .recv(|buf| {
                    let take = buf.len().min(512);
                    let s = std::str::from_utf8(&buf[..take]).unwrap_or("").to_string();
                    (take, s)
                })
                .unwrap_or_default();
            if !req_line.is_empty() {
                self.response = self.build_response(&req_line);
                self.response_offset = 0;
                self.active = true;
                log::info!(
                    "ca-http: serving {} byte response",
                    self.response.len()
                );
            }
        }

        // Push response bytes into TX until it's drained.
        if self.active && sock.can_send() {
            while self.response_offset < self.response.len() && sock.can_send() {
                let remaining = &self.response[self.response_offset..];
                let pushed = sock.send_slice(remaining).unwrap_or(0);
                if pushed == 0 {
                    break;
                }
                self.response_offset += pushed;
            }
            if self.response_offset >= self.response.len() {
                sock.close();
            }
        }

        // Torn down → re-arm for next client.
        if !sock.is_open() {
            if self.active {
                self.active = false;
                self.response.clear();
                self.response_offset = 0;
            }
            let _ = sock.listen(HTTP_PORT);
        }
    }

    /// Build a minimal HTTP/1.0 response. Parses the request line's path
    /// to decide whether to serve PEM or DER. Anything else → small
    /// landing page.
    fn build_response(&self, req: &str) -> Vec<u8> {
        // First line looks like "GET /path HTTP/1.0\r\n..."
        let path = req
            .split_whitespace()
            .nth(1)
            .unwrap_or("/")
            .to_string();

        let (body, content_type): (Vec<u8>, &str) = match path.as_str() {
            "/MitmCA.crt" | "/mitmca.crt" => {
                (self.pem.clone(), "application/x-x509-ca-cert")
            }
            "/MitmCA.cer" | "/mitmca.cer" => {
                (self.der.clone(), "application/x-x509-ca-cert")
            }
            _ => {
                let page = concat!(
                    "<html><head><title>MacPhoenix MITM CA</title></head>",
                    "<body><h1>MacPhoenix MITM Root CA</h1>",
                    "<p>Click one of the links below to install this CA ",
                    "as a trusted root for your browser.</p>",
                    "<ul>",
                    "<li><a href=\"/MitmCA.crt\">MitmCA.crt</a> (PEM — Netscape)</li>",
                    "<li><a href=\"/MitmCA.cer\">MitmCA.cer</a> (DER — MSIE)</li>",
                    "</ul></body></html>"
                );
                (page.as_bytes().to_vec(), "text/html")
            }
        };

        let mut resp = Vec::with_capacity(body.len() + 256);
        let header = format!(
            "HTTP/1.0 200 OK\r\n\
             Content-Type: {}\r\n\
             Content-Length: {}\r\n\
             Connection: close\r\n\
             \r\n",
            content_type,
            body.len()
        );
        resp.extend_from_slice(header.as_bytes());
        resp.extend_from_slice(&body);
        resp
    }
}
