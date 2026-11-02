package main

// Pairing: a browser uses this server once it has shown the key
// (docs/executor/Standalone.md "Pairing"). The key is 128 random bits
// made on the first run, DATA/pair.key (0600); the server prints it in a
// ready address, https://HOST:PORT/#key=KEY (a fragment: no request,
// log or Referer carries it), which --open opens. The page there sends
// it once, POST /api/pair { key, name } (compared in constant time; an
// address with pairFailures wrong ones in pairWindow gets 429); the
// answer sets a session cookie (HttpOnly, SameSite=Strict, Secure over
// TLS) whose token's hash is kept in DATA/paired.json with the
// browser's name and when it was last seen. Every route needs one but
// this page and /api/pair; loopback is nothing special.
//
//	GET    /api/paired       { browsers: [{ id, name, address, created, last_seen, this }] }
//	DELETE /api/paired/ID    revoke one ("this": the cookie goes too)
//
// --rotate-key makes a new key and forgets every paired browser.

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/hex"
	"encoding/json"
	"encoding/pem"
	"errors"
	"fmt"
	"math/big"
	"net"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
)

const (
	pairFailures = 10 // per address in pairWindow, then 429
	pairWindow   = 10 * time.Minute
	pairedLife   = 400 * 24 * time.Hour // unseen this long: forgotten (a cookie's longest in Chrome)
	seenSaveGap  = time.Minute          // last_seen reaches the file at most this often
)

type Browser struct {
	Name     string    `json:"name"`
	Address  string    `json:"address"`
	Created  time.Time `json:"created"`
	LastSeen time.Time `json:"last_seen"`
}

type Pairing struct {
	mu       sync.Mutex
	dir      string
	key      string              // 32 hex digits
	browsers map[string]*Browser // sha256(token), hex -> the browser
	failures map[string][]time.Time
	saved    time.Time
}

func (p *Pairing) keyPath() string    { return filepath.Join(p.dir, "pair.key") }
func (p *Pairing) pairedPath() string { return filepath.Join(p.dir, "paired.json") }

// loadPairing: the key (made if there is none, or rotate) and the paired
// browsers (none, if rotate).
func loadPairing(dir string, rotate bool) (*Pairing, error) {
	p := &Pairing{dir: dir, browsers: map[string]*Browser{}, failures: map[string][]time.Time{}}
	os.Remove(filepath.Join(dir, "sessions.json")) // the password's sessions, before pairing
	if b, err := os.ReadFile(p.keyPath()); err == nil && !rotate {
		p.key = normalKey(string(b))
	}
	if len(p.key) != 32 {
		b := make([]byte, 16)
		rand.Read(b)
		p.key = hex.EncodeToString(b)
		if err := writeAtomic(p.keyPath(), []byte(p.key+"\n"), 0o600); err != nil {
			return nil, err
		}
		rotate = true
	}
	if rotate {
		p.mu.Lock()
		p.save()
		p.mu.Unlock()
	} else if b, err := os.ReadFile(p.pairedPath()); err == nil {
		json.Unmarshal(b, &p.browsers)
	}
	return p, nil
}

// normalKey: the key as typed, without spaces, dashes or capitals.
func normalKey(s string) string {
	var b strings.Builder
	for _, r := range strings.ToLower(s) {
		if (r >= '0' && r <= '9') || (r >= 'a' && r <= 'f') {
			b.WriteRune(r)
		}
	}
	return b.String()
}

// grouped: the key for reading out, in fours.
func grouped(key string) string {
	var parts []string
	for i := 0; i < len(key); i += 4 {
		parts = append(parts, key[i:min(i+4, len(key))])
	}
	return strings.Join(parts, "-")
}

func tokenHash(token string) string {
	h := sha256.Sum256([]byte(token))
	return hex.EncodeToString(h[:])
}

func browserID(hash string) string { return hash[:16] }

// save writes paired.json. Under mu.
func (p *Pairing) save() {
	now := time.Now()
	for k, b := range p.browsers {
		if now.Sub(b.LastSeen) > pairedLife {
			delete(p.browsers, k)
		}
	}
	if b, err := json.MarshalIndent(p.browsers, "", "  "); err == nil {
		writeAtomic(p.pairedPath(), append(b, '\n'), 0o600)
	}
	p.saved = now
}

// seen: whether token is a paired browser's (its last_seen now, if so).
func (p *Pairing) seen(token string) (string, bool) {
	if token == "" {
		return "", false
	}
	h := tokenHash(token)
	p.mu.Lock()
	defer p.mu.Unlock()
	b, ok := p.browsers[h]
	if !ok {
		return "", false
	}
	now := time.Now()
	b.LastSeen = now
	if now.Sub(p.saved) > seenSaveGap {
		p.save()
	}
	return h, true
}

