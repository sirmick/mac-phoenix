//! TLS ClientHello SNI sniffer.
//!
//! Parses the server_name_list (extension 0x0000, name_type host_name) out
//! of a TLS ClientHello handshake record. SSLv3 has no extension section
//! and therefore no SNI — callers fall back to reverse DNS in that case.
//!
//! Caller is expected to accumulate bytes from the peer until either the
//! full first TLS record is available or we've clearly exceeded what a
//! sane ClientHello could be. `peek_sni` is zero-side-effect; it never
//! consumes or mutates the buffer.

/// Result of sniffing the first bytes of a TLS flow.
#[derive(Debug, PartialEq, Eq)]
pub enum SniPeek {
    /// Full ClientHello parsed; SNI extension present with this hostname.
    Found(String),
    /// Full ClientHello parsed; no SNI extension present (e.g. SSLv3).
    NotPresent,
    /// Not enough bytes yet — call again once more data arrives.
    NeedMore,
    /// Bytes do not look like a TLS handshake. Caller should abandon
    /// MITM for this connection and fall back to raw pass-through.
    NotTls,
}

const CLIENT_HELLO_CAP: usize = 16 * 1024; // hard ceiling — real ones are tiny

pub fn peek_sni(data: &[u8]) -> SniPeek {
    // Record layer: content_type(1) + version(2) + length(2) = 5 bytes.
    if data.len() < 5 {
        return SniPeek::NeedMore;
    }
    // Netscape 3 / MSIE 3 / MSIE 2 on classic Mac send an "SSLv2-format
    // ClientHello" — a legacy envelope whose first byte has the MSB set
    // (2-byte length form) and byte 2 is 0x01 (ClientHello). It ADVERTISES
    // SSLv3 or higher as a protocol version inside. OpenSSL accepts it
    // and upgrades the negotiation to whatever the server supports, but
    // SSLv2 has no extensions at all, so there is no SNI to sniff.
    // Treat it as SniAbsent so the caller falls back to IP-as-hostname.
    if (data[0] & 0x80) != 0 && data[2] == 0x01 {
        return SniPeek::NotPresent;
    }
    if data[0] != 0x16 {
        return SniPeek::NotTls; // not a TLS 1.x handshake record either
    }
    let rec_len = u16::from_be_bytes([data[3], data[4]]) as usize;
    if rec_len > CLIENT_HELLO_CAP {
        return SniPeek::NotTls;
    }
    if data.len() < 5 + rec_len {
        return SniPeek::NeedMore;
    }
    let rec = &data[5..5 + rec_len];

    // Handshake layer: type(1) + length(3).
    if rec.len() < 4 {
        return SniPeek::NotTls;
    }
    if rec[0] != 0x01 {
        return SniPeek::NotTls; // not ClientHello
    }
    let hs_len = ((rec[1] as usize) << 16) | ((rec[2] as usize) << 8) | (rec[3] as usize);
    if hs_len > rec.len() - 4 {
        return SniPeek::NotTls;
    }
    let body = &rec[4..4 + hs_len];

    parse_client_hello_body(body)
}

fn parse_client_hello_body(body: &[u8]) -> SniPeek {
    let mut p = 0;
    // legacy_version(2) + random(32)
    if body.len() < 34 {
        return SniPeek::NotTls;
    }
    p += 34;
    // session_id
    if body.len() < p + 1 {
        return SniPeek::NotTls;
    }
    let sid_len = body[p] as usize;
    p += 1;
    if body.len() < p + sid_len {
        return SniPeek::NotTls;
    }
    p += sid_len;
    // cipher_suites (2-byte length)
    if body.len() < p + 2 {
        return SniPeek::NotTls;
    }
    let cs_len = u16::from_be_bytes([body[p], body[p + 1]]) as usize;
    p += 2;
    if body.len() < p + cs_len {
        return SniPeek::NotTls;
    }
    p += cs_len;
    // compression_methods (1-byte length)
    if body.len() < p + 1 {
        return SniPeek::NotTls;
    }
    let cm_len = body[p] as usize;
    p += 1;
    if body.len() < p + cm_len {
        return SniPeek::NotTls;
    }
    p += cm_len;
    // extensions — optional. SSLv3 omits the section entirely.
    if body.len() == p {
        return SniPeek::NotPresent;
    }
    if body.len() < p + 2 {
        return SniPeek::NotTls;
    }
    let ext_total = u16::from_be_bytes([body[p], body[p + 1]]) as usize;
    p += 2;
    if body.len() < p + ext_total {
        return SniPeek::NotTls;
    }
    let ext_area = &body[p..p + ext_total];

    let mut q = 0;
    while q + 4 <= ext_area.len() {
        let ext_type = u16::from_be_bytes([ext_area[q], ext_area[q + 1]]);
        let ext_len = u16::from_be_bytes([ext_area[q + 2], ext_area[q + 3]]) as usize;
        q += 4;
        if q + ext_len > ext_area.len() {
            return SniPeek::NotTls;
        }
        if ext_type == 0x0000 {
            return parse_sni_extension(&ext_area[q..q + ext_len]);
        }
        q += ext_len;
    }
    SniPeek::NotPresent
}

