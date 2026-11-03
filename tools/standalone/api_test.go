package main

// The API over a real connection, the claiming browser's: the page and
// the library, the logs, disk ranges, sync, hostile requests, cross-site
// writes, the controlling tab, the relay. And the program itself (TestMain
// runs main in a child): the printed address, --tls auto, the claim over
// HTTPS and a restart freeing it.

import (
	"bufio"
	"bytes"
	"crypto/rand"
	"crypto/sha1"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
	"time"
)

func TestMain(m *testing.M) {
	if os.Getenv("EXECUTOR_STANDALONE_MAIN") == "1" { // TestProgram's child: the program itself
		os.Args = append([]string{"executor-standalone"}, strings.Fields(os.Getenv("EXECUTOR_STANDALONE_ARGS"))...)
		main()
		return
	}
	os.Exit(m.Run())
}

// live: testServer on a real listener, and the claiming browser's cookie.
type live struct {
	t     *testing.T
	s     *Server
	addr  string
	token string
}

func liveServer(t *testing.T) *live {
	s := testServer(t, nil)
	hs := httptest.NewServer(s)
	t.Cleanup(hs.Close)
	return &live{t, s, hs.Listener.Addr().String(), claimAs(t, s)}
}

// req: method path with body ([]byte, or JSON of anything else), the
// cookie (but with "Cookie: " given), headers "Name: value".
func (l *live) req(method, path string, body any, headers ...string) (*http.Response, []byte) {
	l.t.Helper()
	var rd io.Reader
	ctype := ""
	switch b := body.(type) {
	case nil:
	case []byte:
		rd = bytes.NewReader(b)
	default:
		j, _ := json.Marshal(b)
		rd, ctype = bytes.NewReader(j), "application/json"
	}
	r, _ := http.NewRequest(method, "http://"+l.addr+path, rd)
	if ctype != "" {
		r.Header.Set("Content-Type", ctype)
	}
	for _, h := range headers {
		k, v, _ := strings.Cut(h, ": ")
		if k == "Host" {
			r.Host = v
		} else {
			r.Header.Set(k, v)
		}
	}
	if _, ok := r.Header["Cookie"]; !ok {
		r.Header.Set("Cookie", cookieName(r.Host)+"="+l.token)
	}
	res, err := http.DefaultTransport.RoundTrip(r)
	if err != nil {
		l.t.Fatal(err)
	}
	data, _ := io.ReadAll(res.Body)
	res.Body.Close()
	return res, data
}

func (l *live) status(method, path string, body any, headers ...string) int {
	l.t.Helper()
	res, _ := l.req(method, path, body, headers...)
	return res.StatusCode
}

func (l *live) json(method, path string, body any) map[string]any {
	l.t.Helper()
	res, data := l.req(method, path, body)
	var v map[string]any
	if err := json.Unmarshal(data, &v); err != nil {
		l.t.Fatalf("%s %s: %d %q", method, path, res.StatusCode, data)
	}
	return v
}

// raw: text on a new connection, everything it answers.
func (l *live) raw(text string) []byte {
	c, err := net.Dial("tcp", l.addr)
	if err != nil {
		l.t.Fatal(err)
	}
	defer c.Close()
	c.SetDeadline(time.Now().Add(10 * time.Second))
	io.WriteString(c, text)
	data, _ := io.ReadAll(c)
	return data
}

func (l *live) images() string { return l.s.settings().Images }

func TestAPIPageAndLibrary(t *testing.T) {
	l := liveServer(t)
	img := l.images()
	os.WriteFile(filepath.Join(img, "secret.img"), []byte("x"), 0o644) // no record: not offered
	os.WriteFile(filepath.Join(img, "c.iso"), make([]byte, 4096), 0o644)
	os.WriteFile(filepath.Join(img, "c.iso.json"), []byte(`{"title":"CD C","tags":["cd"],"cover":"c.jpg","items":[{"name":"Software","kind":"folder","size":9}]}`), 0o644)
	os.WriteFile(filepath.Join(img, "c.jpg"), []byte("cover"), 0o644)
	os.WriteFile(filepath.Join(img, "d.jpg"), []byte("not a cover"), 0o644)
	if res, data := l.req("GET", "/", nil); res.StatusCode != 200 || string(data) != "<p>tab</p>" {
		t.Errorf("/: %d %q", res.StatusCode, data)
	}
	if code := l.status("GET", "/../settings.json", nil); code != 404 {
		t.Errorf("/../settings.json: %d", code)
	}
	_, data := l.req("GET", "/library.json", nil)
	var lib []map[string]any
	json.Unmarshal(data, &lib)
	if len(lib) != 2 || lib[0]["title"] != "A" || lib[0]["url"] != "disks/a.img" || lib[0]["size"] != float64(64<<10) ||
		lib[0]["cover"] != nil || lib[1]["url"] != "disks/c.iso" || lib[1]["cover"] != "covers/c.jpg" {
		t.Errorf("library.json: %s", data)
	}
	// A record's items pass through (the library's details row)
	if items, _ := lib[1]["items"].([]any); len(items) != 1 || items[0].(map[string]any)["name"] != "Software" || lib[0]["items"] != nil {
		t.Errorf("library.json items: %s", data)
	}
	for path, want := range map[string]int{"/disks/secret.img": 404, "/covers/c.jpg": 200, "/covers/d.jpg": 404} {
		if code := l.status("GET", path, nil); code != want {
			t.Errorf("%s: %d, want %d", path, code, want)
		}
	}
	if code := l.status("GET", "/covers/c.jpg", nil, "Cookie: "); code != 403 {
		t.Errorf("a cover, another browser: %d", code)
	}
}

