//! TLS MITM CA and leaf-certificate minter.
//!
//! Generates (or loads) a local root CA, then on demand mints short-lived
//! leaf certs for arbitrary hostnames, signed by that CA. All certificates
//! are RSA-1024 + SHA1-RSA so that classic Mac SSL clients (MSIE 4.5,
//! Netscape 3.x/4.x) can parse and validate them.
//!
//! The CA cert is the trust anchor the guest must import once. Only the
//! public cert needs to leave the host.

use std::collections::BTreeSet;
use std::fs;
use std::io;
use std::net::Ipv4Addr;
use std::path::{Path, PathBuf};
use std::sync::Arc;

use crate::sni::{peek_sni, SniPeek};

use openssl::asn1::Asn1Time;
use openssl::bn::{BigNum, MsbOption};
use openssl::error::ErrorStack;
use openssl::hash::MessageDigest;
use openssl::pkey::{PKey, Private};
use openssl::rsa::Rsa;
use openssl::x509::extension::{
    BasicConstraints, KeyUsage, SubjectAlternativeName, SubjectKeyIdentifier,
};
use openssl::x509::{X509, X509Builder, X509NameBuilder};

const DEFAULT_MITM_PORTS: &[u16] = &[443];
const DEFAULT_CA_DIR: &str = ".mitm_ca";

/// Static MITM configuration parsed from CLI / env.
#[derive(Debug, Clone)]
pub struct MitmConfig {
    pub enabled: bool,
    pub ports: BTreeSet<u16>,
    pub ca_dir: PathBuf,
}

impl Default for MitmConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            ports: DEFAULT_MITM_PORTS.iter().copied().collect(),
            ca_dir: PathBuf::from(DEFAULT_CA_DIR),
        }
    }
}

impl MitmConfig {
    /// Parse `--mitm-tls`, `--mitm-ports a,b,c`, `--mitm-ca-dir PATH` out of
    /// an argv iterator. Unknown args are ignored (main.rs handles those).
    pub fn from_args<I: IntoIterator<Item = String>>(args: I) -> Result<Self, String> {
        let mut cfg = Self::default();
        let mut iter = args.into_iter();
        while let Some(a) = iter.next() {
            match a.as_str() {
                "--mitm-tls" => cfg.enabled = true,
                "--mitm-ports" => {
                    let list = iter.next().ok_or("--mitm-ports needs an argument")?;
                    cfg.ports = parse_ports(&list)?;
                }
                "--mitm-ca-dir" => {
                    let d = iter.next().ok_or("--mitm-ca-dir needs an argument")?;
                    cfg.ca_dir = PathBuf::from(d);
                }
                _ => {}
            }
        }
        Ok(cfg)
    }

    pub fn matches(&self, dst_port: u16) -> bool {
        self.enabled && self.ports.contains(&dst_port)
    }
}

fn parse_ports(s: &str) -> Result<BTreeSet<u16>, String> {
    let mut out = BTreeSet::new();
    for tok in s.split(',').map(str::trim).filter(|t| !t.is_empty()) {
        let p: u16 = tok.parse().map_err(|_| format!("bad port '{}'", tok))?;
        out.insert(p);
    }
    if out.is_empty() {
        return Err("--mitm-ports must list at least one port".into());
    }
    Ok(out)
}

/// Live MITM state: config + loaded CA. `None` when MITM is disabled.
pub struct MitmRuntime {
    pub config: MitmConfig,
    pub ca: Arc<MitmCa>,
}

impl MitmRuntime {
    pub fn from_config(config: MitmConfig) -> Result<Option<Self>, Error> {
        if !config.enabled {
            return Ok(None);
        }
        let ca = MitmCa::load_or_generate(&config.ca_dir)?;
        log::info!(
            "MITM TLS enabled: ports={:?} ca={}",
            config.ports, config.ca_dir.display()
        );
        Ok(Some(Self { config, ca: Arc::new(ca) }))
    }

    pub fn matches(&self, dst_port: u16) -> bool {
        self.config.matches(dst_port)
    }
}

