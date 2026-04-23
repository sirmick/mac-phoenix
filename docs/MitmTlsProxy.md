# MITM TLS Proxy

The MITM TLS proxy lets classic-Mac browsers inside the emulator reach
modern HTTPS sites by terminating TLS 1.2/1.3 on the host and re-encrypting
to the guest using SSLv3 / TLS 1.0 with RC4-MD5 / RC4-SHA / 3DES-SHA — the
ciphers MSIE 4.5, Netscape 3.x/4.x and contemporary Mac browsers actually
understand.

Lives in `net-bridge` (the smoltcp-based network process spawned when
`--network socket` is active). Off by default; opt in with `--mitm-tls`.

## Setup (one-time)

1. **Build the legacy-capable OpenSSL**. The system `libssl` on Ubuntu has
   SSLv3 and RC4 stripped — we ship a vendored OpenSSL build script:
   ```
   net-bridge/tools/build-legacy-openssl.sh
   ```
   This downloads OpenSSL 3.0.13, verifies its sha256, and installs a
   static build to `net-bridge/vendor/openssl-legacy/` with
   `enable-ssl3 enable-ssl3-method enable-weak-ssl-ciphers enable-legacy`.

2. **Rebuild `net-bridge` against the vendored OpenSSL**:
   ```
   export OPENSSL_DIR=$PWD/net-bridge/vendor/openssl-legacy
   export OPENSSL_STATIC=1
   cargo build --release --manifest-path net-bridge/Cargo.toml
   ```

3. **Start the emulator with `--mitm-tls`**:
   ```
   ./build/mac-phoenix \
       --network socket --bridge --mitm-tls \
       --extfs ~/macshare \
       ~/roms/quadra.rom
   ```
   On first run, the bridge creates the root CA at `./.mitm_ca/mitm_ca.crt`
   and `./.mitm_ca/mitm_ca.key` (or wherever `--mitm-ca-dir` points).

4. **Import the root CA inside the guest**. Copy `mitm_ca.crt` into the
   shared ExtFS folder:
   ```
   cp .mitm_ca/mitm_ca.crt ~/macshare/MitmCA.crt
   ```
   In the guest, open `Host:MitmCA.crt` with MSIE / Netscape and accept
   the certificate as a trusted root CA. The exact path depends on the
   browser; MSIE 4.5 uses *Edit → Preferences → Receiving Files →
   Certificate Authorities*.

## How it works

1. Guest connects to e.g. `www.example.com:443`. `net-bridge` sees the
   SYN, opens a real TCP socket to the destination, replies SYN-ACK as
   before.
2. Because `dst_port` matches the MITM set, the connection enters a
   *sniffing* phase: incoming bytes are **buffered** rather than
   forwarded. No ClientHello has hit the real server yet.
3. Once the ClientHello is complete, `net-bridge` extracts the SNI
   hostname (or falls back to the IP literal for SSLv3 without SNI).
4. It mints a leaf cert for that hostname (1024-bit RSA, SHA1-RSA,
   minimal extensions — what classic browsers parse) signed by the
   local root CA, and spins up two TLS sessions:
   - **Downstream** to the guest: SSLv3 / TLS 1.0, cipher from
     `RC4-MD5:RC4-SHA:DES-CBC3-SHA:AES128-SHA:AES256-SHA:@SECLEVEL=0`.
   - **Upstream** to the real server: modern TLS, system trust store,
     real-cert verification on.
5. After both handshakes complete, plaintext flows through the bridge
   both ways: guest HTTP request → decrypt downstream → re-encrypt
   upstream → real server, response goes back the same way reversed.
6. If the guest's first bytes don't look like TLS at all (e.g. plain
   HTTP on 443), the connection falls back to raw pass-through and the
   buffered bytes are flushed to the real server — the MITM abandons
   cleanly.

## Flags

| Flag | Default | Description |
|------|---------|-------------|
| `--mitm-tls` | off | Enable the proxy. |
| `--mitm-ports LIST` | `443` | Comma-separated TCP ports to intercept. |
| `--mitm-ca-dir PATH` | `.mitm_ca` | Directory holding `mitm_ca.{key,crt}`. |

## Testing

Unit + integration tests live in `net-bridge/src/tls_listener.rs` and
cover: CA + cert minting; SNI sniffer across TLS 1.2, SSLv3, truncated
and malformed inputs; SSLv3 + RC4-MD5, TLS 1.0 + 3DES handshakes; and a
full bridge end-to-end test with a mock upstream server.

Run with:
```
OPENSSL_DIR=$PWD/net-bridge/vendor/openssl-legacy OPENSSL_STATIC=1 \
    cargo test --manifest-path net-bridge/Cargo.toml
```

Guest-side protocol smoke test: `tests/guest/SslMitmTest.pl` — a
MacPerl script that opens a raw TCP socket to `:443`, sends a
hand-built SSLv3 ClientHello, and verifies the ServerHello's record
type, version, and negotiated cipher ID. Dispatched like
`MacTestSuite.pl` via `/api/launch` once the runner script is wired up.

## Security posture

- The root CA private key sits at `.mitm_ca/mitm_ca.key` with mode 0600.
  Treat it the way you'd treat any local MITM CA — trust it only inside
  the guest you deliberately installed it into.
- Upstream cert validation is **always on**. The MITM is not a
  "disable TLS" switch; bad real certs still propagate as TLS alerts
  to the guest.
- Minted leaves use SHA1 + 1024-bit RSA. This is intentional — classic
  browsers reject SHA256 and anything bigger than 2048-bit RSA. The
  bridge's upstream side uses modern defaults.

## Limitations

- **No STARTTLS** yet. SMTP 25 / IMAP 143 / POP3 110 go raw.
- **No content rewriting**. gzip, HTTP/2-only servers, and similar
  application-layer mismatches are out of scope.
- **SNI-less SSLv3** falls back to the destination IP, which is
  unreliable for shared-IP CDNs. Most classic-Mac browsers send SNI
  once upgraded to TLS 1.0; the fallback matters mostly for Netscape
  3.x.
