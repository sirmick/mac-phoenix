//! Downstream (guest-facing) TLS acceptor.
//!
//! Builds an OpenSSL `SslContext` configured for SSLv3 / TLS 1.0–1.2 with
//! the classic-Mac weak-RSA cipher set, using a just-in-time leaf cert
//! minted from the MITM CA. Relies on the vendored OpenSSL 3.0.13 in
//! `net-bridge/vendor/openssl-legacy` — the system library won't work
//! because Ubuntu strips SSLv3 and RC4 at build time.
//!
//! The legacy provider must be loaded once at startup before any SSLv3
//! or RC4 negotiation can succeed. See `enable_legacy_algorithms`.

use std::collections::VecDeque;
use std::io::{self, ErrorKind, Read, Write};
use std::net::TcpStream;
use std::sync::Once;

use openssl::error::ErrorStack;
use openssl::pkey::PKey;
use openssl::provider::Provider;
use openssl::ssl::{
    ErrorCode, Ssl, SslContext, SslContextBuilder, SslMethod, SslOptions, SslStream,
    SslVerifyMode, SslVersion,
};
use openssl::x509::X509;

use crate::tls_mitm::{Error, MitmCa};

/// Cipher list expressing the RSA-only, MD5/SHA1 suite a classic-Mac
/// browser (MSIE 4.5 / Netscape 3.x/4.x) will actually negotiate.
/// `@SECLEVEL=0` disables OpenSSL's modern security-level filter.
const LEGACY_CIPHERS: &str =
    "RC4-MD5:RC4-SHA:DES-CBC3-SHA:AES128-SHA:AES256-SHA:@SECLEVEL=0";

static PROVIDERS: Once = Once::new();
static mut LEGACY_PROVIDER: Option<Provider> = None;
static mut DEFAULT_PROVIDER: Option<Provider> = None;

/// Load OpenSSL's legacy provider so RC4/DES primitives are available.
/// Safe to call repeatedly; only the first call has an effect.
pub fn enable_legacy_algorithms() -> Result<(), ErrorStack> {
    let mut err: Option<ErrorStack> = None;
    PROVIDERS.call_once(|| {
        // Loading `legacy` on its own disables the implicit default, so we
        // load both explicitly and hold them for the process lifetime.
        match (Provider::load(None, "default"), Provider::load(None, "legacy")) {
            (Ok(d), Ok(l)) => unsafe {
                DEFAULT_PROVIDER = Some(d);
                LEGACY_PROVIDER = Some(l);
            },
            (Err(e), _) | (_, Err(e)) => err = Some(e),
        }
    });
    if let Some(e) = err {
        return Err(e);
    }
    Ok(())
}

/// Build an `SslContext` that presents a freshly minted leaf for
/// `hostname`, signed by `ca`, and accepts classic-Mac handshakes.
pub fn build_acceptor_ctx(ca: &MitmCa, hostname: &str) -> Result<SslContext, Error> {
    enable_legacy_algorithms()?;

    let leaf = ca.mint_leaf(hostname)?;
    let cert = X509::from_pem(&leaf.cert_pem)?;
    let key = PKey::private_key_from_pem(&leaf.key_pem)?;

    let mut b = SslContextBuilder::new(SslMethod::tls_server())?;
    // The openssl crate sets `SSL_OP_NO_SSLV3` by default on new
    // contexts — that overrides our min-proto-version. Clear it.
    b.clear_options(SslOptions::NO_SSLV3);
    // SSLv3 floor — matches the agreed protocol range.
    b.set_min_proto_version(Some(SslVersion::SSL3))?;
    b.set_max_proto_version(Some(SslVersion::TLS1_2))?;
    // `@SECLEVEL=0` in the cipher list isn't enough on OpenSSL 3.x: the
    // context itself also enforces a floor that blocks SSLv3 + small keys.
    b.set_security_level(0);
    b.set_cipher_list(LEGACY_CIPHERS)?;
    b.set_certificate(&cert)?;
    b.set_private_key(&key)?;
    b.check_private_key()?;
    // Classic clients don't do session tickets; suppress noise.
    b.set_options(SslOptions::NO_TICKET);

    Ok(b.build())
}

