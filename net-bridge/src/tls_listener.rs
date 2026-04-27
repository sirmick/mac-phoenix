//! Downstream (guest-facing) TLS acceptor + upstream client.
//!
//! Backed by the vendored OpenSSL 1.0.2u in `net-bridge/vendor/openssl-102-legacy`
//! — the only widely-buildable SSL library that still has SSLv2 + the
//! 40-bit export ciphers that NN3-international-Mac sends. Modern OpenSSL
//! (3.x) and wolfSSL (5.x) both stripped them; we tried both and they
//! can't talk to export-only vintage browsers. See `tools/build-openssl-102.sh`.
//!
//! Cert minting in `tls_mitm.rs` uses the same crate, just the modern
//! parts (RSA-2048, SHA-1 X.509 generation work fine in 1.0.2 too).

use std::collections::VecDeque;
use std::io::{self, ErrorKind, Read, Write};
use std::net::TcpStream;

use openssl::pkey::PKey;
use openssl::ssl::{
    ErrorCode, Ssl, SslContext, SslContextBuilder, SslMethod, SslOptions, SslStream,
    SslVerifyMode,
};
use openssl::x509::X509;

/// `SslMethod::tls_server()` in openssl-rs maps to `TLS_server_method()`,
/// which in OpenSSL 1.0.2 = TLS1.0+ only — it doesn't accept SSLv2 or
/// SSLv3 ClientHellos at all. We need `SSLv23_server_method()` (the
/// any-version method) to talk to NN3. The safe wrapper doesn't expose
/// it, so we drop to FFI for this one call.
fn sslv23_server_method() -> SslMethod {
    unsafe { SslMethod::from_ptr(openssl_sys::SSLv23_server_method()) }
}

use crate::tls_mitm::{Error, MitmCa};

/// Cipher list for the guest-facing acceptor. OpenSSL 1.0.2u +
/// enable-weak-ssl-ciphers + enable-ssl2 brings back everything a
/// vintage Mac browser might offer:
///   - SSLv2 cipher specs (RC4-MD5, RC2-MD5, DES-CBC-MD5, etc.) via `SSLv2`
///   - 40-bit export (EXP-RC4-MD5, EXP-RC2-CBC-MD5, EXP-DES-CBC-SHA) via `EXP`
///   - Single DES via `LOW`
///   - Strong RC4 / 3DES / IDEA / AES (already in DEFAULT/MEDIUM/HIGH)
///
/// `!aNULL:!eNULL` because we always present a real cert. No `@SECLEVEL`
/// because 1.0.2 doesn't have that filter (it was added in 1.1.0).
// Explicit list — no keywords. Matches both NN3-export-Mac (top three)
// and the strong-crypto vintage browsers (NN4 / MSIE 4.5 / NN3 US).
const LEGACY_CIPHERS: &str = concat!(
    "EXP-RC4-MD5:EXP-RC2-CBC-MD5:EXP-DES-CBC-SHA:",
    "RC4-MD5:RC4-SHA:",
    "DES-CBC-SHA:DES-CBC3-SHA:",
    "IDEA-CBC-MD5:IDEA-CBC-SHA:",
    "AES128-SHA:AES256-SHA",
);

/// Build an `SslContext` that presents a freshly minted leaf for
/// `hostname`, signed by `ca`, and accepts any classic-Mac handshake
/// from SSLv2 through TLS 1.2.
pub fn build_acceptor_ctx(ca: &MitmCa, hostname: &str) -> Result<SslContext, Error> {
    let leaf = ca.mint_leaf(hostname)?;
    let cert = X509::from_pem(&leaf.cert_pem)?;
    let key = PKey::private_key_from_pem(&leaf.key_pem)?;

    let mut b = SslContextBuilder::new(sslv23_server_method())?;
    // Clear EVERY restrictive option OpenSSL defaults set on a new SSL_CTX
    // — we want SSLv2-format hellos with v3 capability to negotiate down
    // to whatever weak cipher the client offers. The openssl crate's
    // SslContextBuilder doesn't set anything itself, but OpenSSL 1.0.2's
    // SSLv23_server_method() ships with NO_SSLv2 / NO_SSLv3 set by default
    // since CVE-2014-3566 (POODLE).
    b.clear_options(
        SslOptions::NO_SSLV2
            | SslOptions::NO_SSLV3
            | SslOptions::NO_TLSV1
            | SslOptions::NO_TLSV1_1
            | SslOptions::NO_TLSV1_2
            | SslOptions::ALL,
    );
    b.set_cipher_list(LEGACY_CIPHERS)?;
    b.set_certificate(&cert)?;
    b.set_private_key(&key)?;
    b.check_private_key()?;
    // Classic clients don't do session tickets; suppress noise.
    b.set_options(SslOptions::NO_TICKET);

    // Export-grade ciphers (EXP-RC4-MD5 etc.) need an ephemeral 512-bit
    // RSA key for key exchange — the cert key (RSA-2048) is too large to
    // serve directly per US-export rules. Without this callback, OpenSSL
    // silently drops every EXP cipher from the negotiable list, leaving
    // an export-only client (NN3-international-Mac) with no shared
    // cipher. `s_server` registers an equivalent callback by default; the
    // openssl-rs crate doesn't expose it, so we go through openssl-sys.
    install_tmp_rsa(&mut b);

    Ok(b.build())
}