// pair: a new browser's token if key is the key; refused (429) after
// pairFailures wrong ones from addr.
func (p *Pairing) pair(key, name, addr string) (string, error) {
	now := time.Now()
	p.mu.Lock()
	defer p.mu.Unlock()
	recent := p.failures[addr][:0:0]
	for _, t := range p.failures[addr] {
		if now.Sub(t) < pairWindow {
			recent = append(recent, t)
		}
	}
	if len(recent) >= pairFailures {
		return "", httpErr(429, "too many wrong keys: try again later")
	}
	if subtle.ConstantTimeCompare([]byte(normalKey(key)), []byte(p.key)) != 1 {
		p.failures[addr] = append(recent, now)
		if len(p.failures) > 1000 {
			for k, l := range p.failures {
				if len(l) == 0 || now.Sub(l[len(l)-1]) >= pairWindow {
					delete(p.failures, k)
				}
			}
		}
		return "", httpErr(401, "that is not the key")
	}
	delete(p.failures, addr)
	b := make([]byte, 32)
	rand.Read(b)
	token := hex.EncodeToString(b)
	p.browsers[tokenHash(token)] = &Browser{Name: name, Address: addr, Created: now, LastSeen: now}
	p.save()
	return token, nil
}

// list: the paired browsers, the most recently seen first; this is the
// asking one's hash.
func (p *Pairing) list(this string) []map[string]any {
	p.mu.Lock()
	defer p.mu.Unlock()
	out := []map[string]any{}
	for h, b := range p.browsers {
		out = append(out, map[string]any{"id": browserID(h), "name": b.Name, "address": b.Address,
			"created": b.Created.UTC().Format(time.RFC3339), "last_seen": b.LastSeen.UTC().Format(time.RFC3339),
			"this": h == this})
	}
	sort.Slice(out, func(i, j int) bool { return out[i]["last_seen"].(string) > out[j]["last_seen"].(string) })
	return out
}

// name: the paired browser's, by its hash.
func (p *Pairing) name(h string) string {
	p.mu.Lock()
	defer p.mu.Unlock()
	if b, ok := p.browsers[h]; ok {
		return b.Name
	}
	return ""
}

// revoke forgets the browser id; its hash, if there was one.
func (p *Pairing) revoke(id string) (string, bool) {
	p.mu.Lock()
	defer p.mu.Unlock()
	for h := range p.browsers {
		if browserID(h) == id {
			delete(p.browsers, h)
			p.save()
			return h, true
		}
	}
	return "", false
}

// browserName: what a paired browser is called in the list, from its
// User-Agent when the page sends no name ("Firefox on Linux").
func browserName(ua string) string {
	app := "A browser"
	for _, x := range []struct{ token, name string }{
		{"Edg/", "Edge"}, {"OPR/", "Opera"}, {"Firefox/", "Firefox"}, {"HeadlessChrome/", "Headless Chrome"},
		{"Chrome/", "Chrome"}, {"Safari/", "Safari"},
	} {
		if strings.Contains(ua, x.token) {
			app = x.name
			break
		}
	}
	for _, x := range []struct{ token, name string }{
		{"iPhone", "iPhone"}, {"iPad", "iPad"}, {"Android", "Android"}, {"CrOS", "ChromeOS"},
		{"Mac OS X", "macOS"}, {"Windows", "Windows"}, {"Linux", "Linux"},
	} {
		if strings.Contains(ua, x.token) {
			return app + " on " + x.name
		}
	}
	return app
}

func init() {
	handle("GET", "/pair", false, func(c *ctx, m []string) error {
		if c.authed {
			return c.redirect("/")
		}
		return c.sendPairPage(200)
	})
	handle("POST", "/api/pair", false, func(c *ctx, m []string) error {
		b, err := c.jsonBody()
		if err != nil {
			return err
		}
		key, _ := b["key"].(string)
		name, _ := b["name"].(string)
		if name = strings.TrimSpace(name); name == "" {
			name = browserName(c.r.UserAgent())
		}
		if len(name) > 80 {
			name = name[:80]
		}
		token, err := c.s.pairing.pair(key, name, c.address())
		if err != nil {
			return err
		}
		c.setCookie(token)
		return c.sendJSON(200, map[string]any{"name": name})
	})
	handle("GET", "/api/paired", true, func(c *ctx, m []string) error {
		return c.sendJSON(200, map[string]any{"browsers": c.s.pairing.list(c.browser)})
	})
	handle("DELETE", "/api/paired/([0-9a-f]{16})", true, func(c *ctx, m []string) error {
		h, ok := c.s.pairing.revoke(m[1])
		if !ok {
			return httpErr(404, "no such browser")
		}
		c.s.relay.unpaired(h)
		if h == c.browser {
			c.setCookie("")
		}
		return c.sendJSON(200, map[string]any{"browsers": c.s.pairing.list(c.browser)})
	})
}

