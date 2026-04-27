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
    SslVerifyMode, SslVersion,
};
use openssl::x509::X509;

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
const LEGACY_CIPHERS: &str = "ALL:SSLv2:EXP:!aNULL:!eNULL";

/// Build an `SslContext` that presents a freshly minted leaf for
/// `hostname`, signed by `ca`, and accepts any classic-Mac handshake
/// from SSLv2 through TLS 1.2.
pub fn build_acceptor_ctx(ca: &MitmCa, hostname: &str) -> Result<SslContext, Error> {
    let leaf = ca.mint_leaf(hostname)?;
    let cert = X509::from_pem(&leaf.cert_pem)?;
    let key = PKey::private_key_from_pem(&leaf.key_pem)?;

    let mut b = SslContextBuilder::new(SslMethod::tls_server())?;
    // `SslMethod::tls_server()` on 1.0.2 = SSLv23 — accepts any version.
    // The crate sets a bunch of NO_xxx options by default; clear all the
    // vintage ones so SSLv2 / SSLv3 / TLS1.0 ClientHellos are accepted.
    b.clear_options(SslOptions::NO_SSLV2);
    b.clear_options(SslOptions::NO_SSLV3);
    b.clear_options(SslOptions::NO_TLSV1);
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

/// Build a modern TLS client context for the upstream hop. Validates
/// against the system trust store via the openssl crate's default verify
/// paths.
pub fn build_upstream_ctx() -> Result<SslContext, Error> {
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
    /// completes.
    pending_to_upstream: Vec<u8>,
    /// Plaintext from upstream waiting to be re-encrypted downstream once
    /// downstream handshake completes.
    pending_to_downstream: Vec<u8>,
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

        let mut us_ssl = Ssl::new(&us_ctx)?;
        us_ssl.set_connect_state();
        // SSLv2/3 have no SNI; if the guest didn't give us one we fell
        // back to the IP literal as `hostname`. Sending that as upstream
        // SNI is wrong (the server keys on the hostname). Skip it for IP
        // literals; the chain still validates.
        if hostname.parse::<std::net::Ipv4Addr>().is_err() {
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

        // 1. Advance handshakes where possible.
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
        if !self.upstream_done {
            match self.upstream.do_handshake() {
                Ok(()) => {
                    self.upstream_done = true;
                    log::info!(
                        "TlsBridge upstream handshake complete: cipher={:?} version={:?}",
                        self.upstream.ssl().current_cipher().map(|c| c.name()),
                        self.upstream.ssl().version_str(),
                    );
                }
                Err(e) => match e.code() {
                    ErrorCode::WANT_READ | ErrorCode::WANT_WRITE => {}
                    _ => return Err(handshake_err("upstream", e)),
                },
            }
        }

        // 2. Read plaintext from downstream → queue for upstream.
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

        // 3. Flush pending plaintext upstream.
        if self.upstream_done && !self.pending_to_upstream.is_empty() {
            let payload = std::mem::take(&mut self.pending_to_upstream);
            match self.upstream.ssl_write(&payload) {
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

        // 4. Read plaintext from upstream → queue for downstream.
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
                Ok(n) => self.pending_to_downstream = payload[n..].to_vec(),
                Err(ref e) if e.code() == ErrorCode::WANT_READ
                    || e.code() == ErrorCode::WANT_WRITE =>
                {
                    self.pending_to_downstream = payload;
                }
                Err(e) => return Err(handshake_err("downstream write", e)),
            }
        }

        // 6. Drain ciphertext queued for the guest.
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