/// In-memory duplex used as the downstream `SslStream`'s I/O. The guest-side
/// byte pipe accumulates ciphertext in both directions; the caller pushes
/// bytes from the guest via [`feed_from_guest`] and drains bytes to the
/// guest via [`take_to_guest`].
#[derive(Default)]
pub struct GuestIo {
    inbound: VecDeque<u8>,
    outbound: VecDeque<u8>,
    /// Guest TCP has closed; signal EOF once `inbound` drains.
    closed: bool,
}

impl GuestIo {
    pub fn feed_from_guest(&mut self, data: &[u8]) {
        self.inbound.extend(data);
    }

    pub fn take_to_guest(&mut self) -> Vec<u8> {
        self.outbound.drain(..).collect()
    }

    pub fn mark_closed(&mut self) {
        self.closed = true;
    }

    pub fn inbound_len(&self) -> usize {
        self.inbound.len()
    }

    pub fn outbound_len(&self) -> usize {
        self.outbound.len()
    }
}

impl Read for GuestIo {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        if self.inbound.is_empty() {
            if self.closed {
                return Ok(0);
            }
            return Err(io::Error::new(ErrorKind::WouldBlock, "no inbound data"));
        }
        let n = buf.len().min(self.inbound.len());
        for slot in buf.iter_mut().take(n) {
            *slot = self.inbound.pop_front().unwrap();
        }
        Ok(n)
    }
}

impl Write for GuestIo {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.outbound.extend(buf);
        Ok(buf.len())
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

/// Build a modern TLS client context for the upstream hop. Uses the
/// system trust store; validates real server certs strictly. The
/// vendored OpenSSL doesn't ship a CA bundle, so we point it at the
/// distro's via `OPENSSL_CERT_DIR`/`OPENSSL_CERT_FILE` env defaults
/// plus `set_default_verify_paths()`.
fn build_upstream_ctx() -> Result<SslContext, Error> {
    let mut b = SslContextBuilder::new(SslMethod::tls_client())?;
    b.set_default_verify_paths()?;
    b.set_verify(SslVerifyMode::PEER);
    Ok(b.build())
}

/// Per-connection TLS MITM state: downstream (guest-facing, legacy) +
/// upstream (host-facing, modern), joined plaintext-to-plaintext.
pub struct TlsBridge {
    downstream: SslStream<GuestIo>,
    downstream_done: bool,
    upstream: SslStream<TcpStream>,
    upstream_done: bool,
    /// Plaintext waiting to be re-encrypted upstream once upstream handshake
    /// completes. Lets the downstream handshake + first app-data flow even
    /// while the upstream is still handshaking.
    pending_to_upstream: Vec<u8>,
    /// Plaintext from upstream waiting to be re-encrypted downstream once
    /// downstream handshake completes.
    pending_to_downstream: Vec<u8>,
}

/// Result of a single `drive()` tick.
#[derive(Debug, Default)]
pub struct BridgeTick {
    /// Ciphertext to write to the guest (send via TCP frames).
    pub bytes_to_guest: Vec<u8>,
    /// True once both sides are in DataPhase.
    pub handshake_done: bool,
    /// Upstream peer closed cleanly.
    pub upstream_eof: bool,
    /// Downstream peer closed cleanly.
    pub downstream_eof: bool,
}

impl TlsBridge {
    pub fn new(
        ca: &MitmCa,
        hostname: &str,
        upstream_tcp: TcpStream,
    ) -> Result<Self, Error> {
        Self::new_with_upstream_ctx(ca, hostname, upstream_tcp, build_upstream_ctx()?)
    }

    /// Test-friendly variant: caller supplies the upstream TLS context so
    /// a mock upstream server can be trusted (e.g. by loading a test CA
    /// instead of the system trust store).
    pub fn new_with_upstream_ctx(
        ca: &MitmCa,
        hostname: &str,
        upstream_tcp: TcpStream,
        us_ctx: SslContext,
    ) -> Result<Self, Error> {
        enable_legacy_algorithms()?;

        let ds_ctx = build_acceptor_ctx(ca, hostname)?;
        let mut ds_ssl = Ssl::new(&ds_ctx)?;
        ds_ssl.set_accept_state();
        let downstream = SslStream::new(ds_ssl, GuestIo::default())?;

        let mut us_ssl = Ssl::new(&us_ctx)?;
        us_ssl.set_connect_state();
        // SSLv3 has no SNI, so when the guest speaks SSLv3 we fell back
        // to the IP literal as `hostname`. Sending that as an upstream SNI
        // is usually wrong (the server keys on the hostname, not the IP)
        // and asserting cert-hostname-match against the IP always fails.
        // In that case we skip both: still do chain verification via
        // SslVerifyMode::PEER, just don't pin the hostname.
        if !hostname.parse::<std::net::Ipv4Addr>().is_ok() {
            us_ssl.set_hostname(hostname)?;
            us_ssl.param_mut().set_host(hostname)?;
        } else {
            log::debug!(
                "TlsBridge: no SNI from guest for {}; upstream handshake \
                 will skip hostname verification (chain still validated)",
                hostname
            );
        }
        upstream_tcp.set_nonblocking(true).ok();
        let upstream = SslStream::new(us_ssl, upstream_tcp)?;

        Ok(Self {
            downstream,
            downstream_done: false,
            upstream,
            upstream_done: false,
            pending_to_upstream: Vec::new(),
            pending_to_downstream: Vec::new(),
        })
    }