/// Per-connection MITM state. Lives next to the TCP conn in tcp_proxy.
///
/// Phase 1 (`Sniffing`): accumulate guest→host bytes until we can decide
/// SNI. Caller keeps passing data via [`observe_guest_bytes`] and acts on
/// the returned state transition.
///
/// Phase 2 (M4+): once SNI is known, the pipe stops pass-through and
/// swaps in real TLS termination on both sides.
pub struct MitmSession {
    pub dst_ip: Ipv4Addr,
    pub dst_port: u16,
    buf: Vec<u8>,
    state: MitmState,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum MitmState {
    Sniffing,
    /// SNI extracted from ClientHello.
    SniKnown(String),
    /// ClientHello parsed successfully but carried no SNI (SSLv3).
    /// Caller picks a fallback (reverse DNS, dst IP literal).
    SniAbsent,
    /// Bytes did not look like TLS; abandon MITM, stay in raw pass-through.
    Abandoned,
}

/// Upper bound on how many bytes we'll buffer while hunting for the
/// ClientHello. Real ones are under 1 KiB; this is a safety net.
const SNIFF_BUFFER_CAP: usize = 16 * 1024;

impl MitmSession {
    pub fn new(dst_ip: Ipv4Addr, dst_port: u16) -> Self {
        Self {
            dst_ip,
            dst_port,
            buf: Vec::with_capacity(512),
            state: MitmState::Sniffing,
        }
    }

    pub fn state(&self) -> &MitmState {
        &self.state
    }

    /// Feed bytes flowing from guest → host. Returns `Some(&MitmState)`
    /// exactly once, at the transition out of [`MitmState::Sniffing`].
    /// Later calls while already resolved return `None`.
    pub fn observe_guest_bytes(&mut self, data: &[u8]) -> Option<&MitmState> {
        if self.state != MitmState::Sniffing {
            return None;
        }
        if self.buf.len() + data.len() > SNIFF_BUFFER_CAP {
            self.state = MitmState::Abandoned;
            return Some(&self.state);
        }
        self.buf.extend_from_slice(data);
        match peek_sni(&self.buf) {
            SniPeek::Found(h) => {
                self.state = MitmState::SniKnown(h);
                Some(&self.state)
            }
            SniPeek::NotPresent => {
                self.state = MitmState::SniAbsent;
                Some(&self.state)
            }
            SniPeek::NotTls => {
                self.state = MitmState::Abandoned;
                Some(&self.state)
            }
            SniPeek::NeedMore => None,
        }
    }

    /// Hostname the MITM should use as server_name for the upstream
    /// handshake and as CN for the minted leaf. `None` if MITM has been
    /// abandoned or is still sniffing.
    pub fn resolved_host(&self) -> Option<String> {
        match &self.state {
            MitmState::SniKnown(h) => Some(h.clone()),
            MitmState::SniAbsent => Some(self.dst_ip.to_string()),
            _ => None,
        }
    }

    /// Take the bytes accumulated during sniffing so they can be replayed
    /// into the downstream SSL stream. Empties the internal buffer.
    pub fn take_buffer(&mut self) -> Vec<u8> {
        std::mem::take(&mut self.buf)
    }
}

const CA_KEY_FILE: &str = "mitm_ca.key";
const CA_CERT_FILE: &str = "mitm_ca.crt";

const CA_BITS: u32 = 2048;
const LEAF_BITS: u32 = 1024;
const CA_VALID_DAYS: u32 = 3650;
const LEAF_VALID_DAYS: u32 = 825;

#[derive(Debug)]
pub enum Error {
    Io(io::Error),
    Crypto(ErrorStack),
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Error::Io(e) => write!(f, "io: {}", e),
            Error::Crypto(e) => write!(f, "crypto: {}", e),
        }
    }
}

impl std::error::Error for Error {}

impl From<io::Error> for Error {
    fn from(e: io::Error) -> Self {
        Error::Io(e)
    }
}

impl From<ErrorStack> for Error {
    fn from(e: ErrorStack) -> Self {
        Error::Crypto(e)
    }
}

/// In-memory root CA used to mint MITM leaf certs.
pub struct MitmCa {
    key: PKey<Private>,
    cert: X509,
    cert_pem: Vec<u8>,
    cert_der: Vec<u8>,
    cert_path: PathBuf,
}

/// A freshly minted leaf cert + its matching private key.
/// PEM is for tooling/inspection; DER is for feeding into OpenSSL SSL_CTX.
pub struct MintedCert {
    pub cert_pem: Vec<u8>,
    pub cert_der: Vec<u8>,
    pub key_pem: Vec<u8>,
    pub key_der: Vec<u8>,
}

impl MitmCa {
    /// Load the CA from `dir`, generating and persisting it if absent.
    pub fn load_or_generate(dir: &Path) -> Result<Self, Error> {
        fs::create_dir_all(dir)?;
        let key_path = dir.join(CA_KEY_FILE);
        let cert_path = dir.join(CA_CERT_FILE);
        if key_path.exists() && cert_path.exists() {
            Self::load(&key_path, &cert_path)
        } else {
            Self::generate(&key_path, &cert_path)
        }
    }