func (c *ctx) sendPairPage(status int) error {
	c.w.Header().Set("Cache-Control", "no-store")
	return c.sendBytes(status, []byte(pairPage), "text/html; charset=utf-8")
}

// The page an unpaired browser gets, whatever page it asked for: the key
// from the address's #key= (then out of the address), or typed.
const pairPage = `<!doctype html>
<html><head><meta charset="utf-8"><title>Executor: pair this browser</title>
<meta name="viewport" content="width=device-width">
<style>body{font:15px system-ui,sans-serif;max-width:28em;margin:5em auto;padding:0 1em}
input,button{font:inherit;padding:.3em}#key{width:100%;box-sizing:border-box;font-family:ui-monospace,monospace}
#err{color:#b00}small{color:#666}</style>
</head><body>
<h1>Executor</h1>
<p>This browser isn't paired with the server yet. Enter the key it printed when it started
(<small>or open the address it printed, which has the key in it</small>).</p>
<form id="f"><p><input id="key" autocomplete="off" spellcheck="false" placeholder="xxxx-xxxx-xxxx-xxxx-xxxx-xxxx-xxxx-xxxx" autofocus></p>
<p><button id="go">Pair</button></p><p id="err" role="alert"></p></form>
<script>
async function pair(key) {
    const err = document.getElementById('err');
    err.textContent = '';
    const res = await fetch('/api/pair', { method: 'POST', headers: { 'Content-Type': 'application/json' },
                                           body: JSON.stringify({ key }) });
    if (res.ok)
        return location.replace(location.pathname === '/pair' ? '/' : location.pathname + location.search);
    err.textContent = (await res.json().catch(() => ({}))).error || ('HTTP ' + res.status);
}
document.getElementById('f').onsubmit = (e) => {
    e.preventDefault();
    pair(document.getElementById('key').value);
};
// The key in the address: now, or pasted into this page's (a new fragment only).
function fromAddress() {
    const m = /(?:^#|&)key=([^&]*)/.exec(location.hash);
    if (m) {
        history.replaceState(null, '', location.pathname + location.search);
        pair(decodeURIComponent(m[1]));
    }
}
addEventListener('hashchange', fromAddress);
fromAddress();
</script></body></html>
`

// ---- --tls auto: a self-signed certificate, made once ----

// autoCert: DATA/tls/cert.pem and key.pem, made if missing, for
// localhost, this machine's name and its addresses now; and its SHA-256
// fingerprint, which the server prints (a browser shows the same).
func autoCert(dir string) (tls.Certificate, string, error) {
	certPath, keyPath := filepath.Join(dir, "tls", "cert.pem"), filepath.Join(dir, "tls", "key.pem")
	cert, err := tls.LoadX509KeyPair(certPath, keyPath)
	if errors.Is(err, os.ErrNotExist) {
		err = makeCert(certPath, keyPath)
		if err == nil {
			cert, err = tls.LoadX509KeyPair(certPath, keyPath)
		}
	}
	if err != nil {
		return tls.Certificate{}, "", fmt.Errorf("--tls auto: %v", err)
	}
	sum := sha256.Sum256(cert.Certificate[0])
	var hexes []string
	for _, b := range sum {
		hexes = append(hexes, fmt.Sprintf("%02X", b))
	}
	return cert, strings.Join(hexes, ":"), nil
}

func makeCert(certPath, keyPath string) error {
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return err
	}
	serial, _ := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 127))
	host, _ := os.Hostname()
	tmpl := &x509.Certificate{
		SerialNumber: serial,
		Subject:      pkix.Name{CommonName: "Executor (" + host + ")", Organization: []string{"executor-standalone"}},
		NotBefore:    time.Now().Add(-time.Hour),
		NotAfter:     time.Now().AddDate(10, 0, 0),
		KeyUsage:     x509.KeyUsageDigitalSignature,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		DNSNames:     []string{"localhost"},
		IPAddresses:  []net.IP{net.IPv4(127, 0, 0, 1), net.IPv6loopback},
	}
	if host != "" {
		tmpl.DNSNames = append(tmpl.DNSNames, host, host+".local")
	}
	if addrs, err := net.InterfaceAddrs(); err == nil {
		for _, a := range addrs {
			if n, ok := a.(*net.IPNet); ok && !n.IP.IsLoopback() && !n.IP.IsLinkLocalUnicast() {
				tmpl.IPAddresses = append(tmpl.IPAddresses, n.IP)
			}
		}
	}
	der, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, &key.PublicKey, key)
	if err != nil {
		return err
	}
	kb, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(certPath), 0o700); err != nil {
		return err
	}
	if err := writeAtomic(keyPath, pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: kb}), 0o600); err != nil {
		return err
	}
	return writeAtomic(certPath, pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}), 0o644)
}