func TestAPILogs(t *testing.T) {
	l := liveServer(t)
	const sess = "k1x-0011223344556677"
	if code := l.status("POST", "/api/log?session=abc", []byte("x")); code != 400 {
		t.Errorf("a bad session: %d", code)
	}
	l.req("POST", "/api/log?session="+sess, []byte("one\ntwo"))
	l.req("POST", "/api/log?session="+sess, []byte("three\n"))
	_, data := l.req("GET", "/api/logs", nil)
	var logs []map[string]any
	json.Unmarshal(data, &logs)
	if len(logs) != 1 || !strings.HasSuffix(logs[0]["log"].(string), "/"+sess+".log") {
		t.Fatalf("/api/logs: %s", data)
	}
	name := logs[0]["log"].(string)
	_, data = l.req("GET", "/api/logs/"+name, nil)
	lines := strings.Split(strings.TrimSpace(string(data)), "\n")
	if !strings.HasPrefix(lines[0], "# ") || !strings.Contains(lines[0], " local ") || strings.Join(lines[1:], ",") != "one,two,three" {
		t.Errorf("the log: %q", data)
	}
	if code := l.status("GET", "/api/logs/2000-01-01/../../settings.json", nil); code != 404 {
		t.Errorf("out of the logs: %d", code)
	}
	if code := l.status("POST", "/api/shot?session="+sess, []byte("GIF89a")); code != 400 {
		t.Errorf("a GIF: %d", code)
	}
	png := append([]byte("\x89PNG\r\n\x1a\n"), make([]byte, 100000)...)
	res, data := l.req("POST", "/api/shot?session="+sess+"&note=the%20menu", png)
	var v map[string]any
	json.Unmarshal(data, &v)
	shot, _ := v["name"].(string)
	if res.StatusCode != 200 || shot == "" {
		t.Fatalf("a shot: %d %s", res.StatusCode, data)
	}
	if _, got := l.req("GET", "/api/logs/"+shot, nil); !bytes.Equal(got, png) {
		t.Error("the shot back")
	}
	_, data = l.req("GET", "/api/logs/"+name, nil)
	lines = strings.Split(strings.TrimSpace(string(data)), "\n")
	if lines[len(lines)-1] != "# screenshot "+shot+": the menu" {
		t.Errorf("the shot's line: %q", lines[len(lines)-1])
	}
}

func TestAPIDiskRanges(t *testing.T) {
	l := liveServer(t)
	disk := bytes.Repeat(func() []byte {
		b := make([]byte, 256)
		for i := range b {
			b[i] = byte(i)
		}
		return b
	}(), 4096) // 1 MB
	os.WriteFile(filepath.Join(l.images(), "a.img"), disk, 0o644)
	res, data := l.req("GET", "/disks/a.img", nil, "Range: bytes=4096-8191")
	if res.StatusCode != 206 || res.Header.Get("Content-Range") != fmt.Sprintf("bytes 4096-8191/%d", len(disk)) ||
		!bytes.Equal(data, disk[4096:8192]) {
		t.Errorf("a range: %d %v", res.StatusCode, res.Header)
	}
	sum := sha256.Sum256(disk)
	ident := hex.EncodeToString(sum[:])[:24]
	etag := res.Header.Get("ETag")
	if etag != `"`+ident+`"` || res.Header.Get("X-Image-Id") != ident {
		t.Errorf("ETag %s, X-Image-Id %s, want %s", etag, res.Header.Get("X-Image-Id"), ident)
	}
	if res, data := l.req("GET", "/disks/a.img", nil, "Range: bytes=0-9", `If-Range: "other"`); res.StatusCode != 200 || len(data) != len(disk) {
		t.Errorf("If-Range another: %d %d", res.StatusCode, len(data))
	}
	if code := l.status("GET", "/disks/a.img", nil, "Range: bytes=2000000-"); code != 416 {
		t.Errorf("past the end: %d", code)
	}
	if res, data := l.req("HEAD", "/disks/a.img", nil); res.StatusCode != 200 || res.Header.Get("Accept-Ranges") != "bytes" || len(data) != 0 {
		t.Errorf("HEAD: %d %v", res.StatusCode, res.Header)
	}
	if code := l.status("GET", "/disks/a.img", nil, "If-None-Match: "+etag); code != 304 {
		t.Errorf("If-None-Match: %d", code)
	}
	if code := l.status("GET", "/disks/a.img", nil, "Range: bytes=0-99", "Cookie: "); code != 403 {
		t.Errorf("a range, another browser: %d", code)
	}
}