    pub fn feed_from_guest(&mut self, bytes: &[u8]) {
        self.downstream.get_mut().feed_from_guest(bytes);
    }

    pub fn close_downstream(&mut self) {
        self.downstream.get_mut().mark_closed();
    }

    /// One iteration of the bidirectional pump. Call from both the
    /// handle_frame (after feeding guest bytes) and poll paths.
    pub fn drive(&mut self) -> Result<BridgeTick, Error> {
        let mut tick = BridgeTick::default();

        // 1. Advance handshakes where possible. Order matters: the
        // downstream handshake can partially complete purely on bytes
        // already buffered, so try it first on every tick.
        if !self.downstream_done {
            match self.downstream.do_handshake() {
                Ok(()) => self.downstream_done = true,
                Err(e) => match e.code() {
                    ErrorCode::WANT_READ | ErrorCode::WANT_WRITE => {}
                    _ => return Err(handshake_err("downstream", e)),
                },
            }
        }
        if !self.upstream_done {
            match self.upstream.do_handshake() {
                Ok(()) => self.upstream_done = true,
                Err(e) => match e.code() {
                    ErrorCode::WANT_READ | ErrorCode::WANT_WRITE => {}
                    _ => return Err(handshake_err("upstream", e)),
                },
            }
        }

        // 2. If downstream is past handshake, try to read plaintext from it
        // and queue for upstream.
        if self.downstream_done {
            let mut buf = [0u8; 4096];
            loop {
                match self.downstream.ssl_read(&mut buf) {
                    Ok(0) => {
                        tick.downstream_eof = true;
                        break;
                    }
                    Ok(n) => self.pending_to_upstream.extend_from_slice(&buf[..n]),
                    Err(ref e) if e.code() == ErrorCode::WANT_READ
                        || e.code() == ErrorCode::WANT_WRITE =>
                    {
                        break
                    }
                    Err(e)
                        if e.code() == ErrorCode::ZERO_RETURN
                            || is_unexpected_eof(&e) =>
                    {
                        tick.downstream_eof = true;
                        break;
                    }
                    Err(e) => return Err(handshake_err("downstream read", e)),
                }
            }
        }

        // 3. If upstream is past handshake, flush any pending plaintext.
        if self.upstream_done && !self.pending_to_upstream.is_empty() {
            let payload = std::mem::take(&mut self.pending_to_upstream);
            match self.upstream.ssl_write(&payload) {
                Ok(n) if n == payload.len() => {}
                Ok(n) => {
                    // Short write — put the remainder back at the front.
                    self.pending_to_upstream = payload[n..].to_vec();
                }
                Err(ref e) if e.code() == ErrorCode::WANT_READ
                    || e.code() == ErrorCode::WANT_WRITE =>
                {
                    // Buffer for next tick.
                    self.pending_to_upstream = payload;
                }
                Err(e) => return Err(handshake_err("upstream write", e)),
            }
        }

        // 4. Read plaintext from upstream, queue for downstream.
        if self.upstream_done {
            let mut buf = [0u8; 4096];
            loop {
                match self.upstream.ssl_read(&mut buf) {
                    Ok(0) => {
                        tick.upstream_eof = true;
                        break;
                    }
                    Ok(n) => self.pending_to_downstream.extend_from_slice(&buf[..n]),
                    Err(ref e) if e.code() == ErrorCode::WANT_READ
                        || e.code() == ErrorCode::WANT_WRITE =>
                    {
                        break
                    }
                    Err(e)
                        if e.code() == ErrorCode::ZERO_RETURN
                            || is_unexpected_eof(&e) =>
                    {
                        tick.upstream_eof = true;
                        break;
                    }
                    Err(e) => return Err(handshake_err("upstream read", e)),
                }
            }
        }

        // 5. Flush plaintext to downstream.
        if self.downstream_done && !self.pending_to_downstream.is_empty() {
            let payload = std::mem::take(&mut self.pending_to_downstream);
            match self.downstream.ssl_write(&payload) {
                Ok(n) if n == payload.len() => {}
                Ok(n) => {
                    self.pending_to_downstream = payload[n..].to_vec();
                }
                Err(ref e) if e.code() == ErrorCode::WANT_READ
                    || e.code() == ErrorCode::WANT_WRITE =>
                {
                    self.pending_to_downstream = payload;
                }
                Err(e) => return Err(handshake_err("downstream write", e)),
            }
        }

        // 6. Drain ciphertext that downstream wants sent to the guest.
        tick.bytes_to_guest = self.downstream.get_mut().take_to_guest();
        tick.handshake_done = self.downstream_done && self.upstream_done;
        Ok(tick)
    }
}

/// Many classic-era peers tear down the TCP socket without sending a TLS
/// `close_notify`. OpenSSL surfaces that as a `SYSCALL` error with either
/// `UnexpectedEof` (io::ErrorKind) or an empty error stack. Treat it as
/// clean EOF here — the guest is off the wire.
fn is_unexpected_eof(e: &openssl::ssl::Error) -> bool {
    if e.code() != ErrorCode::SYSCALL {
        return false;
    }
    if let Some(io_err) = e.io_error() {
        matches!(io_err.kind(), ErrorKind::UnexpectedEof | ErrorKind::BrokenPipe)
    } else {
        // SYSCALL with no io_error is typical for "peer closed without
        // close_notify" in OpenSSL 3.x.
        true
    }
}

fn handshake_err(where_: &str, e: openssl::ssl::Error) -> Error {
    // Convert to crate-local Error. Prefer the inner ErrorStack if present;
    // otherwise synthesize an io error.
    if let Some(stack) = e.ssl_error() {
        log::warn!("TlsBridge {} failed: {:?}", where_, stack);
        // Convert ErrorStack → our Error::Crypto via From impl in tls_mitm.
        Error::from(stack.clone())
    } else {
        Error::from(io::Error::new(io::ErrorKind::Other, format!("{}: {}", where_, e)))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tls_mitm::MitmCa;
    use openssl::ssl::{Ssl, SslConnector, SslVerifyMode};
    use std::io::{Read, Write};
    use std::os::unix::net::UnixStream;
    use std::path::PathBuf;
    use std::thread;

    fn fresh_tempdir() -> PathBuf {
        let nanos = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let p = std::env::temp_dir().join(format!("tls_listener_test_{}", nanos));
        std::fs::create_dir_all(&p).unwrap();
        p
    }

    /// End-to-end: classic-cipher client ↔ our legacy acceptor over a
    /// socketpair. Verifies the full handshake + data exchange works and
    /// that a weak-RSA cipher was actually selected.
    fn handshake_round(
        min: SslVersion,
        max: SslVersion,
        client_ciphers: &str,
        expected_cipher_substr: &str,
    ) {
        enable_legacy_algorithms().unwrap();
        let ca = MitmCa::load_or_generate(&fresh_tempdir()).unwrap();
        let ca_cert = X509::from_pem(ca.cert_pem()).unwrap();

        let ctx = build_acceptor_ctx(&ca, "www.example.com").unwrap();

        let (server_io, client_io) = UnixStream::pair().unwrap();

        let server_thread = thread::spawn(move || {
            let ssl = Ssl::new(&ctx).unwrap();
            let mut stream = ssl.accept(server_io).unwrap();
            let mut buf = [0u8; 32];
            let n = stream.read(&mut buf).unwrap();
            assert_eq!(&buf[..n], b"ping");
            stream.write_all(b"pong").unwrap();
            stream.shutdown().ok();
            stream.ssl().current_cipher().map(|c| c.name().to_string())
        });

        let mut cb = SslConnector::builder(SslMethod::tls_client()).unwrap();
        cb.clear_options(openssl::ssl::SslOptions::NO_SSLV3);
        cb.cert_store_mut().add_cert(ca_cert).unwrap();
        cb.set_min_proto_version(Some(min)).unwrap();
        cb.set_max_proto_version(Some(max)).unwrap();
        cb.set_security_level(0);
        cb.set_cipher_list(client_ciphers).unwrap();
        cb.set_verify(SslVerifyMode::PEER);
        let connector = cb.build();
        let mut stream = connector.connect("www.example.com", client_io).unwrap();

        let negotiated = stream
            .ssl()
            .current_cipher()
            .map(|c| c.name().to_string())
            .unwrap_or_default();
        assert!(
            negotiated.contains(expected_cipher_substr),
            "expected cipher matching '{}', got '{}'",
            expected_cipher_substr,
            negotiated
        );

        stream.write_all(b"ping").unwrap();
        let mut buf = [0u8; 32];
        let n = stream.read(&mut buf).unwrap();
        assert_eq!(&buf[..n], b"pong");
        stream.shutdown().ok();

        let server_cipher = server_thread.join().unwrap();
        assert_eq!(server_cipher.as_deref(), Some(negotiated.as_str()));
    }

    /// Full MITM bridge: a classic-SSLv3 client connects through a
    /// `TlsBridge` to a mock upstream TLS server (cert signed by the
    /// same CA). Verifies:
    ///   1. the downstream handshake completes at SSLv3 + RC4-MD5
    ///   2. the upstream handshake completes (modern TLS)
    ///   3. plaintext HTTP flows both directions through the bridge
    #[test]
    fn end_to_end_bridge() {
        use std::net::{TcpListener, TcpStream};
        use std::sync::Arc;
        use std::time::{Duration, Instant};

        enable_legacy_algorithms().unwrap();
        let ca = Arc::new(MitmCa::load_or_generate(&fresh_tempdir()).unwrap());

        // --- Mock upstream TLS server (cert signed by our CA) ---
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let upstream_addr = listener.local_addr().unwrap();
        let ca_for_srv = Arc::clone(&ca);
        let server_jh = thread::spawn(move || {
            let (sock, _) = listener.accept().unwrap();
            let leaf = ca_for_srv.mint_leaf("upstream.test").unwrap();
            let cert = X509::from_pem(&leaf.cert_pem).unwrap();
            let key = PKey::private_key_from_pem(&leaf.key_pem).unwrap();
            let mut b = SslContextBuilder::new(SslMethod::tls_server()).unwrap();
            // The leaf is SHA1/1024-bit RSA — modern OpenSSL rejects
            // loading it unless SECLEVEL is lowered here too.
            b.set_security_level(0);
            b.set_certificate(&cert).unwrap();
            b.set_private_key(&key).unwrap();
            let ctx = b.build();
            let ssl = Ssl::new(&ctx).unwrap();
            let mut s = ssl.accept(sock).unwrap();
            let mut buf = [0u8; 256];
            let n = s.read(&mut buf).unwrap();
            assert!(
                buf[..n].starts_with(b"GET /"),
                "unexpected upstream request: {:?}",
                &buf[..n]
            );
            s.write_all(b"HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nhello")
                .unwrap();
            s.shutdown().ok();
        });

        // --- TlsBridge upstream context: trusts our CA only ---
        let mut us_cb = SslContextBuilder::new(SslMethod::tls_client()).unwrap();
        us_cb
            .cert_store_mut()
            .add_cert(X509::from_pem(ca.cert_pem()).unwrap())
            .unwrap();
        // Same reason as the server side — our test CA is SHA1/1024-bit.
        us_cb.set_security_level(0);
        us_cb.set_verify(SslVerifyMode::PEER);
        let us_ctx = us_cb.build();

        let upstream_tcp = TcpStream::connect(upstream_addr).unwrap();
        let mut bridge = TlsBridge::new_with_upstream_ctx(
            &ca,
            "upstream.test",
            upstream_tcp,
            us_ctx,
        )
        .unwrap();

        // --- Synthetic classic-SSLv3 client on a socketpair ---
        let (client_sock, ferry) = UnixStream::pair().unwrap();
        let ca_for_cli = Arc::clone(&ca);
        let client_jh = thread::spawn(move || {
            let ca_cert = X509::from_pem(ca_for_cli.cert_pem()).unwrap();
            let mut cb = SslConnector::builder(SslMethod::tls_client()).unwrap();
            cb.cert_store_mut().add_cert(ca_cert).unwrap();
            cb.clear_options(SslOptions::NO_SSLV3);
            cb.set_min_proto_version(Some(SslVersion::SSL3)).unwrap();
            cb.set_max_proto_version(Some(SslVersion::SSL3)).unwrap();
            cb.set_security_level(0);
            cb.set_cipher_list("RC4-MD5:@SECLEVEL=0").unwrap();
            cb.set_verify(SslVerifyMode::PEER);
            let connector = cb.build();
            let mut s = connector.connect("upstream.test", client_sock).unwrap();
            let cipher = s.ssl().current_cipher().map(|c| c.name().to_string());
            eprintln!("client: handshake done, cipher={:?}", cipher);
            s.write_all(b"GET / HTTP/1.0\r\n\r\n").unwrap();
            eprintln!("client: wrote GET");
            let mut got = Vec::new();
            let mut buf = [0u8; 512];
            loop {
                match s.read(&mut buf) {
                    Ok(0) => {
                        eprintln!("client: read EOF");
                        break;
                    }
                    Ok(n) => {
                        eprintln!("client: read {} bytes", n);
                        got.extend_from_slice(&buf[..n]);
                        if got.windows(5).any(|w| w == b"hello") {
                            // Server won't close immediately; no point hanging.
                            break;
                        }
                    }
                    Err(e) => {
                        eprintln!("client: read err {:?}", e);
                        break;
                    }
                }
            }
            (got, cipher)
        });

        // --- Ferry: copy bytes between classic-client UnixStream and the
        // bridge's GuestIo while driving the bridge state machine. ---
        ferry.set_nonblocking(true).unwrap();
        let deadline = Instant::now() + Duration::from_secs(10);
        let mut got_eof_from_client = false;
        let mut quiet_ticks = 0usize;
        loop {
            assert!(Instant::now() < deadline, "bridge loop timed out");
            let mut buf = [0u8; 4096];
            let mut did_work = false;
            match (&ferry).read(&mut buf) {
                Ok(0) => {
                    if !got_eof_from_client {
                        bridge.close_downstream();
                        got_eof_from_client = true;
                        did_work = true;
                    }
                }
                Ok(n) => {
                    bridge.feed_from_guest(&buf[..n]);
                    did_work = true;
                }
                Err(e) if e.kind() == ErrorKind::WouldBlock => {}
                Err(e) => panic!("ferry read: {}", e),
            }
            let tick = bridge.drive().unwrap();
            if !tick.bytes_to_guest.is_empty() {
                (&ferry).write_all(&tick.bytes_to_guest).unwrap();
                did_work = true;
            }
            if tick.downstream_eof && tick.upstream_eof {
                break;
            }
            // HTTP/1.0 closes don't always surface as SSL ZERO_RETURN; exit
            // when both handshakes are done, the classic client has closed,
            // and the pipe has been idle for a few ticks.
            if did_work {
                quiet_ticks = 0;
            } else {
                quiet_ticks += 1;
                if got_eof_from_client
                    && bridge.downstream_done
                    && bridge.upstream_done
                    && quiet_ticks > 50
                {
                    break;
                }
            }
            thread::sleep(Duration::from_millis(2));
        }

        let (got, cipher) = client_jh.join().unwrap();
        server_jh.join().unwrap();

        let text = String::from_utf8_lossy(&got);
        assert!(
            text.contains("hello"),
            "expected 'hello' in response, got {:?}",
            text
        );
        assert_eq!(cipher.as_deref(), Some("RC4-MD5"));
    }

    #[test]
    fn rc4_md5_over_sslv3() {
        handshake_round(
            SslVersion::SSL3,
            SslVersion::SSL3,
            "RC4-MD5:@SECLEVEL=0",
            "RC4-MD5",
        );
    }

    #[test]
    fn des_cbc3_sha_over_tls10() {
        handshake_round(
            SslVersion::TLS1,
            SslVersion::TLS1,
            "DES-CBC3-SHA:@SECLEVEL=0",
            "DES-CBC3-SHA",
        );
    }

    #[test]
    fn rc4_sha_over_tls10() {
        handshake_round(
            SslVersion::TLS1,
            SslVersion::TLS1,
            "RC4-SHA:@SECLEVEL=0",
            "RC4-SHA",
        );
    }
}
