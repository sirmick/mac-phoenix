package main

// Pairing (pair.go): unpaired requests are refused everywhere, pairing
// works, revoke works, rotate works; --tls auto's certificate.

import (
	"bufio"
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"testing/fstest"
	"time"
)

// testServer: a server on a scratch data directory, a library of one
// disk, the site fsys.
func testServer(t *testing.T, fsys fstest.MapFS) *Server {
	t.Helper()
	data := t.TempDir()
	images := filepath.Join(data, "images")
	os.MkdirAll(images, 0o755)
	os.WriteFile(filepath.Join(images, "a.img"), make([]byte, 64<<10), 0o644)
	os.WriteFile(filepath.Join(images, "a.img.json"), []byte(`{"title":"A"}`), 0o644)
	if fsys == nil {
		fsys = fstest.MapFS{"index.html": {Data: []byte("<p>tab</p>")}, "main.js": {Data: []byte("// js")}}
	}
	s := &Server{data: data, settingsPath: filepath.Join(data, "settings.json"), quiet: true,
		ids: newIds(filepath.Join(data, "image-ids.json")), site: fsys}
	var err error
	if s.pairing, err = loadPairing(data, false); err != nil {
		t.Fatal(err)
	}
	doc := defaultDoc()
	doc["disks"].(map[string]any)["images"] = images
	s.applySettings(doc)
	s.overlays = newOverlayStore(data, s.imageFor)
	s.volumes = newVolumeStore(data)
	s.relay = &Relay{s: s}
	return s
}

type answer struct {
	code int
	h    http.Header
	body string
}

func (a answer) json() map[string]any {
	var v map[string]any
	json.Unmarshal([]byte(a.body), &v)
	return v
}

// do: a request to s from addr (default 192.0.2.1, not loopback) with
// token's cookie, if any; headers as "Name: value".
func do(s *Server, method, target, body, token string, headers ...string) answer {
	r := httptest.NewRequest(method, target, strings.NewReader(body))
	r.RemoteAddr = "192.0.2.1:4000"
	for _, h := range headers {
		k, v, _ := strings.Cut(h, ": ")
		if k == "RemoteAddr" {
			r.RemoteAddr = v
			continue
		}
		r.Header.Set(k, v)
	}
	if body != "" && r.Header.Get("Content-Type") == "" {
		r.Header.Set("Content-Type", "application/json")
	}
	if token != "" {
		r.AddCookie(&http.Cookie{Name: cookieName, Value: token})
	}
	w := httptest.NewRecorder()
	s.ServeHTTP(w, r)
	return answer{w.Code, w.Header(), w.Body.String()}
}

// pairAs: a new browser's cookie, by POST /api/pair.
func pairAs(t *testing.T, s *Server, name string, headers ...string) string {
	t.Helper()
	a := do(s, "POST", "/api/pair", fmt.Sprintf(`{"key":%q,"name":%q}`, s.pairing.key, name), "", headers...)
	if a.code != 200 {
		t.Fatalf("pair: %d %s", a.code, a.body)
	}
	for _, c := range (&http.Response{Header: a.h}).Cookies() {
		if c.Name == cookieName && c.Value != "" {
			return c.Value
		}
	}
	t.Fatalf("pair: no cookie in %v", a.h)
	return ""
}