func TestAPICrossSite(t *testing.T) {
	l := liveServer(t)
	if code := l.status("GET", "/api/sync", nil); code != 200 {
		t.Errorf("claimed: %d", code)
	}
	for _, h := range []string{"Origin: https://elsewhere.example", "Sec-Fetch-Site: cross-site"} {
		if code := l.status("DELETE", "/api/sync/volumes/own", nil, h); code != 403 {
			t.Errorf("a write, %s: %d", h, code)
		}
	}
	if code := l.status("GET", "/relay", nil, "Origin: https://elsewhere.example", "Upgrade: websocket",
		"Connection: Upgrade", "Sec-WebSocket-Key: eA=="); code != 403 {
		t.Errorf("the relay from another site: %d", code)
	}
}

func rec(block uint32, fill byte) []byte {
	return append(binary.LittleEndian.AppendUint32(nil, block), bytes.Repeat([]byte{fill}, 4096)...)
}

func records(data []byte) map[uint32]byte {
	out := map[uint32]byte{}
	for i := 0; i+4100 <= len(data); i += 4100 {
		out[binary.LittleEndian.Uint32(data[i:])] = data[i+4]
	}
	return out
}

func TestAPIOverlaySync(t *testing.T) {
	l := liveServer(t)
	post := func(q string, body []byte) (int, map[string]any) {
		res, data := l.req("POST", "/api/sync/overlays/a.img-x?"+q, body)
		var v map[string]any
		json.Unmarshal(data, &v)
		return res.StatusCode, v
	}
	cat := func(b ...[]byte) []byte { return bytes.Join(b, nil) }
	if code, _ := post("", rec(0, 1)); code != 400 {
		t.Errorf("no base: %d", code)
	}
	_, v := post("base=0&name=a.img&size=1048576", cat(rec(0, 1), rec(7, 2)))
	if v["version"] != 1.0 {
		t.Fatalf("v1: %v", v)
	}
	if code, v := post("base=0", rec(7, 9)); code != 409 || v["version"] != 1.0 {
		t.Errorf("behind: %d %v", code, v)
	}
	_, v = post("base=1", cat(rec(7, 3), rec(9, 4)))
	v2 := int(v["version"].(float64))
	res, data := l.req("GET", "/api/sync/overlays/a.img-x?since=0", nil)
	if res.Header.Get("X-Sync-Version") != fmt.Sprint(v2) || fmt.Sprint(records(data)) != fmt.Sprint(map[uint32]byte{0: 1, 7: 3, 9: 4}) {
		t.Errorf("since 0: %v %v", res.Header, records(data))
	}
	if _, data := l.req("GET", "/api/sync/overlays/a.img-x?since=1", nil); len(data) != 2*4100 {
		t.Errorf("since 1: %d bytes", len(data))
	}
	if code, _ := post(fmt.Sprintf("base=%d", v2), []byte("short")); code != 400 {
		t.Errorf("short: %d", code)
	}
	// A replace, staged: nothing shows until the commit, then only its blocks.
	if code, _ := post("stage=token-1234", rec(1, 5)); code != 204 {
		t.Errorf("stage: %d", code)
	}
	if _, data := l.req("GET", fmt.Sprintf("/api/sync/overlays/a.img-x?since=%d", v2), nil); len(data) != 0 {
		t.Errorf("staged shows: %d bytes", len(data))
	}
	_, v = post(fmt.Sprintf("base=%d&stage=token-1234&replace=1", v2), rec(2, 6))
	v3 := int(v["version"].(float64))
	res, data = l.req("GET", fmt.Sprintf("/api/sync/overlays/a.img-x?since=%d", v2), nil)
	if res.Header.Get("X-Sync-Reset") != "1" || fmt.Sprint(records(data)) != fmt.Sprint(map[uint32]byte{1: 5, 2: 6}) {
		t.Errorf("replaced: %v %v", res.Header, records(data))
	}
	v4 := int(l.json("DELETE", "/api/sync/overlays/a.img-x", nil)["version"].(float64))
	if res, data := l.req("GET", fmt.Sprintf("/api/sync/overlays/a.img-x?since=%d", v3), nil); res.Header.Get("X-Sync-Reset") != "1" || len(data) != 0 {
		t.Errorf("reverted: %v %d", res.Header, len(data))
	}
	if res, _ := l.req("GET", fmt.Sprintf("/api/sync/overlays/a.img-x?since=%d", v4), nil); res.Header.Get("X-Sync-Reset") != "0" {
		t.Errorf("since the revert: %v", res.Header)
	}
}

