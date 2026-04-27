//! HTTP-rewrite proxy for vintage browsers.
//!
//! Listens on gateway:8080 inside smoltcp. Accepts HTTP/1.0 proxy
//! requests in absolute-URL form (`GET https://example.com/ HTTP/1.0`),
//! fetches them via host-side ureq with modern TLS, rewrites every
//! `https://` in the response body to `http://` so subsequent clicks /
//! image loads come back through the proxy too, and returns the result
//! as plain HTTP.
//!
//! Why: classic-Mac browsers (NN3, iCab 2.9.x, MSIE 4.5 Mac) have no
//! UI to import a custom root CA, so the MITM-TLS path is unreachable
//! end-to-end. This sidesteps the cert-trust problem entirely — guests
//! never see TLS at all. Same architecture as WebOne and FrogFind,
//! built directly into net-bridge so the user just configures their
//! browser's HTTP proxy to 10.0.2.1:8080.
//!
//! Limits:
//!   - HTML rewriting is regex-style on response bodies (text/html, text/css)
//!   - Cookies pass through unchanged; same-origin policy may reinterpret
//!     domains since scheme changes
//!   - JS-heavy SPAs won't render in a 1996 browser anyway, so we don't try
//!   - Streaming responses are buffered (memory cap = 8 MiB per response)

use smoltcp::iface::{SocketHandle, SocketSet};
use smoltcp::socket::tcp;
use std::io::Read;
use std::sync::mpsc::{self, Receiver, TryRecvError};
use std::thread;
use std::time::Duration;

const PROXY_PORT: u16 = 8080;
const TCP_RX_BUF: usize = 16 * 1024;     // 16 KiB request headers max
const TCP_TX_BUF: usize = 256 * 1024;    // 256 KiB outbound staging
const REQ_HEADER_CAP: usize = 32 * 1024; // 32 KiB total req-headers cap
const RESP_BODY_CAP: usize = 8 * 1024 * 1024; // 8 MiB max response
const FETCH_TIMEOUT_S: u64 = 30;

pub struct HttpProxy {
    sock: SocketHandle,
    state: ProxyState,
}

enum ProxyState {
    /// Idle / awaiting client.
    Listening,
    /// Receiving request bytes; accumulating until end-of-headers.
    Receiving { buf: Vec<u8> },
    /// Worker thread is fetching upstream; waiting on the channel.
    Fetching { rx: Receiver<Vec<u8>> },
    /// Response buffered; drain to TX.
    Sending { resp: Vec<u8>, offset: usize },
}

impl HttpProxy {
    pub fn new(sockets: &mut SocketSet<'_>) -> Self {
        let rx = tcp::SocketBuffer::new(vec![0u8; TCP_RX_BUF]);
        let tx = tcp::SocketBuffer::new(vec![0u8; TCP_TX_BUF]);
        let mut s = tcp::Socket::new(rx, tx);
        s.listen(PROXY_PORT).expect("http-proxy listen :8080");
        Self {
            sock: sockets.add(s),
            state: ProxyState::Listening,
        }
    }