// Every route but the pairing page's needs a paired browser.
func TestUnpairedRefused(t *testing.T) {
	open := map[string]bool{"GET /pair": true, "POST /api/pair": true}
	for _, rt := range routes {
		name := rt.method + " " + strings.TrimSuffix(strings.TrimPrefix(rt.re.String(), "^(?:"), ")$")
		if rt.auth == open[name] {
			t.Errorf("%s: auth %v", name, rt.auth)
		}
	}
	s := testServer(t, nil)
	sha := strings.Repeat("ab", 32)
	for _, req := range []string{
		"GET /", "GET /index.html", "GET /main.js", "GET /nowhere", "GET /some/page",
		"GET /api/server", "GET /api/me", "GET /api/settings", "POST /api/settings",
		"GET /library.json", "GET /disks/a.img", "HEAD /disks/a.img", "GET /covers/a.png",
		"GET /api/sync", "GET /api/sync/overlays/k", "POST /api/sync/overlays/k?base=0", "DELETE /api/sync/overlays/k",
		"POST /api/sync/overlays/k/merge", "GET /api/sync/volumes/own", "PUT /api/sync/volumes/own",
		"DELETE /api/sync/volumes/own", "POST /api/sync/objects/missing", "PUT /api/sync/objects/" + sha,
		"GET /api/sync/objects/" + sha, "GET /api/relay", "GET /relay",
		"POST /api/session?id=abcdefgh1", "POST /api/session/release?id=abcdefgh1",
		"PUT /api/session/screen?id=abcdefgh1", "GET /api/session/screen",
		"POST /api/log?session=abcdefgh1", "POST /api/shot?session=abcdefgh1", "GET /api/logs",
		"GET /api/logs/2026-11-02/abcdefgh1.log", "GET /api/paired", "DELETE /api/paired/0123456789abcdef",
		"POST /api/logout", "POST /api/login",
	} {
		method, target, _ := strings.Cut(req, " ")
		a := do(s, method, target, "{}", "", "Range: bytes=0-99", "Upgrade: websocket",
			"Connection: Upgrade", "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==", "Sec-WebSocket-Version: 13")
		want := 401
		if req == "POST /api/logout" || req == "POST /api/login" {
			want = 405 // gone
		}
		if a.code != want {
			t.Errorf("%s: %d, want %d (%.80s)", req, a.code, want, a.body)
		}
		if a.code == 401 && method == "GET" && !strings.HasPrefix(target, "/api/") &&
			!strings.Contains(target, ".") && !strings.HasPrefix(target, "/relay") &&
			!strings.Contains(a.body, `id="key"`) {
			t.Errorf("%s: not the pairing page: %.80s", req, a.body)
		}
	}
	if a := do(s, "GET", "/pair", "", ""); a.code != 200 || !strings.Contains(a.body, "/api/pair") {
		t.Errorf("/pair: %d", a.code)
	}
	// The pairing page has no key in it, nor does anything unpaired.
	if a := do(s, "GET", "/", "", ""); strings.Contains(a.body, s.pairing.key) {
		t.Error("the key in the pairing page")
	}
}