    fn load(key_path: &Path, cert_path: &Path) -> Result<Self, Error> {
        let key_pem = fs::read(key_path)?;
        let cert_pem = fs::read(cert_path)?;
        let key = PKey::private_key_from_pem(&key_pem)?;
        let cert = X509::from_pem(&cert_pem)?;
        let cert_der = cert.to_der()?;
        Ok(Self {
            key,
            cert,
            cert_pem,
            cert_der,
            cert_path: cert_path.to_path_buf(),
        })
    }

    fn generate(key_path: &Path, cert_path: &Path) -> Result<Self, Error> {
        let rsa = Rsa::generate(CA_BITS)?;
        let key = PKey::from_rsa(rsa)?;

        let mut name_b = X509NameBuilder::new()?;
        name_b.append_entry_by_text("CN", "MacPhoenix MITM Root CA")?;
        name_b.append_entry_by_text("O", "MacPhoenix")?;
        let name = name_b.build();

        let mut b = X509Builder::new()?;
        b.set_version(2)?; // X.509 v3 (zero-indexed)
        b.set_subject_name(&name)?;
        b.set_issuer_name(&name)?;
        b.set_pubkey(&key)?;
        let serial = random_serial()?;
        b.set_serial_number(&serial)?;
        let not_before = Asn1Time::days_from_now(0)?;
        let not_after = Asn1Time::days_from_now(CA_VALID_DAYS)?;
        b.set_not_before(&not_before)?;
        b.set_not_after(&not_after)?;

        b.append_extension(BasicConstraints::new().critical().ca().build()?)?;
        b.append_extension(
            KeyUsage::new()
                .critical()
                .key_cert_sign()
                .crl_sign()
                .build()?,
        )?;
        let ski = SubjectKeyIdentifier::new().build(&b.x509v3_context(None, None))?;
        b.append_extension(ski)?;

        // SHA1 so classic clients that walk the chain up to this anchor can
        // still parse our own self-signature if they try.
        b.sign(&key, MessageDigest::sha1())?;
        let cert = b.build();

        let key_pem = key.private_key_to_pem_pkcs8()?;
        let cert_pem = cert.to_pem()?;
        let cert_der = cert.to_der()?;

        fs::write(key_path, &key_pem)?;
        fs::write(cert_path, &cert_pem)?;
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(key_path, fs::Permissions::from_mode(0o600))?;
        }

        Ok(Self {
            key,
            cert,
            cert_pem,
            cert_der,
            cert_path: cert_path.to_path_buf(),
        })
    }

    pub fn cert_pem(&self) -> &[u8] {
        &self.cert_pem
    }

    pub fn cert_der(&self) -> &[u8] {
        &self.cert_der
    }

    pub fn cert_path(&self) -> &Path {
        &self.cert_path
    }

    /// Mint a leaf cert for `hostname` (DNS name or IPv4 literal).
    pub fn mint_leaf(&self, hostname: &str) -> Result<MintedCert, Error> {
        let rsa = Rsa::generate(LEAF_BITS)?;
        let key = PKey::from_rsa(rsa)?;

        let mut name_b = X509NameBuilder::new()?;
        name_b.append_entry_by_text("CN", hostname)?;
        let name = name_b.build();

        let mut b = X509Builder::new()?;
        b.set_version(2)?;
        b.set_subject_name(&name)?;
        b.set_issuer_name(self.cert.subject_name())?;
        b.set_pubkey(&key)?;
        let serial = random_serial()?;
        b.set_serial_number(&serial)?;
        let not_before = Asn1Time::days_from_now(0)?;
        let not_after = Asn1Time::days_from_now(LEAF_VALID_DAYS)?;
        b.set_not_before(&not_before)?;
        b.set_not_after(&not_after)?;

        b.append_extension(BasicConstraints::new().critical().build()?)?;
        b.append_extension(
            KeyUsage::new()
                .critical()
                .digital_signature()
                .key_encipherment()
                .build()?,
        )?;

        let mut san = SubjectAlternativeName::new();
        if hostname.parse::<Ipv4Addr>().is_ok() {
            san.ip(hostname);
        } else {
            san.dns(hostname);
        }
        let san_ext = san.build(&b.x509v3_context(Some(&self.cert), None))?;
        b.append_extension(san_ext)?;

        b.sign(&self.key, MessageDigest::sha1())?;
        let cert = b.build();

        Ok(MintedCert {
            cert_pem: cert.to_pem()?,
            cert_der: cert.to_der()?,
            key_pem: key.private_key_to_pem_pkcs8()?,
            key_der: key.private_key_to_der()?,
        })
    }
}