func TestAPIVolumeSync(t *testing.T) {
	l := liveServer(t)
	blob := []byte("kept across browsers")
	sum := sha256.Sum256(blob)
	sha := hex.EncodeToString(sum[:])
	manifest := map[string]any{"dirs": []any{"/executor"},
		"files": map[string]any{"/executor/note": map[string]any{"size": float64(len(blob)), "mtime": 5.0, "sha": sha}}}
	if code := l.status("PUT", "/api/sync/volumes/own", map[string]any{"manifest": manifest}); code != 400 {
		t.Errorf("no base: %d", code)
	}
	res, data := l.req("PUT", "/api/sync/volumes/own", map[string]any{"manifest": manifest, "base": 0})
	if res.StatusCode != 409 || !strings.Contains(string(data), sha) {
		t.Errorf("missing: %d %s", res.StatusCode, data)
	}
	if v := l.json("POST", "/api/sync/objects/missing", map[string]any{"shas": []string{sha}}); fmt.Sprint(v["missing"]) != "["+sha+"]" {
		t.Errorf("objects/missing: %v", v)
	}
	if code := l.status("PUT", "/api/sync/objects/"+sha, []byte("other")); code != 400 {
		t.Errorf("the wrong bytes: %d", code)
	}
	if code := l.status("PUT", "/api/sync/objects/"+sha, blob); code != 204 {
		t.Errorf("the object: %d", code)
	}
	v := l.json("PUT", "/api/sync/volumes/own", map[string]any{"manifest": manifest, "base": 0})
	version, _ := v["version"].(float64)
	if res, data := l.req("PUT", "/api/sync/volumes/own", map[string]any{"manifest": manifest, "base": 0}); res.StatusCode != 409 ||
		!strings.Contains(string(data), fmt.Sprintf(`"version":%v`, version)) {
		t.Errorf("behind: %d %s", res.StatusCode, data)
	}
	got := l.json("GET", "/api/sync/volumes/own", nil)
	if got["version"] != version || !jsonEqual(got["manifest"], manifest) {
		t.Errorf("the volume: %v", got)
	}
	if _, data := l.req("GET", "/api/sync/objects/"+sha, nil); !bytes.Equal(data, blob) {
		t.Errorf("the object back: %q", data)
	}
	l.req("DELETE", "/api/sync/volumes/own", nil)
	if got := l.json("GET", "/api/sync/volumes/own", nil); got["version"] != version+1 || got["manifest"] != nil {
		t.Errorf("deleted: %v", got)
	}
}

func jsonEqual(a, b any) bool {
	x, _ := json.Marshal(a)
	y, _ := json.Marshal(b)
	return bytes.Equal(x, y)
}

