package main

// The claim (claim.go): the first load of the tab claims the server;
// another browser is refused everywhere; reloads keep working; a restart
// frees it. --tls auto's certificate.

import (
	"bufio"
	"crypto/x509"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"testing/fstest"
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
	doc := defaultDoc()
	doc["disks"].(map[string]any)["images"] = images
	s.applySettings(doc)
	s.overlays = newOverlayStore(data, s.imageFor)
	s.volumes = newVolumeStore(data)
	s.relay = &Relay{s: s}
	t.Cleanup(s.ids.warming.Wait) // before the data directory goes
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
// token's cookie, if any; headers as "Name: value" ("Host: H" the address
// asked for).
func do(s *Server, method, target, body, token string, headers ...string) answer {
	r := httptest.NewRequest(method, target, strings.NewReader(body))
	r.RemoteAddr = "192.0.2.1:4000"
	for _, h := range headers {
		k, v, _ := strings.Cut(h, ": ")
		if k == "RemoteAddr" {
			r.RemoteAddr = v
			continue
		}
		if k == "Host" {
			r.Host = v
			continue
		}
		r.Header.Set(k, v)
	}
	if body != "" && r.Header.Get("Content-Type") == "" {
		r.Header.Set("Content-Type", "application/json")
	}
	if token != "" {
		r.AddCookie(&http.Cookie{Name: cookieName(r.Host), Value: token})
	}
	w := httptest.NewRecorder()
	s.ServeHTTP(w, r)
	return answer{w.Code, w.Header(), w.Body.String()}
}

// setCookie: the claim's cookie a's Set-Cookie gives, "" if none.
func setCookie(a answer) string {
	for _, c := range (&http.Response{Header: a.h}).Cookies() {
		if strings.HasPrefix(c.Name, "executor_claim") && c.Value != "" {
			return c.Value
		}
	}
	return ""
}

// claimAs: the claim's cookie, by loading the tab.
func claimAs(t *testing.T, s *Server, headers ...string) string {
	t.Helper()
	a := do(s, "GET", "/", "", "", headers...)
	token := setCookie(a)
	if a.code != 200 || token == "" {
		t.Fatalf("claim: %d %v %s", a.code, a.h, a.body)
	}
	return token
}

// What needs the claim: every route but the tab's first load. Unclaimed
// 401 (reload), claimed by another 403 (in use); pages get a page.
var guarded = []string{
	"GET /main.js", "GET /nowhere", "GET /some/page", "GET /other.html",
	"GET /api/server", "GET /api/me", "GET /api/settings", "POST /api/settings",
	"GET /library.json", "GET /disks/a.img", "HEAD /disks/a.img", "GET /covers/a.png",
	"GET /api/sync", "GET /api/sync/overlays/k", "POST /api/sync/overlays/k?base=0", "DELETE /api/sync/overlays/k",
	"POST /api/sync/overlays/k/merge", "GET /api/sync/volumes/own", "PUT /api/sync/volumes/own",
	"DELETE /api/sync/volumes/own", "POST /api/sync/objects/missing", "PUT /api/sync/objects/" + strings.Repeat("ab", 32),
	"GET /api/sync/objects/" + strings.Repeat("ab", 32), "GET /api/relay", "GET /relay",
	"POST /api/session?id=abcdefgh1", "POST /api/session/release?id=abcdefgh1",
	"PUT /api/session/screen?id=abcdefgh1", "GET /api/session/screen",
	"POST /api/log?session=abcdefgh1", "POST /api/shot?session=abcdefgh1", "GET /api/logs",
	"GET /api/logs/2026-11-02/abcdefgh1.log",
	"GET /pair", "POST /api/pair", "GET /api/paired", "DELETE /api/paired/0123456789abcdef", // the pairing's, gone
	"POST /api/logout", "POST /api/login", // the password's, gone
}

func checkRefused(t *testing.T, s *Server, token string, want int, page string) {
	t.Helper()
	for _, req := range guarded {
		method, target, _ := strings.Cut(req, " ")
		a := do(s, method, target, "{}", token, "Range: bytes=0-99", "Upgrade: websocket",
			"Connection: Upgrade", "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==", "Sec-WebSocket-Version: 13")
		w := want
		if method != "GET" && method != "HEAD" && (strings.Contains(target, "/api/pair") || strings.Contains(target, "/api/log")) &&
			!strings.HasPrefix(target, "/api/log?") {
			w = 405 // no such route: gone
		}
		if a.code != w {
			t.Errorf("%s: %d, want %d (%.80s)", req, a.code, w, a.body)
		}
		if setCookie(a) != "" {
			t.Errorf("%s: claimed", req)
		}
		if a.code == want && method == "GET" && !strings.HasPrefix(target, "/api/") && !strings.HasPrefix(target, "/relay") &&
			(!strings.Contains(target, ".") || strings.HasSuffix(target, ".html")) && !strings.Contains(a.body, page) {
			t.Errorf("%s: not the page with %s: %.80s", req, page, a.body)
		}
	}
}

// Every route needs the claim (the route table), and without it is refused.
func TestUnclaimedRefused(t *testing.T) {
	for _, rt := range routes {
		if !rt.auth {
			t.Errorf("%s %s: no claim needed", rt.method, rt.re)
		}
	}
	s := testServer(t, nil)
	checkRefused(t, s, "", 401, `id="open"`)
	checkRefused(t, s, "0123", 401, `id="open"`)
	// Not a claim: HEAD, a fetch, an image, a frame, another site's link
	// or form, from another site at all.
	for _, h := range [][]string{
		{"Sec-Fetch-Dest: empty"}, {"Sec-Fetch-Dest: image"}, {"Sec-Fetch-Dest: iframe"},
		{"Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: document"}, {"Sec-Fetch-Site: same-site"},
		{"Origin: http://evil.example"},
	} {
		if a := do(s, "GET", "/", "", "", h...); a.code != 401 || setCookie(a) != "" {
			t.Errorf("GET / %v: %d, claimed %v", h, a.code, setCookie(a) != "")
		}
	}
	if a := do(s, "HEAD", "/", "", ""); a.code != 401 || setCookie(a) != "" {
		t.Errorf("HEAD /: %d", a.code)
	}
	if s.claim.claimed() {
		t.Fatal("claimed")
	}
	// Claimed: another browser gets 403 everywhere, the tab too.
	claimAs(t, s, "Sec-Fetch-Site: none", "Sec-Fetch-Dest: document")
	checkRefused(t, s, "", 403, `id="in-use"`)
	checkRefused(t, s, "0123", 403, `id="in-use"`)
	for _, p := range []string{"/", "/index.html"} {
		if a := do(s, "GET", p, "", ""); a.code != 403 || setCookie(a) != "" || !strings.Contains(a.body, "in use by another browser") {
			t.Errorf("%s, another browser: %d %.80s", p, a.code, a.body)
		}
	}
	if a := do(s, "GET", "/api/me", "", ""); a.json()["error"] != "in use by another browser" {
		t.Errorf("/api/me, another browser: %s", a.body)
	}
}

func TestClaim(t *testing.T) {
	s := testServer(t, nil)
	a := do(s, "GET", "/", "", "")
	token := setCookie(a)
	if a.code != 200 || a.body != "<p>tab</p>" || len(token) != 64 {
		t.Fatalf("the first load: %d %q %q", a.code, a.body, token)
	}
	cookie := a.h.Get("Set-Cookie")
	for _, attr := range []string{"HttpOnly", "SameSite=Strict", "Path=/", "Max-Age="} {
		if !strings.Contains(cookie, attr) {
			t.Errorf("cookie %q: no %s", cookie, attr)
		}
	}
	if strings.Contains(cookie, "Secure") {
		t.Errorf("cookie %q: Secure over plain HTTP", cookie)
	}
	// Reloads, the site, the API: this browser's.
	for _, p := range []string{"/", "/index.html", "/main.js", "/api/me", "/library.json"} {
		if a := do(s, "GET", p, "", token); a.code != 200 || setCookie(a) != "" {
			t.Errorf("%s: %d (a new cookie: %v)", p, a.code, setCookie(a) != "")
		}
	}
	if a := do(s, "POST", "/api/settings", `{"machine":{"ram_mb":96}}`, token); a.code != 200 {
		t.Errorf("a write: %d", a.code)
	}
	// A second browser can neither read nor write.
	if a := do(s, "GET", "/", "", ""); a.code != 403 || setCookie(a) != "" {
		t.Errorf("a second browser: %d", a.code)
	}
	if a := do(s, "POST", "/api/settings", `{"machine":{"ram_mb":32}}`, "f00"); a.code != 403 {
		t.Errorf("a second browser's write: %d", a.code)
	}
	if v := do(s, "GET", "/api/settings", "", token).json(); at(v["settings"].(map[string]any), "machine.ram_mb") != 96.0 {
		t.Errorf("after the second browser: %v", v["settings"])
	}
	// A restart (a new server) frees it: the old cookie
	// gets 401 (the tab asks for a reload); the reload claims it again.
	s2 := testServer(t, nil)
	if a := do(s2, "GET", "/api/me", "", token); a.code != 401 || a.json()["reload"] != true {
		t.Errorf("the old cookie after a restart: %d %s", a.code, a.body)
	}
	a = do(s2, "GET", "/", "", token)
	again := setCookie(a)
	if a.code != 200 || again == "" || again == token {
		t.Fatalf("the reload after a restart: %d %q", a.code, again)
	}
	if a := do(s2, "GET", "/api/me", "", again); a.code != 200 {
		t.Errorf("the new cookie: %d", a.code)
	}
	if a := do(s2, "GET", "/api/me", "", token); a.code != 403 {
		t.Errorf("the old cookie, claimed again: %d", a.code)
	}
	// Over TLS (or a trusted proxy's https) the cookie is Secure.
	s3 := testServer(t, nil)
	r := httptest.NewRequest("GET", "https://example.com/", nil)
	w := httptest.NewRecorder()
	s3.ServeHTTP(w, r)
	if w.Code != 200 || !strings.Contains(w.Header().Get("Set-Cookie"), "Secure") {
		t.Errorf("over TLS: %d %q", w.Code, w.Header().Get("Set-Cookie"))
	}
}

// Two servers on one machine (ports 8090, 8091) don't share a cookie.
func TestClaimCookiePerPort(t *testing.T) {
	a, b := testServer(t, nil), testServer(t, nil)
	x := do(a, "GET", "/", "", "", "Host: localhost:8090")
	if !strings.HasPrefix(x.h.Get("Set-Cookie"), "executor_claim_8090=") {
		t.Errorf("8090: %q", x.h.Get("Set-Cookie"))
	}
	y := do(b, "GET", "/", "", "", "Host: localhost:8091")
	if !strings.HasPrefix(y.h.Get("Set-Cookie"), "executor_claim_8091=") {
		t.Errorf("8091: %q", y.h.Get("Set-Cookie"))
	}
	if do(a, "GET", "/api/me", "", setCookie(x), "Host: localhost:8090").code != 200 ||
		do(b, "GET", "/api/me", "", setCookie(y), "Host: localhost:8091").code != 200 {
		t.Error("each its own")
	}
}

// The relay's WebSocket over a real connection: refused unclaimed and to
// another browser, open to the claim's.
func TestRelayClaim(t *testing.T) {
	s := testServer(t, nil)
	srv := httptest.NewServer(s)
	defer srv.Close()
	addr := srv.Listener.Addr().String()
	upgrade := func(token string) (net.Conn, int) {
		conn, err := net.Dial("tcp", addr)
		if err != nil {
			t.Fatal(err)
		}
		cookie := ""
		if token != "" {
			cookie = "Cookie: " + cookieName(addr) + "=" + token + "\r\n"
		}
		fmt.Fprintf(conn, "GET /relay HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"+
			"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n%s\r\n", addr, cookie)
		res, err := http.ReadResponse(bufio.NewReader(conn), nil)
		if err != nil {
			t.Fatal(err)
		}
		return conn, res.StatusCode
	}
	if conn, code := upgrade(""); code != 401 {
		t.Errorf("unclaimed: %d", code)
		conn.Close()
	}
	token := claimAs(t, s)
	if conn, code := upgrade("f00"); code != 403 {
		t.Errorf("another browser: %d", code)
		conn.Close()
	}
	conn, code := upgrade(token)
	defer conn.Close()
	if code != 101 {
		t.Fatalf("claimed: %d", code)
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