    pub fn poll(&mut self, sockets: &mut SocketSet<'_>) {
        let sock = sockets.get_mut::<tcp::Socket>(self.sock);

        // Detect new connection — transition out of Listening.
        if matches!(self.state, ProxyState::Listening) && sock.is_active() {
            self.state = ProxyState::Receiving { buf: Vec::with_capacity(2048) };
        }

        // Receive request bytes.
        if let ProxyState::Receiving { buf } = &mut self.state {
            while sock.can_recv() {
                let appended = sock.recv(|chunk| {
                    let n = chunk.len();
                    buf.extend_from_slice(chunk);
                    (n, n)
                }).unwrap_or(0);
                if appended == 0 { break; }
            }
            // Got end-of-headers? Dispatch.
            if let Some(end) = find_headers_end(buf) {
                let need_body = parse_content_length(&buf[..end]);
                let total_needed = end + need_body.unwrap_or(0);
                if buf.len() >= total_needed {
                    let req = std::mem::take(buf);
                    log::info!(
                        "http-proxy: received request ({} bytes total, {} body)",
                        req.len(), need_body.unwrap_or(0)
                    );
                    self.state = dispatch_fetch(req);
                } else if buf.len() >= REQ_HEADER_CAP + RESP_BODY_CAP {
                    // Sanity cap.
                    log::warn!("http-proxy: oversized request, aborting");
                    self.state = ProxyState::Sending {
                        resp: error_response(413, "Payload Too Large"),
                        offset: 0,
                    };
                }
            } else if buf.len() > REQ_HEADER_CAP {
                log::warn!("http-proxy: request headers > {}B without CRLFCRLF", REQ_HEADER_CAP);
                self.state = ProxyState::Sending {
                    resp: error_response(431, "Request Header Fields Too Large"),
                    offset: 0,
                };
            }
        }

        // Wait for fetch result.
        if let ProxyState::Fetching { rx } = &mut self.state {
            match rx.try_recv() {
                Ok(resp) => {
                    log::info!("http-proxy: upstream returned {} bytes", resp.len());
                    self.state = ProxyState::Sending { resp, offset: 0 };
                }
                Err(TryRecvError::Empty) => { /* keep waiting */ }
                Err(TryRecvError::Disconnected) => {
                    log::warn!("http-proxy: worker thread died without response");
                    self.state = ProxyState::Sending {
                        resp: error_response(502, "Bad Gateway"),
                        offset: 0,
                    };
                }
            }
        }

        // Drain response.
        if let ProxyState::Sending { resp, offset } = &mut self.state {
            while *offset < resp.len() && sock.can_send() {
                let n = sock.send_slice(&resp[*offset..]).unwrap_or(0);
                if n == 0 { break; }
                *offset += n;
            }
            if *offset >= resp.len() {
                sock.close();
                self.state = ProxyState::Listening;
            }
        }

        // Connection torn down — re-arm.
        if !sock.is_open() {
            self.state = ProxyState::Listening;
            let _ = sock.listen(PROXY_PORT);
        }
    }
}

/// Find `\r\n\r\n` end-of-headers position. Returns the byte index of
/// the first byte AFTER the blank line, i.e. where the body starts.
fn find_headers_end(buf: &[u8]) -> Option<usize> {
    buf.windows(4).position(|w| w == b"\r\n\r\n").map(|i| i + 4)
}

/// Pull `Content-Length: N` out of header text. None if absent.
fn parse_content_length(hdr_bytes: &[u8]) -> Option<usize> {
    let s = std::str::from_utf8(hdr_bytes).ok()?;
    for line in s.split("\r\n") {
        let lower = line.get(..15).map(|p| p.eq_ignore_ascii_case("content-length:")).unwrap_or(false);
        if lower {
            return line[15..].trim().parse().ok();
        }
    }
    None
}

/// Build a minimal error response.
fn error_response(code: u16, reason: &str) -> Vec<u8> {
    let body = format!(
        "<html><body><h1>{} {}</h1><p>HTTP proxy error.</p></body></html>",
        code, reason
    );
    format!(
        "HTTP/1.0 {} {}\r\n\
         Content-Type: text/html\r\n\
         Content-Length: {}\r\n\
         Connection: close\r\n\
         \r\n{}",
        code, reason, body.len(), body
    ).into_bytes()
}

/// Spawn a worker thread to fetch upstream. Returns the new ProxyState
/// (Fetching with the rx side of the response channel) or Sending with
/// an error response if the request couldn't be parsed.
fn dispatch_fetch(req: Vec<u8>) -> ProxyState {
    let req_str = match std::str::from_utf8(&req) {
        Ok(s) => s.to_string(),
        Err(_) => return ProxyState::Sending {
            resp: error_response(400, "Bad Request — non-UTF8 headers"),
            offset: 0,
        },
    };
    let parsed = match parse_proxy_request(&req_str) {
        Some(p) => p,
        None => return ProxyState::Sending {
            resp: error_response(400, "Bad Request — could not parse"),
            offset: 0,
        },
    };

    let (tx, rx) = mpsc::channel::<Vec<u8>>();
    let body_offset = find_headers_end(&req).unwrap_or(req.len());
    let body = req[body_offset..].to_vec();

    thread::spawn(move || {
        let resp = fetch_upstream(parsed, body);
        let _ = tx.send(resp);
    });

    ProxyState::Fetching { rx }
}