func TestAPIHostileRequests(t *testing.T) {
	l := liveServer(t)
	// A negative or bad length: 400, the connection closed.
	for _, n := range []string{"-1", "abc"} {
		data := l.raw("POST /api/settings HTTP/1.1\r\nHost: x\r\nContent-Length: " + n + "\r\n\r\n")
		if line, _, _ := bytes.Cut(data, []byte("\r\n")); !bytes.Contains(line, []byte(" 400 ")) {
			t.Errorf("Content-Length %s: %q", n, line)
		}
	}
	// Another browser's large body is refused before it is read.
	if code := l.status("POST", "/api/settings", bytes.Repeat([]byte("x"), 100<<10), "Content-Type: application/json", "Cookie: "); code != 403 {
		t.Errorf("a large body, another browser: %d", code)
	}
	// CR/LF in the path can't make headers.
	data := l.raw("GET /a%0d%0aSet-Cookie:%20evil=1.html HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
	head, _, _ := bytes.Cut(data, []byte("\r\n\r\n"))
	if bytes.Contains(head, []byte("\r\nSet-Cookie: evil")) || !bytes.Contains(head, []byte(" 403 ")) {
		t.Errorf("CR/LF: %q", head)
	}
	// ../ however spelled stays in the site.
	for _, p := range []string{"/%2e%2e/settings.json", "/..%2fsettings.json", "/%2e%2e%2f%2e%2e/etc/passwd"} {
		if code := l.status("GET", p, nil); code != 404 {
			t.Errorf("%s: %d", p, code)
		}
	}
}

func TestAPISettings(t *testing.T) {
	l := liveServer(t)
	v := l.json("GET", "/api/settings", nil)
	if v["version"] != 2.0 || at(v["settings"].(map[string]any), "machine.screen") != "640x480" {
		t.Errorf("GET: %v", v)
	}
	if code := l.status("POST", "/api/settings", map[string]any{"machine": map[string]any{"ram_mb": 96},
		"input": map[string]any{"command_key": "ctrl"}, "network": map[string]any{"relay_max_sockets": 16}}); code != 200 {
		t.Errorf("POST: %d", code)
	}
	st := l.json("GET", "/api/settings", nil)["settings"].(map[string]any)
	if at(st, "machine.ram_mb") != 96.0 || at(st, "input.command_key") != "ctrl" || at(st, "network.relay_max_sockets") != 16.0 {
		t.Errorf("kept: %v", st)
	}
	l.req("POST", "/api/settings", map[string]any{"machine": nil, "input": nil, "network": map[string]any{"relay_max_sockets": nil}})
	st = l.json("GET", "/api/settings", nil)["settings"].(map[string]any)
	if at(st, "machine.ram_mb") != 0.0 || at(st, "input.command_key") != "meta" || at(st, "network.relay_max_sockets") != 32.0 {
		t.Errorf("reset: %v", st)
	}
}

func TestAPIOneControllingTab(t *testing.T) {
	l := liveServer(t)
	png := []byte("\x89PNG\r\n\x1a\npixels")
	a, b := "tab-a-00001", "tab-b-00001"
	beat := func(id string, take bool) string {
		q := "/api/session?id=" + id
		if take {
			q += "&take=1"
		}
		v := l.json("POST", q, nil)
		return fmt.Sprintf("%v %v %v %v", v["control"], v["held"], v["handing"], v["viewers"])
	}
	if got := beat(a, false); got != "true false false 0" {
		t.Errorf("A: %s", got)
	}
	if got := beat(b, false); got != "false true false 1" {
		t.Errorf("B: %s", got)
	}
	if code := l.status("PUT", "/api/session/screen?id="+b, png); code != 409 {
		t.Errorf("B's screen: %d", code)
	}
	if code := l.status("PUT", "/api/session/screen?id="+a, png); code != 204 {
		t.Errorf("A's screen: %d", code)
	}
	if _, data := l.req("GET", "/api/session/screen", nil); !bytes.Equal(data, png) {
		t.Errorf("the screen: %q", data)
	}
	// B takes control: A sees it, lets go; B waits for that.
	if got := beat(b, true); got != "true false true 1" {
		t.Errorf("B takes: %s", got)
	}
	if got := beat(a, false); !strings.HasPrefix(got, "false ") {
		t.Errorf("A after: %s", got)
	}
	l.req("POST", "/api/session/release?id="+a, nil)
	if got := beat(b, false); got != "true false false 0" {
		t.Errorf("B, A gone: %s", got)
	}
	if code := l.status("GET", "/api/session/screen", nil); code != 404 {
		t.Errorf("A's screen after: %d", code)
	}
	if code := l.status("POST", "/api/session?id=x", nil); code != 400 {
		t.Errorf("a bad id: %d", code)
	}
}

// ---- the relay ----

type ws struct {
	t *testing.T
	c net.Conn
	r *bufio.Reader
}

// relayOpen: a WebSocket on /relay, its HELLO checked; nil and the status
// if it isn't one.
func (l *live) relayOpen(origin string) (*ws, int) {
	l.t.Helper()
	c, err := net.Dial("tcp", l.addr)
	if err != nil {
		l.t.Fatal(err)
	}
	k := make([]byte, 16)
	rand.Read(k)
	key := base64.StdEncoding.EncodeToString(k)
	extra := ""
	if origin != "" {
		extra = "Origin: " + origin + "\r\n"
	}
	fmt.Fprintf(c, "GET /relay HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"+
		"Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n%sCookie: %s=%s\r\n\r\n", l.addr, key, extra, cookieName(l.addr), l.token)
	w := &ws{l.t, c, bufio.NewReader(c)}
	c.SetDeadline(time.Now().Add(10 * time.Second))
	res, err := http.ReadResponse(w.r, nil)
	if err != nil {
		l.t.Fatal(err)
	}
	if res.StatusCode != 101 {
		c.Close()
		return nil, res.StatusCode
	}
	sum := sha1.Sum([]byte(key + wsGUID))
	if res.Header.Get("Sec-WebSocket-Accept") != base64.StdEncoding.EncodeToString(sum[:]) {
		l.t.Errorf("Sec-WebSocket-Accept %q", res.Header.Get("Sec-WebSocket-Accept"))
	}
	op, id, body := w.recv()
	if op != opHello || id != 0 || binary.BigEndian.Uint16(body) != relayVer {
		l.t.Fatalf("HELLO: %x %d %x", op, id, body)
	}
	return w, 101
}

func (w *ws) send(op byte, id uint32, body ...byte) {
	payload := append(binary.BigEndian.AppendUint32([]byte{op}, id), body...)
	mask := make([]byte, 4)
	rand.Read(mask)
	n := len(payload)
	head := []byte{0x82}
	switch {
	case n < 126:
		head = append(head, 0x80|byte(n))
	case n < 65536:
		head = binary.BigEndian.AppendUint16(append(head, 0x80|126), uint16(n))
	default:
		head = binary.BigEndian.AppendUint64(append(head, 0x80|127), uint64(n))
	}
	for i := range payload {
		payload[i] ^= mask[i%4]
	}
	w.c.Write(append(append(head, mask...), payload...))
}

func (w *ws) frame() (byte, []byte) {
	w.t.Helper()
	w.c.SetReadDeadline(time.Now().Add(10 * time.Second))
	h := make([]byte, 2)
	if _, err := io.ReadFull(w.r, h); err != nil {
		w.t.Fatalf("a frame: %v", err)
	}
	n := uint64(h[1] & 0x7f)
	switch n {
	case 126:
		b := make([]byte, 2)
		io.ReadFull(w.r, b)
		n = uint64(binary.BigEndian.Uint16(b))
	case 127:
		b := make([]byte, 8)
		io.ReadFull(w.r, b)
		n = binary.BigEndian.Uint64(b)
	}
	p := make([]byte, n)
	if _, err := io.ReadFull(w.r, p); err != nil {
		w.t.Fatalf("a frame's payload: %v", err)
	}
	return h[0] & 0x0f, p
}

// recv: the next relay message; pings skipped; a close fails the test.
func (w *ws) recv() (byte, uint32, []byte) {
	w.t.Helper()
	for {
		opcode, p := w.frame()
		switch opcode {
		case 0x2:
			return p[0], binary.BigEndian.Uint32(p[1:5]), p[5:]
		case 0x8:
			w.t.Fatalf("closed: %q", p)
		}
	}
}

func (w *ws) expect(op byte, id uint32, body ...byte) {
	w.t.Helper()
	gop, gid, gbody := w.recv()
	if gop != op || gid != id || (body != nil && !bytes.Equal(gbody, body)) {
		w.t.Errorf("got %x %d %x, want %x %d %x", gop, gid, gbody, op, id, body)
	}
}

func (w *ws) close() {
	w.c.Write([]byte{0x88, 0x80, 1, 2, 3, 4})
	w.c.Close()
}

func addrPort(ip uint32, port int) []byte {
	return binary.BigEndian.AppendUint16(binary.BigEndian.AppendUint32(nil, ip), uint16(port))
}

func (l *live) relayPolicy(kv ...any) {
	l.t.Helper()
	n := map[string]any{"relay_ports": []any{"1-65535"}, "relay_allow_private": true,
		"relay_max_sockets": 32, "relay_max_connections": 4}
	for i := 0; i+1 < len(kv); i += 2 {
		n["relay_"+kv[i].(string)] = kv[i+1]
	}
	if code := l.status("POST", "/api/settings", map[string]any{"network": n}); code != 200 {
		l.t.Fatalf("the relay's policy: %d", code)
	}
}

// echo: TCP and UDP echo on one loopback port.
func echo(t *testing.T) int {
	tl, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	port := tl.Addr().(*net.TCPAddr).Port
	ul, err := net.ListenPacket("udp", fmt.Sprintf("127.0.0.1:%d", port))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { tl.Close(); ul.Close() })
	go func() {
		for {
			c, err := tl.Accept()
			if err != nil {
				return
			}
			go func() { io.Copy(c, c); c.Close() }()
		}
	}()
	go func() {
		buf := make([]byte, 65536)
		for {
			n, a, err := ul.ReadFrom(buf)
			if err != nil {
				return
			}
			ul.WriteTo(buf[:n], a)
		}
	}()
	return port
}