fn random_serial() -> Result<openssl::asn1::Asn1Integer, ErrorStack> {
    let mut bn = BigNum::new()?;
    bn.rand(64, MsbOption::MAYBE_ZERO, false)?;
    bn.to_asn1_integer()
}

#[cfg(test)]
mod tests {
    use super::*;
    use openssl::nid::Nid;

    fn fresh_tempdir() -> PathBuf {
        let nanos = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let pid = std::process::id();
        let p = std::env::temp_dir().join(format!("tls_mitm_test_{}_{}", pid, nanos));
        std::fs::create_dir_all(&p).unwrap();
        p
    }

    fn cn(name: &openssl::x509::X509NameRef) -> String {
        name.entries_by_nid(Nid::COMMONNAME)
            .next()
            .unwrap()
            .data()
            .as_utf8()
            .unwrap()
            .to_string()
    }

    #[test]
    fn ca_persists_across_loads() {
        let dir = fresh_tempdir();
        let ca1 = MitmCa::load_or_generate(&dir).unwrap();
        let ca2 = MitmCa::load_or_generate(&dir).unwrap();
        assert_eq!(ca1.cert_pem(), ca2.cert_pem());
    }

    #[test]
    fn mint_leaf_dns_name() {
        let dir = fresh_tempdir();
        let ca = MitmCa::load_or_generate(&dir).unwrap();
        let leaf = ca.mint_leaf("www.example.com").unwrap();
        let cert = X509::from_pem(&leaf.cert_pem).unwrap();

        assert_eq!(cn(cert.subject_name()), "www.example.com");
        assert_eq!(cn(cert.issuer_name()), "MacPhoenix MITM Root CA");

        // SHA1-with-RSA signature.
        assert_eq!(
            cert.signature_algorithm().object().nid(),
            Nid::SHA1WITHRSAENCRYPTION
        );

        // 1024-bit RSA key.
        let rsa = cert.public_key().unwrap().rsa().unwrap();
        assert_eq!(rsa.size() * 8, LEAF_BITS);

        // Signature verifies against the CA.
        let ca_cert = X509::from_pem(ca.cert_pem()).unwrap();
        assert!(cert.verify(&ca_cert.public_key().unwrap()).unwrap());
    }

    #[test]
    fn config_defaults_off() {
        let cfg = MitmConfig::from_args(Vec::<String>::new()).unwrap();
        assert!(!cfg.enabled);
        assert!(cfg.ports.contains(&443));
        assert!(!cfg.matches(443), "disabled must not match");
    }

    #[test]
    fn config_parses_flags() {
        let args = vec![
            "--mitm-tls".to_string(),
            "--mitm-ports".to_string(),
            "443, 993 ,465".to_string(),
            "--mitm-ca-dir".to_string(),
            "/tmp/some-ca-dir".to_string(),
        ];
        let cfg = MitmConfig::from_args(args).unwrap();
        assert!(cfg.enabled);
        assert_eq!(cfg.ports.iter().copied().collect::<Vec<_>>(), vec![443, 465, 993]);
        assert_eq!(cfg.ca_dir, PathBuf::from("/tmp/some-ca-dir"));
        assert!(cfg.matches(443));
        assert!(cfg.matches(465));
        assert!(!cfg.matches(80));
    }

    #[test]
    fn config_rejects_bad_port() {
        let args = vec!["--mitm-tls".to_string(), "--mitm-ports".to_string(), "0abc".to_string()];
        assert!(MitmConfig::from_args(args).is_err());
    }

    #[test]
    fn runtime_disabled_is_none() {
        let cfg = MitmConfig::default();
        assert!(MitmRuntime::from_config(cfg).unwrap().is_none());
    }