struct ParsedRequest {
    method: String,
    url: String,
    headers: Vec<(String, String)>,
}

/// Parse a proxy-form request line + headers. Accepts:
///   GET http://example.com/path HTTP/1.0
///   GET https://example.com/path HTTP/1.0
///   POST http://example.com/form HTTP/1.0
/// Returns None if the URL isn't absolute (vintage browsers in proxy
/// mode always send absolute URLs; non-proxy clients shouldn't reach us).
fn parse_proxy_request(req: &str) -> Option<ParsedRequest> {
    let mut lines = req.split("\r\n");
    let request_line = lines.next()?;
    let mut parts = request_line.split_whitespace();
    let method = parts.next()?.to_string();
    let raw_url = parts.next()?.to_string();
    let _version = parts.next()?;

    // Vintage browsers in proxy mode send absolute URL. If they sent
    // origin-form (path only), they don't think we're a proxy → reject.
    if !raw_url.starts_with("http://") && !raw_url.starts_with("https://") {
        return None;
    }

    let mut headers = Vec::new();
    for line in lines {
        if line.is_empty() { break; }
        if let Some((k, v)) = line.split_once(':') {
            headers.push((k.trim().to_string(), v.trim().to_string()));
        }
    }
    Some(ParsedRequest { method, url: raw_url, headers })
}

fn fetch_upstream(req: ParsedRequest, body: Vec<u8>) -> Vec<u8> {
    log::info!("http-proxy: fetching {} {}", req.method, req.url);
    let agent = ureq::AgentBuilder::new()
        .timeout(Duration::from_secs(FETCH_TIMEOUT_S))
        .redirects(5)
        // Identify ourselves so logs upstream are clear; browsers send their
        // own UA via headers loop below.
        .user_agent("MacPhoenix-net-bridge/0.1 (HTTP rewrite proxy)")
        .build();
    let mut r = match req.method.to_uppercase().as_str() {
        "GET"     => agent.get(&req.url),
        "POST"    => agent.post(&req.url),
        "HEAD"    => agent.head(&req.url),
        "PUT"     => agent.put(&req.url),
        "DELETE"  => agent.delete(&req.url),
        "OPTIONS" => agent.request("OPTIONS", &req.url),
        m         => agent.request(m, &req.url),
    };
    // Forward the guest's headers, with a few skipped (we set our own
    // Connection / Content-Length, and Host comes from the URL).
    for (k, v) in &req.headers {
        let lk = k.to_lowercase();
        if lk == "host"
            || lk == "connection"
            || lk == "proxy-connection"
            || lk == "content-length"
            || lk == "accept-encoding"
        {
            continue;
        }
        r = r.set(k, v);
    }
    // ureq handles gzip transparently — strip Accept-Encoding so the upstream
    // doesn't send something we'd then have to decode and not pass through.

    let response = if body.is_empty() {
        r.call()
    } else {
        r.send_bytes(&body)
    };

    match response {
        Ok(resp) => format_response(resp),
        Err(ureq::Error::Status(code, resp)) => {
            log::warn!("http-proxy: upstream returned status {}", code);
            format_response(resp)
        }
        Err(e) => {
            log::warn!("http-proxy: fetch error: {}", e);
            error_response(502, &format!("Bad Gateway: {}", e))
        }
    }
}