func TestRelayTCP(t *testing.T) {
	l := liveServer(t)
	l.relayPolicy()
	port := echo(t)
	w, _ := l.relayOpen("http://" + l.addr)
	w.send(opConnect, 7, addrPort(0x7F000001, port)...)
	w.expect(opConnected, 7)
	w.send(opSend, 7, []byte("hello relay")...)
	var got []byte
	acked := 0
	for len(got) < 11 || acked < 11 {
		op, id, body := w.recv()
		if id != 7 {
			t.Fatalf("id %d", id)
		}
		if op == opAck {
			acked += int(binary.BigEndian.Uint32(body))
		} else if op == opData {
			got = append(got, body...)
		} else {
			t.Fatalf("op %x", op)
		}
	}
	if string(got) != "hello relay" {
		t.Errorf("echoed %q", got)
	}
	w.send(opCredit, 7, binary.BigEndian.AppendUint32(nil, uint32(len(got)))...)
	// More than a window, credited as it comes: flow control both ways.
	big := make([]byte, 3*window)
	rand.Read(big)
	sent, acked := 0, 0
	got = nil
	for len(got) < len(big) {
		if sent < len(big) && sent-acked < window {
			n := min(window-(sent-acked), 16384, len(big)-sent)
			w.send(opSend, 7, big[sent:sent+n]...)
			sent += n
			continue
		}
		op, _, body := w.recv()
		if op == opAck {
			acked += int(binary.BigEndian.Uint32(body))
		} else if op == opData {
			got = append(got, body...)
			w.send(opCredit, 7, binary.BigEndian.AppendUint32(nil, uint32(len(body)))...)
		}
	}
	if !bytes.Equal(got, big) {
		t.Error("3 windows echoed: not the same")
	}
	// Our FIN: the echo server closes, EOF.
	w.send(opShutdown, 7)
	op, _, _ := w.recv()
	for op == opAck {
		op, _, _ = w.recv()
	}
	if op != opEOF {
		t.Errorf("after SHUTDOWN: %x", op)
	}
	w.send(opClose, 7, 0)
	// Refused: nothing listens.
	free, _ := net.Listen("tcp", "127.0.0.1:0")
	freePort := free.Addr().(*net.TCPAddr).Port
	free.Close()
	w.send(opConnect, 8, addrPort(0x7F000001, freePort)...)
	w.expect(opError, 8, errRefused)
	w.close()
	// Denied: loopback, a private host, a port off the list.
	l.relayPolicy("ports", []any{80, "8000-8001"}, "allow_private", false)
	w, _ = l.relayOpen("")
	w.send(opConnect, 1, addrPort(0x7F000001, 80)...)
	w.expect(opError, 1, errDenied)
	w.send(opConnect, 2, addrPort(0xC0A80001, 8001)...)
	w.expect(opError, 2, errDenied)
	w.send(opConnect, 3, addrPort(0x08080808, 25)...)
	w.expect(opError, 3, errDenied)
	w.close()
}