func TestPairing(t *testing.T) {
	s := testServer(t, nil)
	if a := do(s, "POST", "/api/pair", `{"key":"0123456789abcdef0123456789abcdef"}`, ""); a.code != 401 {
		t.Errorf("a wrong key: %d", a.code)
	}
	if a := do(s, "POST", "/api/pair", fmt.Sprintf(`{"key":%q}`, s.pairing.key), "", "Origin: http://evil.example"); a.code != 403 {
		t.Errorf("from another site: %d", a.code)
	}
	a := do(s, "POST", "/api/pair", fmt.Sprintf(`{"key":%q}`, strings.ToUpper(grouped(s.pairing.key))), "",
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0 Safari/537.36")
	if a.code != 200 || a.json()["name"] != "Chrome on Linux" {
		t.Fatalf("the key, typed in fours: %d %s", a.code, a.body)
	}
	cookie := a.h.Get("Set-Cookie")
	for _, attr := range []string{"HttpOnly", "SameSite=Strict", "Path=/"} {
		if !strings.Contains(cookie, attr) {
			t.Errorf("cookie %q: no %s", cookie, attr)
		}
	}
	if strings.Contains(cookie, "Secure") {
		t.Errorf("cookie %q: Secure over plain HTTP", cookie)
	}
	token := pairAs(t, s, "laptop")
	for _, target := range []string{"/", "/main.js", "/api/me", "/api/server", "/library.json", "/api/settings"} {
		if a := do(s, "GET", target, "", token); a.code != 200 {
			t.Errorf("paired %s: %d %.80s", target, a.code, a.body)
		}
	}
	if a := do(s, "GET", "/disks/a.img", "", token, "Range: bytes=0-99"); a.code != 206 || len(a.body) != 100 {
		t.Errorf("a disk's range, paired: %d %d", a.code, len(a.body))
	}
	if a := do(s, "GET", "/api/me", "", token+"x"); a.code != 401 {
		t.Errorf("a made-up cookie: %d", a.code)
	}
	// The file holds the tokens' hashes, not the tokens.
	b, _ := os.ReadFile(filepath.Join(s.data, "paired.json"))
	if strings.Contains(string(b), token) || !strings.Contains(string(b), "laptop") {
		t.Errorf("paired.json: %s", b)
	}
	for _, f := range []string{"pair.key", "paired.json"} {
		if st, _ := os.Stat(filepath.Join(s.data, f)); st == nil || st.Mode().Perm() != 0o600 {
			t.Errorf("%s: %v", f, st)
		}
	}
	// Too many wrong keys from one address: refused, the right one too;
	// another address still pairs.
	for i := 0; i < pairFailures; i++ {
		do(s, "POST", "/api/pair", `{"key":"nope"}`, "", "RemoteAddr: 198.51.100.9:1")
	}
	if a := do(s, "POST", "/api/pair", fmt.Sprintf(`{"key":%q}`, s.pairing.key), "", "RemoteAddr: 198.51.100.9:1"); a.code != 429 {
		t.Errorf("after %d wrong keys: %d", pairFailures, a.code)
	}
	pairAs(t, s, "another", "RemoteAddr: 198.51.100.10:1")
	// Over TLS (or a trusted proxy's https) the cookie is Secure.
	r := httptest.NewRequest("POST", "/api/pair", strings.NewReader(fmt.Sprintf(`{"key":%q}`, s.pairing.key)))
	r.TLS = &tls.ConnectionState{}
	w := httptest.NewRecorder()
	s.ServeHTTP(w, r)
	if !strings.Contains(w.Header().Get("Set-Cookie"), "Secure") {
		t.Errorf("over TLS: %q", w.Header().Get("Set-Cookie"))
	}
}

func TestRevoke(t *testing.T) {
	s := testServer(t, nil)
	a, b := pairAs(t, s, "A"), pairAs(t, s, "B")
	list := do(s, "GET", "/api/paired", "", a).json()["browsers"].([]any)
	if len(list) != 2 {
		t.Fatalf("paired: %v", list)
	}
	var bID, aID string
	for _, x := range list {
		m := x.(map[string]any)
		switch m["name"] {
		case "A":
			aID = m["id"].(string)
			if m["this"] != true {
				t.Errorf("A is not this: %v", m)
			}
		case "B":
			bID = m["id"].(string)
		}
	}
	if r := do(s, "DELETE", "/api/paired/"+bID, "", a); r.code != 200 || len(r.json()["browsers"].([]any)) != 1 {
		t.Fatalf("revoke B: %d %s", r.code, r.body)
	}
	if r := do(s, "GET", "/api/me", "", b); r.code != 401 {
		t.Errorf("B after revoke: %d", r.code)
	}
	if r := do(s, "GET", "/api/me", "", a); r.code != 200 {
		t.Errorf("A after revoking B: %d", r.code)
	}
	if r := do(s, "DELETE", "/api/paired/"+bID, "", a); r.code != 404 {
		t.Errorf("revoke B again: %d", r.code)
	}
	r := do(s, "DELETE", "/api/paired/"+aID, "", a)
	if r.code != 200 || !strings.Contains(r.h.Get("Set-Cookie"), "Max-Age=0") {
		t.Errorf("revoke this one: %d %v", r.code, r.h)
	}
	if r := do(s, "GET", "/api/me", "", a); r.code != 401 {
		t.Errorf("A after revoking itself: %d", r.code)
	}
}

func TestRotate(t *testing.T) {
	s := testServer(t, nil)
	token := pairAs(t, s, "A")
	key := s.pairing.key
	// A restart keeps the key and the browsers ...
	p, err := loadPairing(s.data, false)
	if err != nil || p.key != key {
		t.Fatalf("restart: %v %q", err, p.key)
	}
	if _, ok := p.seen(token); !ok {
		t.Error("A unpaired by a restart")
	}
	// ... --rotate-key doesn't.
	if s.pairing, err = loadPairing(s.data, true); err != nil {
		t.Fatal(err)
	}
	if s.pairing.key == key || len(s.pairing.key) != 32 {
		t.Errorf("rotated: %q", s.pairing.key)
	}
	if r := do(s, "GET", "/api/me", "", token); r.code != 401 {
		t.Errorf("A after rotate: %d", r.code)
	}
	if r := do(s, "POST", "/api/pair", fmt.Sprintf(`{"key":%q}`, key), ""); r.code != 401 {
		t.Errorf("the old key after rotate: %d", r.code)
	}
	pairAs(t, s, "A again")
	if b, _ := os.ReadFile(filepath.Join(s.data, "pair.key")); strings.TrimSpace(string(b)) != s.pairing.key {
		t.Errorf("pair.key: %q", b)
	}
}

// The relay's WebSocket over a real connection: refused unpaired, open
// paired, closed when its browser is revoked.
func TestRelayPaired(t *testing.T) {
	s := testServer(t, nil)
	srv := httptest.NewServer(s)
	defer srv.Close()
	upgrade := func(token string) (net.Conn, int) {
		conn, err := net.Dial("tcp", srv.Listener.Addr().String())
		if err != nil {
			t.Fatal(err)
		}
		cookie := ""
		if token != "" {
			cookie = "Cookie: " + cookieName + "=" + token + "\r\n"
		}
		fmt.Fprintf(conn, "GET /relay HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"+
			"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n%s\r\n", srv.Listener.Addr(), cookie)
		res, err := http.ReadResponse(bufio.NewReader(conn), nil)
		if err != nil {
			t.Fatal(err)
		}
		return conn, res.StatusCode
	}
	if conn, code := upgrade(""); code != 401 {
		t.Errorf("unpaired: %d", code)
		conn.Close()
	}
	token := pairAs(t, s, "A")
	conn, code := upgrade(token)
	defer conn.Close()
	if code != 101 {
		t.Fatalf("paired: %d", code)
	}
	id := do(s, "GET", "/api/paired", "", token).json()["browsers"].([]any)[0].(map[string]any)["id"].(string)
	do(s, "DELETE", "/api/paired/"+id, "", token)
	conn.SetReadDeadline(time.Now().Add(5 * time.Second))
	if _, err := io.ReadAll(conn); err != nil {
		t.Errorf("the relay after revoke: %v (still open)", err)
	}
}

func TestAutoCert(t *testing.T) {
	dir := t.TempDir()
	cert, fp, err := autoCert(dir)
	if err != nil {
		t.Fatal(err)
	}
	_, fp2, err := autoCert(dir) // made once
	if err != nil || fp2 != fp || len(fp) != 95 {
		t.Errorf("again: %v %q %q", err, fp, fp2)
	}
	if st, _ := os.Stat(filepath.Join(dir, "tls", "key.pem")); st == nil || st.Mode().Perm() != 0o600 {
		t.Errorf("key.pem: %v", st)
	}
	x, _ := x509.ParseCertificate(cert.Certificate[0])
	if x.VerifyHostname("127.0.0.1") != nil || x.VerifyHostname("localhost") != nil {
		t.Errorf("names: %v %v", x.DNSNames, x.IPAddresses)
	}
}