fn parse_sni_extension(d: &[u8]) -> SniPeek {
    if d.len() < 2 {
        return SniPeek::NotTls;
    }
    let list_len = u16::from_be_bytes([d[0], d[1]]) as usize;
    if d.len() < 2 + list_len {
        return SniPeek::NotTls;
    }
    let list = &d[2..2 + list_len];
    let mut r = 0;
    while r + 3 <= list.len() {
        let name_type = list[r];
        let name_len = u16::from_be_bytes([list[r + 1], list[r + 2]]) as usize;
        r += 3;
        if r + name_len > list.len() {
            return SniPeek::NotTls;
        }
        if name_type == 0 {
            match std::str::from_utf8(&list[r..r + name_len]) {
                Ok(s) => return SniPeek::Found(s.to_string()),
                Err(_) => return SniPeek::NotTls,
            }
        }
        r += name_len;
    }
    SniPeek::NotPresent
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Build a minimal TLS1.2 ClientHello record carrying a single SNI
    /// extension for `host`.
    fn tls12_client_hello_with_sni(host: &str) -> Vec<u8> {
        let mut ch = Vec::new();
        ch.extend_from_slice(&[0x03, 0x03]); // client_version = TLS1.2
        ch.extend_from_slice(&[0u8; 32]); // random
        ch.push(0); // session_id len
        // Two cipher suites (arbitrary)
        ch.extend_from_slice(&[0x00, 0x04]); // cipher list length
        ch.extend_from_slice(&[0xC0, 0x2F, 0x00, 0x35]);
        // 1 compression method, NULL
        ch.push(1);
        ch.push(0);
        // Extensions
        let mut ext = Vec::new();
        // SNI extension (type 0x0000)
        let mut sni_data = Vec::new();
        let host_b = host.as_bytes();
        // server_name_list_length = 3 + host_len
        let list_len = 3 + host_b.len();
        sni_data.extend_from_slice(&(list_len as u16).to_be_bytes());
        sni_data.push(0x00); // host_name
        sni_data.extend_from_slice(&(host_b.len() as u16).to_be_bytes());
        sni_data.extend_from_slice(host_b);
        ext.extend_from_slice(&0x0000u16.to_be_bytes()); // type
        ext.extend_from_slice(&(sni_data.len() as u16).to_be_bytes()); // length
        ext.extend_from_slice(&sni_data);
        ch.extend_from_slice(&(ext.len() as u16).to_be_bytes());
        ch.extend_from_slice(&ext);

        wrap_client_hello(&ch)
    }

    /// Build an SSLv3 ClientHello with no extensions block at all.
    fn sslv3_client_hello() -> Vec<u8> {
        let mut ch = Vec::new();
        ch.extend_from_slice(&[0x03, 0x00]);
        ch.extend_from_slice(&[0u8; 32]);
        ch.push(0);
        ch.extend_from_slice(&[0x00, 0x02]);
        ch.extend_from_slice(&[0x00, 0x04]); // RC4-MD5
        ch.push(1);
        ch.push(0);
        wrap_client_hello(&ch)
    }

    fn wrap_client_hello(body: &[u8]) -> Vec<u8> {
        let mut hs = Vec::new();
        hs.push(0x01); // ClientHello
        let l = body.len();
        hs.push(((l >> 16) & 0xff) as u8);
        hs.extend_from_slice(&(l as u16).to_be_bytes());
        hs.extend_from_slice(body);

        let mut rec = Vec::new();
        rec.push(0x16);
        rec.extend_from_slice(&[0x03, 0x01]); // record_version
        rec.extend_from_slice(&(hs.len() as u16).to_be_bytes());
        rec.extend_from_slice(&hs);
        rec
    }

    #[test]
    fn sni_found_in_tls12() {
        let data = tls12_client_hello_with_sni("www.example.com");
        assert_eq!(peek_sni(&data), SniPeek::Found("www.example.com".into()));
    }

    #[test]
    fn sni_absent_in_sslv3() {
        let data = sslv3_client_hello();
        assert_eq!(peek_sni(&data), SniPeek::NotPresent);
    }

    #[test]
    fn needs_more_when_truncated() {
        let data = tls12_client_hello_with_sni("example.com");
        // One byte short of a full record.
        let short = &data[..data.len() - 1];
        assert_eq!(peek_sni(short), SniPeek::NeedMore);
    }

    #[test]
    fn only_header() {
        let data = tls12_client_hello_with_sni("example.com");
        assert_eq!(peek_sni(&data[..3]), SniPeek::NeedMore);
    }

    #[test]
    fn not_a_handshake() {
        let data = [0x17, 0x03, 0x03, 0x00, 0x05, 1, 2, 3, 4, 5];
        assert_eq!(peek_sni(&data), SniPeek::NotTls);
    }

    #[test]
    fn sslv2_format_hello_recognised_as_sni_absent() {
        // Netscape 3's SSLv2-backwards-compat ClientHello shape:
        //   [len_hi|0x80][len_lo][0x01][0x03 0x00 ... (SSLv3 advertised)]
        // We don't parse contents — just need byte0 MSB set + byte2==0x01.
        let hello: [u8; 11] = [0x80, 0x2e, 0x01, 0x03, 0x00, 0, 0, 0, 0, 0, 0];
        assert_eq!(peek_sni(&hello), SniPeek::NotPresent);
    }

    #[test]
    fn handshake_but_not_client_hello() {
        let mut data = vec![0x16, 0x03, 0x03, 0x00, 0x04];
        data.extend_from_slice(&[0x02, 0x00, 0x00, 0x00]); // ServerHello
        assert_eq!(peek_sni(&data), SniPeek::NotTls);
    }
}