func TestRelayUDPDNSLimits(t *testing.T) {
	l := liveServer(t)
	l.relayPolicy("max_sockets", 2, "max_connections", 1)
	port := echo(t)
	w, _ := l.relayOpen("")
	w.send(opUDP, 1)
	w.send(opSendTo, 1, append(addrPort(0x7F000001, port), "a datagram"...)...)
	w.expect(opDataFrom, 1, append(addrPort(0x7F000001, port), "a datagram"...)...)
	w.send(opLookup, 5, []byte("localhost")...)
	op, id, body := w.recv()
	found := false
	for i := 1; i+4 <= len(body); i += 4 {
		found = found || binary.BigEndian.Uint32(body[i:]) == 0x7F000001
	}
	if op != opAddrs || id != 5 || body[0] != 0 || !found {
		t.Errorf("localhost: %x %d %x", op, id, body)
	}
	w.send(opLookup, 6, []byte("no-such-host.invalid")...)
	if op, id, _ := w.recv(); op != opAddrs || id != 6 {
		t.Errorf("no such host: %x %d", op, id)
	}
	// Two sockets: a third is refused (a second WebSocket: TestRelaySecondRefused).
	w.send(opUDP, 2)
	w.send(opUDP, 3)
	w.expect(opError, 3, errTooMany)
	// A frame without a mask ends the connection (1002); its sockets go.
	w.c.Write(append([]byte{0x82, 5, opClose}, 0, 0, 0, 1))
	opc, p := w.frame()
	if opc != 0x8 || binary.BigEndian.Uint16(p) != 1002 {
		t.Errorf("unmasked: %x %x", opc, p)
	}
	w.c.Close()
	for i := 0; i < 50; i++ {
		if _, s := l.s.relay.counts(); s == 0 {
			return
		}
		time.Sleep(100 * time.Millisecond)
	}
	t.Error("the sockets outlived their WebSocket")
}

func TestRelaySecondRefused(t *testing.T) {
	l := liveServer(t)
	l.relayPolicy("max_connections", 1)
	w, _ := l.relayOpen("")
	defer w.close()
	c, _ := net.Dial("tcp", l.addr)
	defer c.Close()
	fmt.Fprintf(c, "GET /relay HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"+
		"Sec-WebSocket-Key: eA==\r\nCookie: %s=%s\r\n\r\n", l.addr, cookieName(l.addr), l.token)
	c.SetDeadline(time.Now().Add(10 * time.Second))
	data, _ := io.ReadAll(c)
	if !bytes.Contains(data, []byte("\x88")) || !bytes.HasSuffix(data, []byte("too many relay connections")) {
		t.Errorf("a second WebSocket: %q", data)
	}
}

func TestRelayShortCredit(t *testing.T) {
	l := liveServer(t)
	l.relayPolicy()
	w, _ := l.relayOpen("")
	w.send(opUDP, 1)
	w.send(opCredit, 1, 0, 0) // a u32 cut short
	for {
		opc, p := w.frame()
		if opc == 0x8 {
			if binary.BigEndian.Uint16(p) != 1008 {
				t.Errorf("closed with %x", p)
			}
			break
		}
	}
	w.c.Close()
}

// ---- the program ----

// program: this test binary as executor-standalone (TestMain) with args;
// the lines it printed up to its "Open ADDRESS" one.
func program(t *testing.T, args ...string) (*exec.Cmd, []string) {
	t.Helper()
	cmd := exec.Command(os.Args[0], "-test.run=^$")
	cmd.Env = append(os.Environ(), "EXECUTOR_STANDALONE_MAIN=1", "EXECUTOR_STANDALONE_ARGS="+strings.Join(args, " "))
	out, _ := cmd.StdoutPipe()
	cmd.Stderr = os.Stderr
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { cmd.Process.Kill(); cmd.Wait() })
	var lines []string
	sc := bufio.NewScanner(out)
	for sc.Scan() {
		lines = append(lines, sc.Text())
		if strings.HasPrefix(sc.Text(), "Open ") {
			break
		}
	}
	go io.Copy(io.Discard, out)
	return cmd, lines
}

var openLine = regexp.MustCompile(`^Open (https?://127\.0\.0\.1:(\d+)/) \(`)