    fn tls12_hello_with_sni(host: &str) -> Vec<u8> {
        // Mirrors the fixture in sni::tests — kept local to avoid cross-module
        // test plumbing.
        let mut ch = Vec::new();
        ch.extend_from_slice(&[0x03, 0x03]);
        ch.extend_from_slice(&[0u8; 32]);
        ch.push(0);
        ch.extend_from_slice(&[0x00, 0x02, 0x00, 0x35]);
        ch.extend_from_slice(&[1, 0]);
        let mut ext = Vec::new();
        let hb = host.as_bytes();
        let mut sni = Vec::new();
        sni.extend_from_slice(&((3 + hb.len()) as u16).to_be_bytes());
        sni.push(0);
        sni.extend_from_slice(&(hb.len() as u16).to_be_bytes());
        sni.extend_from_slice(hb);
        ext.extend_from_slice(&0u16.to_be_bytes());
        ext.extend_from_slice(&(sni.len() as u16).to_be_bytes());
        ext.extend_from_slice(&sni);
        ch.extend_from_slice(&(ext.len() as u16).to_be_bytes());
        ch.extend_from_slice(&ext);
        let mut hs = Vec::new();
        hs.push(0x01);
        hs.push(((ch.len() >> 16) & 0xff) as u8);
        hs.extend_from_slice(&(ch.len() as u16).to_be_bytes());
        hs.extend_from_slice(&ch);
        let mut rec = Vec::new();
        rec.push(0x16);
        rec.extend_from_slice(&[0x03, 0x01]);
        rec.extend_from_slice(&(hs.len() as u16).to_be_bytes());
        rec.extend_from_slice(&hs);
        rec
    }

    #[test]
    fn session_resolves_sni_from_split_writes() {
        let mut sess = MitmSession::new(Ipv4Addr::new(192, 0, 2, 1), 443);
        let hello = tls12_hello_with_sni("www.example.com");
        // Feed in two chunks; SNI must not resolve early, then resolve on the
        // chunk that completes the record.
        let mid = hello.len() / 2;
        assert!(sess.observe_guest_bytes(&hello[..mid]).is_none());
        let end = sess.observe_guest_bytes(&hello[mid..]).cloned();
        assert_eq!(end, Some(MitmState::SniKnown("www.example.com".into())));
        assert_eq!(sess.resolved_host().as_deref(), Some("www.example.com"));
        // Subsequent writes return None because state is already resolved.
        assert!(sess.observe_guest_bytes(b"GET / HTTP/1.1\r\n").is_none());
    }

    #[test]
    fn session_abandons_non_tls_bytes() {
        let mut sess = MitmSession::new(Ipv4Addr::new(192, 0, 2, 1), 443);
        let r = sess.observe_guest_bytes(b"GET / HTTP/1.0\r\nHost: x\r\n\r\n").cloned();
        assert_eq!(r, Some(MitmState::Abandoned));
        assert!(sess.resolved_host().is_none());
    }

    #[test]
    fn session_falls_back_to_ip_when_sni_absent() {
        let mut sess = MitmSession::new(Ipv4Addr::new(10, 0, 0, 5), 443);
        // SSLv3 style: no extensions.
        let mut ch = Vec::new();
        ch.extend_from_slice(&[0x03, 0x00]);
        ch.extend_from_slice(&[0u8; 32]);
        ch.push(0);
        ch.extend_from_slice(&[0x00, 0x02, 0x00, 0x04]);
        ch.extend_from_slice(&[1, 0]);
        let mut hs = Vec::new();
        hs.push(0x01);
        hs.push(((ch.len() >> 16) & 0xff) as u8);
        hs.extend_from_slice(&(ch.len() as u16).to_be_bytes());
        hs.extend_from_slice(&ch);
        let mut rec = Vec::new();
        rec.push(0x16);
        rec.extend_from_slice(&[0x03, 0x00]);
        rec.extend_from_slice(&(hs.len() as u16).to_be_bytes());
        rec.extend_from_slice(&hs);
        let r = sess.observe_guest_bytes(&rec).cloned();
        assert_eq!(r, Some(MitmState::SniAbsent));
        assert_eq!(sess.resolved_host().as_deref(), Some("10.0.0.5"));
    }

    #[test]
    fn runtime_enabled_loads_ca() {
        let dir = fresh_tempdir();
        let mut cfg = MitmConfig::default();
        cfg.enabled = true;
        cfg.ca_dir = dir.clone();
        let rt = MitmRuntime::from_config(cfg).unwrap().expect("Some");
        assert!(dir.join("mitm_ca.crt").exists());
        assert!(rt.matches(443));
    }

    #[test]
    fn mint_leaf_ipv4_literal() {
        let dir = fresh_tempdir();
        let ca = MitmCa::load_or_generate(&dir).unwrap();
        // Just needs to succeed; SAN-IP path is taken internally.
        let _ = ca.mint_leaf("192.0.2.7").unwrap();
    }
}