// Callback for SSL_CTX_set_tmp_rsa_callback. Called by OpenSSL on demand
// when a handshake selects an RSA-EXPORT cipher and the cert key is too
// large to use directly. We generate a fresh 512-bit RSA key per call —
// not hot-path code (export ciphers are rare even with vintage clients).
extern "C" fn tmp_rsa_cb(
    _ssl: *mut openssl_sys::SSL,
    _is_export: std::os::raw::c_int,
    keylen: std::os::raw::c_int,
) -> *mut openssl_sys::RSA {
    unsafe {
        let bn = openssl_sys::BN_new();
        if bn.is_null() {
            return std::ptr::null_mut();
        }
        openssl_sys::BN_set_word(bn, 65537);
        let rsa = openssl_sys::RSA_new();
        if rsa.is_null() {
            openssl_sys::BN_free(bn);
            return std::ptr::null_mut();
        }
        let ok = openssl_sys::RSA_generate_key_ex(
            rsa, keylen, bn, std::ptr::null_mut(),
        );
        openssl_sys::BN_free(bn);
        if ok != 1 {
            openssl_sys::RSA_free(rsa);
            return std::ptr::null_mut();
        }
        rsa
    }
}

fn install_tmp_rsa(b: &mut SslContextBuilder) {
    // SSL_CTX_set_tmp_rsa_callback macro = SSL_CTX_callback_ctrl(ctx,
    // SSL_CTRL_SET_TMP_RSA_CB, (void(*)())cb). openssl-sys still exposes
    // SSL_CTX_callback_ctrl as a shim. The callback approach matches what
    // `openssl s_server` does; the static-key approach (SET_TMP_RSA ctrl)
    // didn't move the needle in our setup for reasons unclear.
    const SSL_CTRL_SET_TMP_RSA_CB: i32 = 5;
    unsafe {
        // Erase the typed callback through a generic fn() pointer.
        let cb_ptr: extern "C" fn() = std::mem::transmute(
            tmp_rsa_cb as extern "C" fn(*mut _, _, _) -> *mut _,
        );
        openssl_sys::SSL_CTX_callback_ctrl(
            b.as_ptr(),
            SSL_CTRL_SET_TMP_RSA_CB,
            Some(cb_ptr),
        );
    }
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
        // Diagnostic: dump the first bytes the guest sends so we can
        // identify what cipher set its ClientHello offered. Only logs
        // until the inbound queue first hits 80 bytes (one TLS hello
        // worth) — keeps the log clean post-handshake.
        if self.inbound.len() < 80 && !data.is_empty() {
            let take = data.len().min(80);
            let mut hex = String::with_capacity(take * 3);
            for b in &data[..take] {
                use std::fmt::Write;
                let _ = write!(hex, "{:02x} ", b);
            }
            log::info!("MITM downstream first bytes ({}): {}", data.len(), hex.trim());
        }
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
            return Err(io::Error::from(ErrorKind::WouldBlock));
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

/// Build a modern TLS client context for the upstream hop.
///
/// Trust store: our vendored OpenSSL 1.0.2u was built with --openssldir
/// pointing at vendor/openssl-102-legacy/ssl/, which ships no CA bundle.
/// `set_default_verify_paths` would point there and find nothing → every
/// real-world cert verification fails. Point at the OS trust store
/// instead. Falls through a few common paths so we work on Debian/Ubuntu
/// (/etc/ssl/certs/ca-certificates.crt), Fedora/RHEL (/etc/pki/tls/certs/
/// ca-bundle.crt), and a couple other distros.
pub fn build_upstream_ctx() -> Result<SslContext, Error> {
    let mut b = SslContextBuilder::new(SslMethod::tls_client())?;
    let ca_file = system_ca_bundle_path();
    if let Some(path) = &ca_file {
        b.set_ca_file(path)?;
        log::debug!("upstream CTX: trusting system CAs from {}", path);
    } else {
        // Fall back to OpenSSL's compile-time default (empty for vendored).
        b.set_default_verify_paths()?;
        log::warn!(
            "upstream CTX: no system CA bundle found at any known path; \
             upstream cert verification will likely fail"
        );
    }
    b.set_verify(SslVerifyMode::PEER);
    Ok(b.build())
}

/// Find a usable system-wide PEM-format CA bundle.
fn system_ca_bundle_path() -> Option<String> {
    // Allow override via env for unusual distros / containers.
    if let Ok(p) = std::env::var("SSL_CERT_FILE") {
        if std::path::Path::new(&p).exists() {
            return Some(p);
        }
    }
    for candidate in &[
        "/etc/ssl/certs/ca-certificates.crt",       // Debian, Ubuntu, Alpine
        "/etc/pki/tls/certs/ca-bundle.crt",          // Fedora, RHEL, CentOS
        "/etc/ssl/ca-bundle.pem",                    // OpenSUSE
        "/etc/pki/tls/cacert.pem",                   // OpenELEC
        "/etc/ssl/cert.pem",                         // macOS, Alpine
    ] {
        if std::path::Path::new(candidate).exists() {
            return Some((*candidate).to_string());
        }
    }
    None
}

/// Upstream slot — handles deferred construction for SNI-less downstream
/// connections (NN3, iCab old, anything that sends a v2-format hello). When
/// the guest didn't give us an SNI, we wait until the downstream handshake
/// finishes and we can read the HTTP Host: header from the decrypted request,
/// then build the upstream SslStream with the proper SNI. Cloudflare and
/// other shared-IP HTTPS hosts reject SNI-less connections with alert 40,
/// so this matters.
enum UpstreamSlot {
    /// SNI was known up-front (sniffed from a modern TLS hello, or an IP
    /// literal where we expect default-cert behaviour). Stream is ready
    /// to handshake immediately.
    Ready(SslStream<TcpStream>),
    /// SNI unknown. Hold the raw TCP + ctx; build the SslStream once we
    /// extract Host: from plaintext.
    Pending {
        tcp: Option<TcpStream>, // Option so we can take() it on transition
        ctx: SslContext,
    },
}

/// Per-connection TLS MITM state: downstream (guest-facing, legacy) +
/// upstream (host-facing, modern), joined plaintext-to-plaintext.
pub struct TlsBridge {
    downstream: SslStream<GuestIo>,
    downstream_done: bool,
    upstream: UpstreamSlot,
    upstream_done: bool,
    /// Plaintext waiting to be re-encrypted upstream once upstream handshake
    /// completes. Also serves as the buffer we scan for Host: when upstream
    /// is in Pending state.
    pending_to_upstream: Vec<u8>,
    /// Plaintext from upstream waiting to be re-encrypted downstream once
    /// downstream handshake completes.
    pending_to_downstream: Vec<u8>,
    /// Original dst IP literal — used as a last-resort SNI/CN if the guest
    /// never sends a Host: header (non-HTTP TLS, or after the timeout).
    fallback_host: String,
}

#[derive(Debug, Default)]
pub struct BridgeTick {
    pub bytes_to_guest: Vec<u8>,
    pub handshake_done: bool,
    pub upstream_eof: bool,
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
        let ds_ctx = build_acceptor_ctx(ca, hostname)?;
        let mut ds_ssl = Ssl::new(&ds_ctx)?;
        ds_ssl.set_accept_state();
        let downstream = SslStream::new(ds_ssl, GuestIo::default())?;
        upstream_tcp.set_nonblocking(true).ok();

        let upstream = if hostname.parse::<std::net::Ipv4Addr>().is_err() {
            // SNI was sniffed from the guest hello. Build upstream now.
            let mut us_ssl = Ssl::new(&us_ctx)?;
            us_ssl.set_connect_state();
            us_ssl.set_hostname(hostname)?;
            us_ssl.param_mut().set_host(hostname)?;
            UpstreamSlot::Ready(SslStream::new(us_ssl, upstream_tcp)?)
        } else {
            // No SNI from guest. Defer upstream until we read Host: from
            // the decrypted HTTP request. Common for NN3 / iCab-old / any
            // browser sending an SSLv2-format hello.
            log::debug!(
                "TlsBridge: no SNI from guest for {}; deferring upstream \
                 connect until Host: header arrives",
                hostname
            );
            UpstreamSlot::Pending {
                tcp: Some(upstream_tcp),
                ctx: us_ctx,
            }
        };

        Ok(Self {
            downstream,
            downstream_done: false,
            upstream,
            upstream_done: false,
            pending_to_upstream: Vec::new(),
            pending_to_downstream: Vec::new(),
            fallback_host: hostname.to_string(),
        })
    }

    /// Promote a Pending upstream to Ready. Called once we've seen the
    /// HTTP Host: header in the decrypted plaintext (or after a small
    /// threshold elapsed without one — non-HTTP traffic).
    fn realize_upstream(&mut self, sni_host: &str) -> Result<(), Error> {
        let (tcp, ctx) = match &mut self.upstream {
            UpstreamSlot::Pending { tcp, ctx } => {
                let t = tcp.take().expect("pending tcp already taken");
                (t, ctx.clone())
            }
            UpstreamSlot::Ready(_) => return Ok(()), // already promoted
        };
        let mut us_ssl = Ssl::new(&ctx)?;
        us_ssl.set_connect_state();
        // Only set SNI if it's a hostname, not an IP literal — Cloudflare
        // accepts SNI=hostname; SNI=ip-literal returns alert 40 anyway.
        if sni_host.parse::<std::net::Ipv4Addr>().is_err() && !sni_host.is_empty() {
            us_ssl.set_hostname(sni_host)?;
            us_ssl.param_mut().set_host(sni_host)?;
            log::info!("TlsBridge: upstream SNI promoted to '{}'", sni_host);
        } else {
            log::warn!(
                "TlsBridge: no usable Host:; upstream connect without SNI \
                 (server may reject)"
            );
        }
        let stream = SslStream::new(us_ssl, tcp)?;
        self.upstream = UpstreamSlot::Ready(stream);
        Ok(())
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

        // 1a. Always advance the downstream handshake first — it's the side
        // we control and it doesn't depend on upstream state.
        if !self.downstream_done {
            match self.downstream.do_handshake() {
                Ok(()) => {
                    self.downstream_done = true;
                    log::info!(
                        "TlsBridge downstream handshake complete: cipher={:?} version={:?}",
                        self.downstream.ssl().current_cipher().map(|c| c.name()),
                        self.downstream.ssl().version_str(),
                    );
                }
                Err(e) => match e.code() {
                    ErrorCode::WANT_READ | ErrorCode::WANT_WRITE => {}
                    _ => return Err(handshake_err("downstream", e)),
                },
            }
        }

        // 1b. Read plaintext from downstream → queue for upstream. We do
        // this BEFORE the upstream handshake when upstream is Pending, so
        // we accumulate enough bytes to find the Host: header.
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

        // 1c. If upstream is Pending, try to extract Host: header now and
        // promote to Ready. Promote unconditionally once we've seen the
        // end of the HTTP request headers, OR after 4096 bytes (assume
        // non-HTTP and use fallback host).
        if matches!(self.upstream, UpstreamSlot::Pending { .. }) && self.downstream_done {
            let host = extract_http_host(&self.pending_to_upstream);
            let promote = host.is_some()
                || self.pending_to_upstream.windows(4).any(|w| w == b"\r\n\r\n")
                || self.pending_to_upstream.len() >= 4096;
            if promote {
                let sni = host.unwrap_or_else(|| self.fallback_host.clone());
                self.realize_upstream(&sni)?;
            }
        }

        // 1d. Advance upstream handshake (only if Ready).
        if !self.upstream_done {
            if let UpstreamSlot::Ready(stream) = &mut self.upstream {
                match stream.do_handshake() {
                    Ok(()) => {
                        self.upstream_done = true;
                        log::info!(
                            "TlsBridge upstream handshake complete: cipher={:?} version={:?}",
                            stream.ssl().current_cipher().map(|c| c.name()),
                            stream.ssl().version_str(),
                        );
                    }
                    Err(e) => match e.code() {
                        ErrorCode::WANT_READ | ErrorCode::WANT_WRITE => {}
                        _ => return Err(handshake_err("upstream", e)),
                    },
                }
            }
        }

        // 2. Flush pending plaintext upstream.
        if self.upstream_done && !self.pending_to_upstream.is_empty() {
            let payload = std::mem::take(&mut self.pending_to_upstream);
            if let UpstreamSlot::Ready(stream) = &mut self.upstream {
                match stream.ssl_write(&payload) {
                    Ok(n) if n == payload.len() => {}
                    Ok(n) => self.pending_to_upstream = payload[n..].to_vec(),
                    Err(ref e) if e.code() == ErrorCode::WANT_READ
                        || e.code() == ErrorCode::WANT_WRITE =>
                    {
                        self.pending_to_upstream = payload;
                    }
                    Err(e) => return Err(handshake_err("upstream write", e)),
                }
            }
        }

        // 3. Read plaintext from upstream → queue for downstream.
        if self.upstream_done {
            if let UpstreamSlot::Ready(stream) = &mut self.upstream {
                let mut buf = [0u8; 4096];
                loop {
                    match stream.ssl_read(&mut buf) {
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
        }

        // 4. Flush plaintext to downstream.
        if self.downstream_done && !self.pending_to_downstream.is_empty() {
            let payload = std::mem::take(&mut self.pending_to_downstream);
            match self.downstream.ssl_write(&payload) {
                Ok(n) if n == payload.len() => {}
                Ok(n) => self.pending_to_downstream = payload[n..].to_vec(),
                Err(ref e) if e.code() == ErrorCode::WANT_READ
                    || e.code() == ErrorCode::WANT_WRITE =>
                {
                    self.pending_to_downstream = payload;
                }
                Err(e) => return Err(handshake_err("downstream write", e)),
            }
        }

        // 5. Drain ciphertext queued for the guest.
        tick.bytes_to_guest = self.downstream.get_mut().take_to_guest();
        tick.handshake_done = self.downstream_done && self.upstream_done;
        Ok(tick)
    }
}

/// Find the HTTP `Host: <name>` header in a buffer of decrypted plaintext.
/// Strips port if present (`example.com:443` → `example.com`). Returns
/// None if headers aren't complete yet OR if no Host: line is present.
fn extract_http_host(buf: &[u8]) -> Option<String> {
    let s = std::str::from_utf8(buf).ok()?;
    for line in s.split("\r\n") {
        if line.is_empty() {
            // End of headers reached without Host:.
            return None;
        }
        let lower_prefix = line.get(..6).map(|p| p.eq_ignore_ascii_case("host: ")).unwrap_or(false);
        if lower_prefix {
            let host = line[6..].trim();
            // Strip port suffix if present.
            let host = host.split(':').next().unwrap_or(host);
            if !host.is_empty() {
                return Some(host.to_string());
            }
        }
    }
    None
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
        true
    }
}

fn handshake_err(where_: &str, e: openssl::ssl::Error) -> Error {
    if let Some(stack) = e.ssl_error() {
        log::warn!("TlsBridge {} failed: {:?}", where_, stack);
        Error::from(stack.clone())
    } else {
        Error::from(io::Error::new(io::ErrorKind::Other, format!("{}: {}", where_, e)))
    }
}

#[cfg(test)]
mod nn3_repro {
    //! Feed NN3-export-Mac's actual ClientHello bytes into a fresh SSL
    //! context configured exactly the way build_acceptor_ctx does, see
    //! what comes out. Run with: `cargo test nn3_export -- --nocapture`.
    use super::*;
    use openssl::asn1::Asn1Time;
    use openssl::bn::{BigNum, MsbOption};
    use openssl::hash::MessageDigest;
    use openssl::rsa::Rsa;
    use openssl::x509::extension::{BasicConstraints, KeyUsage, SubjectAlternativeName};
    use openssl::x509::{X509Builder, X509NameBuilder};
    use std::io::{Read, Write};

    #[test]
    fn nn3_export_clienthello_repro() {
        // Self-signed RSA-2048 cert (same shape as MitmCa::mint_leaf).
        let rsa = Rsa::generate(2048).unwrap();
        let key = PKey::from_rsa(rsa).unwrap();
        let mut nb = X509NameBuilder::new().unwrap();
        nb.append_entry_by_text("CN", "test.example").unwrap();
        let n = nb.build();
        let mut cb = X509Builder::new().unwrap();
        cb.set_version(2).unwrap();
        cb.set_subject_name(&n).unwrap();
        cb.set_issuer_name(&n).unwrap();
        cb.set_pubkey(&key).unwrap();
        let mut bn = BigNum::new().unwrap();
        bn.rand(64, MsbOption::MAYBE_ZERO, false).unwrap();
        cb.set_serial_number(&bn.to_asn1_integer().unwrap()).unwrap();
        cb.set_not_before(&Asn1Time::days_from_now(0).unwrap()).unwrap();
        cb.set_not_after(&Asn1Time::days_from_now(30).unwrap()).unwrap();
        cb.append_extension(BasicConstraints::new().critical().build().unwrap()).unwrap();
        cb.append_extension(KeyUsage::new().critical().digital_signature().key_encipherment().build().unwrap()).unwrap();
        let san = SubjectAlternativeName::new().dns("test.example").build(&cb.x509v3_context(None, None)).unwrap();
        cb.append_extension(san).unwrap();
        cb.sign(&key, MessageDigest::sha1()).unwrap();
        let cert = cb.build();

        let mut b = SslContextBuilder::new(sslv23_server_method()).unwrap();
        b.clear_options(
            SslOptions::NO_SSLV2
                | SslOptions::NO_SSLV3
                | SslOptions::NO_TLSV1
                | SslOptions::NO_TLSV1_1
                | SslOptions::NO_TLSV1_2
                | SslOptions::ALL,
        );
        b.set_cipher_list(LEGACY_CIPHERS).unwrap();
        b.set_certificate(&cert).unwrap();
        b.set_private_key(&key).unwrap();
        b.check_private_key().unwrap();
        b.set_options(SslOptions::NO_TICKET);
        install_tmp_rsa(&mut b);
        let ctx = b.build();

        let nn3_hello: Vec<u8> = vec![
            0x80, 0x28, 0x01, 0x03, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x10,
            0x04, 0x00, 0x80, 0x02, 0x00, 0x80, 0x00, 0x00, 0x03, 0x00, 0x00,
            0x06, 0x00, 0x00, 0x01, 0xa6, 0x99, 0x31, 0xa9, 0xf0, 0xf4, 0x0c,
            0x66, 0x89, 0x4d, 0xe2, 0x71, 0xe1, 0x86, 0x6c, 0xc6,
        ];

        struct Pipe {
            inbound: std::collections::VecDeque<u8>,
            outbound: std::collections::VecDeque<u8>,
        }
        impl Read for Pipe {
            fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
                if self.inbound.is_empty() {
                    return Err(std::io::Error::from(std::io::ErrorKind::WouldBlock));
                }
                let n = buf.len().min(self.inbound.len());
                for s in buf.iter_mut().take(n) {
                    *s = self.inbound.pop_front().unwrap();
                }
                Ok(n)
            }
        }
        impl Write for Pipe {
            fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
                self.outbound.extend(buf);
                Ok(buf.len())
            }
            fn flush(&mut self) -> std::io::Result<()> { Ok(()) }
        }

        let pipe = Pipe { inbound: nn3_hello.into(), outbound: Default::default() };

        let mut ssl = Ssl::new(&ctx).unwrap();
        ssl.set_accept_state();
        let mut stream = SslStream::new(ssl, pipe).unwrap();

        match stream.do_handshake() {
            Ok(_) => {
                println!("HANDSHAKE OK: cipher={:?} version={:?}",
                    stream.ssl().current_cipher().map(|c| c.name().to_string()),
                    stream.ssl().version_str());
            }
            Err(e) => {
                println!("HANDSHAKE ERR: code={:?}", e.code());
                if let Some(stack) = e.ssl_error() {
                    for err in stack.errors() {
                        println!("  reason: {} ({})",
                            err.reason().unwrap_or("?"), err.code());
                    }
                }
                let pipe = stream.get_ref();
                println!("outbound bytes from server: {}", pipe.outbound.len());
                if !pipe.outbound.is_empty() {
                    print!("  first 32: ");
                    for b in pipe.outbound.iter().take(32) {
                        print!("{:02x} ", b);
                    }
                    println!();
                }
            }
        }
    }
}