func TestProgram(t *testing.T) {
	data := t.TempDir()
	site := t.TempDir()
	os.WriteFile(filepath.Join(site, "index.html"), []byte("<p>tab</p>"), 0o644)
	images := filepath.Join(data, "images")
	os.MkdirAll(images, 0o755)
	disk := bytes.Repeat([]byte("0123456789abcdef"), 4096)
	os.WriteFile(filepath.Join(images, "a.img"), disk, 0o644)
	os.WriteFile(filepath.Join(images, "a.img.json"), []byte(`{"title":"A"}`), 0o644)
	// The pairing builds' files go.
	os.WriteFile(filepath.Join(data, "pair.key"), []byte("0123456789abcdef0123456789abcdef\n"), 0o600)
	os.WriteFile(filepath.Join(data, "paired.json"), []byte("{}\n"), 0o600)
	base := []string{"--data", data, "--site", site, "--images", images, "--bind", "127.0.0.1", "--port", "0", "--quiet"}

	// --tls auto: HTTPS with the certificate it made, pinned.
	start := func() (*exec.Cmd, string) {
		cmd, lines := program(t, append(base, "--tls", "auto")...)
		var m []string
		for _, line := range lines {
			if m = openLine.FindStringSubmatch(line); m != nil {
				break
			}
		}
		if m == nil || !strings.HasPrefix(m[1], "https://") {
			t.Fatalf("printed: %q", lines)
		}
		return cmd, m[1]
	}
	first, url := start()
	for _, f := range []string{"pair.key", "paired.json"} {
		if _, err := os.Stat(filepath.Join(data, f)); err == nil {
			t.Errorf("%s still there", f)
		}
	}
	pem, err := os.ReadFile(filepath.Join(data, "tls", "cert.pem"))
	if err != nil {
		t.Fatal(err)
	}
	pool := x509.NewCertPool()
	pool.AppendCertsFromPEM(pem)
	client := &http.Client{Transport: &http.Transport{TLSClientConfig: &tls.Config{RootCAs: pool}},
		CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }}
	get := func(path, cookie string, headers ...string) (*http.Response, []byte) {
		r, _ := http.NewRequest("GET", url+strings.TrimPrefix(path, "/"), nil)
		if cookie != "" {
			r.Header.Set("Cookie", cookie)
		}
		for _, h := range headers {
			k, v, _ := strings.Cut(h, ": ")
			r.Header.Set(k, v)
		}
		res, err := client.Do(r)
		if err != nil {
			t.Fatal(err)
		}
		b, _ := io.ReadAll(res.Body)
		res.Body.Close()
		return res, b
	}
	// The first load claims it, with a Secure cookie.
	res, body := get("/", "")
	setCookie := res.Header.Get("Set-Cookie")
	if res.StatusCode != 200 || string(body) != "<p>tab</p>" || !strings.Contains(setCookie, "Secure") {
		t.Fatalf("the first load over HTTPS: %d %q %q", res.StatusCode, body, setCookie)
	}
	cookie, _, _ := strings.Cut(setCookie, ";")
	if res, body := get("/disks/a.img", cookie, "Range: bytes=100-199"); res.StatusCode != 206 || !bytes.Equal(body, disk[100:200]) {
		t.Errorf("a range over HTTPS: %d", res.StatusCode)
	}
	if v := func() map[string]any {
		_, b := get("/api/server", cookie)
		var v map[string]any
		json.Unmarshal(b, &v)
		return v
	}(); v["tls"] != true || v["standalone"] != true {
		t.Errorf("/api/server: %v", v)
	}
	// One connected and silent (no ClientHello), one speaking plain HTTP:
	// HTTPS answered meanwhile (another browser: in use); the plain one
	// closed, not hung.
	host := strings.TrimSuffix(strings.TrimPrefix(url, "https://"), "/")
	silent, _ := net.Dial("tcp", host)
	defer silent.Close()
	plain, _ := net.Dial("tcp", host)
	defer plain.Close()
	io.WriteString(plain, "GET / HTTP/1.1\r\nHost: x\r\n\r\n")
	for i := 0; i < 3; i++ {
		if res, body := get("/", ""); res.StatusCode != 403 || !bytes.Contains(body, []byte(`id="in-use"`)) {
			t.Errorf("HTTPS beside a silent client, another browser: %d", res.StatusCode)
		}
	}
	plain.SetDeadline(time.Now().Add(10 * time.Second))
	got, err := io.ReadAll(plain)
	if bytes.Contains(got, []byte("<p>tab</p>")) || errors.Is(err, os.ErrDeadlineExceeded) {
		t.Errorf("plain HTTP to HTTPS: %q %v", got, err)
	}

	// A restart frees it: the old cookie opens nothing (401: reload), and
	// the reload claims it again.
	first.Process.Kill()
	first.Wait()
	_, url = start()
	if res, _ := get("/api/me", cookie); res.StatusCode != 401 {
		t.Errorf("the old cookie after a restart: %d", res.StatusCode)
	}
	res, _ = get("/", cookie)
	again, _, _ := strings.Cut(res.Header.Get("Set-Cookie"), ";")
	if res.StatusCode != 200 || again == "" || again == cookie {
		t.Errorf("the reload after a restart: %d %q", res.StatusCode, again)
	}
	if res, _ := get("/api/me", again); res.StatusCode != 200 {
		t.Errorf("the new cookie: %d", res.StatusCode)
	}
}