fn format_response(resp: ureq::Response) -> Vec<u8> {
    let status = resp.status();
    let status_text = resp.status_text().to_string();

    // Snapshot headers BEFORE consuming the body.
    let mut header_lines: Vec<(String, String)> = resp
        .headers_names()
        .iter()
        .filter_map(|n| resp.header(n).map(|v| (n.clone(), v.to_string())))
        .collect();

    let content_type = resp
        .header("content-type")
        .map(|s| s.to_lowercase())
        .unwrap_or_default();
    let is_text = content_type.contains("text/html")
        || content_type.contains("text/css")
        || content_type.contains("text/xml")
        || content_type.contains("application/xml");

    // Read body, capped.
    let mut body = Vec::new();
    let mut reader = resp.into_reader().take(RESP_BODY_CAP as u64);
    if let Err(e) = std::io::copy(&mut reader, &mut body) {
        log::warn!("http-proxy: body read error: {}", e);
    }

    // Rewrite https:// → http:// in text bodies so guest clicks come back
    // through us.
    if is_text {
        body = rewrite_https_to_http(&body);
    }

    // Build response. Strip headers that don't survive scheme/length rewrite.
    header_lines.retain(|(k, _)| {
        let lk = k.to_lowercase();
        lk != "content-length"
            && lk != "transfer-encoding"
            && lk != "content-encoding"      // ureq already decoded gzip
            && lk != "connection"
            && lk != "strict-transport-security"  // HSTS would force https
            && lk != "alt-svc"
    });

    let mut out = format!("HTTP/1.0 {} {}\r\n", status, status_text).into_bytes();
    for (k, v) in &header_lines {
        out.extend_from_slice(k.as_bytes());
        out.extend_from_slice(b": ");
        out.extend_from_slice(v.as_bytes());
        out.extend_from_slice(b"\r\n");
    }
    out.extend_from_slice(format!("Content-Length: {}\r\n", body.len()).as_bytes());
    out.extend_from_slice(b"Connection: close\r\n\r\n");
    out.extend_from_slice(&body);
    out
}

/// Brute-force scheme rewrite. Catches every `https://` occurrence in
/// HTML/CSS/XML bodies. Sites that depend on https:// in JS strings will
/// break, but JS-heavy sites won't render in a 1996 browser anyway.
fn rewrite_https_to_http(body: &[u8]) -> Vec<u8> {
    let needle = b"https://";
    let replacement = b"http://";
    if !body.windows(needle.len()).any(|w| w == needle) {
        return body.to_vec();
    }
    let mut out = Vec::with_capacity(body.len());
    let mut i = 0;
    while i < body.len() {
        if i + needle.len() <= body.len() && &body[i..i + needle.len()] == needle {
            out.extend_from_slice(replacement);
            i += needle.len();
        } else {
            out.push(body[i]);
            i += 1;
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_request_get() {
        let req = "GET https://example.com/ HTTP/1.0\r\nHost: example.com\r\nUser-Agent: iCab\r\n\r\n";
        let p = parse_proxy_request(req).unwrap();
        assert_eq!(p.method, "GET");
        assert_eq!(p.url, "https://example.com/");
        assert_eq!(p.headers.len(), 2);
    }

    #[test]
    fn parse_request_origin_form_rejected() {
        let req = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
        assert!(parse_proxy_request(req).is_none());
    }

    #[test]
    fn rewrite_https() {
        let body = b"<a href=\"https://www.example.com/\">click</a> and https://other.com";
        let rewritten = rewrite_https_to_http(body);
        assert_eq!(
            std::str::from_utf8(&rewritten).unwrap(),
            "<a href=\"http://www.example.com/\">click</a> and http://other.com"
        );
    }

    #[test]
    fn headers_end_detection() {
        assert_eq!(find_headers_end(b"GET / HTTP/1.0\r\n\r\n"), Some(18));
        assert_eq!(find_headers_end(b"GET / HTTP/1.0\r\nHost: x\r\n\r\nbody"), Some(27));
        assert_eq!(find_headers_end(b"GET / HTTP/1.0\r\nHost: x\r\n"), None);
    }

    #[test]
    fn content_length_parsing() {
        let h = b"POST / HTTP/1.0\r\nContent-Length: 42\r\n\r\n";
        assert_eq!(parse_content_length(h), Some(42));
        let h = b"GET / HTTP/1.0\r\nHost: x\r\n\r\n";
        assert_eq!(parse_content_length(h), None);
    }
}
