package main

// The parts the API can't show (api_test.go checks it): the range
// parser, and the overlay log across restarts, a write cut short,
// compaction and merge.

import (
	"bytes"
	"compress/gzip"
	"encoding/binary"
	"net"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strconv"
	"testing"
	"testing/fstest"
)

func TestParseRange(t *testing.T) {
	type r struct {
		s, e      int64
		ok, unsat bool
	}
	for h, want := range map[string]r{
		"bytes=0-99":      {0, 100, true, false},
		"bytes=900-":      {900, 1000, true, false},
		"bytes=-10":       {990, 1000, true, false},
		"bytes=990-5000":  {990, 1000, true, false},
		"bytes=1000-":     {0, 0, false, true},
		"bytes=0-1,5-6":   {0, 0, false, false},
		"bytes=5-3":       {0, 0, false, false},
		"bytes=-0":        {0, 0, false, true},
		" bytes = 4 - 7 ": {4, 8, true, false},
	} {
		s, e, ok, unsat := parseRange(h, 1000)
		if (r{s, e, ok, unsat}) != want {
			t.Errorf("%q: %v, want %v", h, r{s, e, ok, unsat}, want)
		}
	}
}

func recs(pairs ...int) []byte {
	var b []byte
	for i := 0; i < len(pairs); i += 2 {
		b = binary.LittleEndian.AppendUint32(b, uint32(pairs[i]))
		b = append(b, bytes.Repeat([]byte{byte(pairs[i+1])}, block)...)
	}
	return b
}

// blocks: block -> first byte of the blocks newer than since.
func blocks(t *testing.T, s *OverlayStore, key string, since int64) (uint32, bool, map[uint32]byte) {
	o, err := s.get(key)
	if err != nil {
		t.Fatal(err)
	}
	v, reset, bs, places := o.since(since)
	out := map[uint32]byte{}
	buf := make([]byte, block)
	for i, b := range bs {
		if err := o.readBlock(places[i], buf); err != nil {
			t.Fatal(err)
		}
		out[b] = buf[0]
	}
	if len(bs) > 0 {
		o.done()
	}
	return v, reset, out
}

func same(a, b map[uint32]byte) bool {
	if len(a) != len(b) {
		return false
	}
	for k, v := range a {
		if b[k] != v {
			return false
		}
	}
	return true
}

func TestOverlayLog(t *testing.T) {
	dir := t.TempDir()
	images := filepath.Join(dir, "images")
	os.MkdirAll(images, 0o755)
	none := func(string) (string, string) { return "", "" }
	s := newOverlayStore(dir, none)
	name, size := "a.img", int64(1<<20)
	if v, err := s.put("k", 0, recs(0, 1, 7, 2), &name, &size, "", false); err != nil || v != 1 {
		t.Fatal(v, err)
	}
	if _, err := s.put("k", 0, recs(7, 9), nil, nil, "", false); err == nil {
		t.Fatal("base 0 again: no 409")
	}
	s.put("k", 1, recs(7, 3, 9, 4), nil, nil, "", false)
	if err := s.stage("k", "token-1234", recs(1, 5)); err != nil {
		t.Fatal(err)
	}
	s.put("k", 2, recs(2, 6), nil, nil, "token-1234", true) // v3, a reset

	check := func(s *OverlayStore) {
		t.Helper()
		v, reset, got := blocks(t, s, "k", 0)
		if v != 3 || !reset || !same(got, map[uint32]byte{1: 5, 2: 6}) {
			t.Fatalf("v %d reset %v %v", v, reset, got)
		}
		if _, _, got := blocks(t, s, "k", 3); len(got) != 0 {
			t.Fatalf("since 3: %v", got)
		}
		o, _ := s.get("k")
		if o.name != "a.img" || o.size != 1<<20 {
			t.Fatalf("meta %q %d", o.name, o.size)
		}
	}
	check(s)
	check(newOverlayStore(dir, none)) // as read again at the next start

	// A write cut short (no commit after it) is dropped at the next load.
	path := filepath.Join(dir, "overlays", "k.overlay")
	before, _ := os.Stat(path)
	f, _ := os.OpenFile(path, os.O_WRONLY|os.O_APPEND, 0)
	var torn bytes.Buffer
	putHead(&torn, kindBlock, 50, 4)
	torn.Write(make([]byte, 1000))
	f.Write(torn.Bytes())
	f.Close()
	check(newOverlayStore(dir, none))
	if after, _ := os.Stat(path); after.Size() != before.Size() {
		t.Fatalf("not truncated: %d, was %d", after.Size(), before.Size())
	}

	// Revert: a reset with nothing.
	s = newOverlayStore(dir, none)
	if v, _ := s.remove("k"); v != 4 {
		t.Fatal(v)
	}
	if v, reset, got := blocks(t, newOverlayStore(dir, none), "k", 3); v != 4 || !reset || len(got) != 0 {
		t.Fatal(v, reset, got)
	}
}

func TestOverlayCompaction(t *testing.T) {
	dir := t.TempDir()
	s := newOverlayStore(dir, func(string) (string, string) { return "", "" })
	v := uint32(0)
	for round := 0; round < 4; round++ { // the same 200 blocks again and again
		var body []byte
		for b := 0; b < 200; b++ {
			body = append(body, recs(b, round+1)...)
		}
		var err error
		if v, err = s.put("c", int64(v), body, nil, nil, "", false); err != nil {
			t.Fatal(err)
		}
	}
	st, _ := os.Stat(filepath.Join(dir, "overlays", "c.overlay"))
	if st.Size() > 450*(headLen+block) { // 800 written: compacted at the third, 200 dead since
		t.Fatalf("not compacted: %d bytes", st.Size())
	}
	for _, s := range []*OverlayStore{s, newOverlayStore(dir, func(string) (string, string) { return "", "" })} {
		ver, _, got := blocks(t, s, "c", 0)
		if ver != 4 || len(got) != 200 || got[0] != 4 || got[199] != 4 {
			t.Fatalf("v %d: %d blocks, %v %v", ver, len(got), got[0], got[199])
		}
		if _, _, got := blocks(t, s, "c", 3); len(got) != 200 {
			t.Fatalf("since 3: %d blocks (versions kept?)", len(got))
		}
	}
}

func TestOverlayBesideImageAndMerge(t *testing.T) {
	dir := t.TempDir()
	images := filepath.Join(dir, "images")
	os.MkdirAll(images, 0o755)
	image := filepath.Join(images, "a.img")
	os.WriteFile(image, make([]byte, 64*block), 0o644)
	key := overlayKey("a.img", "0123")
	where := func(k string) (string, string) {
		if k == key {
			return images, image
		}
		return "", ""
	}
	s := newOverlayStore(dir, where)
	if _, err := s.put(key, 0, recs(3, 7, 63, 8), nil, nil, "", false); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(images, key+".overlay")); err != nil {
		t.Fatal("not beside the image:", err)
	}
	n, _, err := s.merge(key)
	if err != nil || n != 2 {
		t.Fatal(n, err)
	}
	b, _ := os.ReadFile(image)
	if b[3*block] != 7 || b[63*block+block-1] != 8 || b[0] != 0 {
		t.Fatal("not merged")
	}
	if _, err := os.Stat(filepath.Join(images, key+".overlay")); err == nil {
		t.Fatal("the overlay is still there")
	}
}

// A file the embedded site has only as NAME.gz: gzip as it is, else
// decompressed, ranges too.
func TestGzipOnly(t *testing.T) {
	plain := bytes.Repeat([]byte("executor "), 5000)
	var z bytes.Buffer
	zw := gzip.NewWriter(&z)
	zw.Write(plain)
	zw.Close()
	fsys := fstest.MapFS{"m.wasm.gz": {Data: z.Bytes()}}
	get := func(h map[string]string) *httptest.ResponseRecorder {
		r := httptest.NewRequest("GET", "/m.wasm", nil)
		for k, v := range h {
			r.Header.Set(k, v)
		}
		w := httptest.NewRecorder()
		if err := sendFile(&ctx{w: w, r: r}, source{fsys, "m.wasm"}, "", nil); err != nil {
			t.Fatal(err)
		}
		return w
	}
	w := get(map[string]string{"Accept-Encoding": "gzip"})
	if w.Header().Get("Content-Encoding") != "gzip" || !bytes.Equal(w.Body.Bytes(), z.Bytes()) ||
		w.Header().Get("X-Decoded-Length") != strconv.Itoa(len(plain)) || w.Header().Get("Content-Type") != "application/wasm" {
		t.Fatalf("gzip: %v", w.Header())
	}
	if w = get(nil); !bytes.Equal(w.Body.Bytes(), plain) || w.Header().Get("Content-Length") != strconv.Itoa(len(plain)) {
		t.Fatalf("plain: %d bytes, %v", w.Body.Len(), w.Header())
	}
	w = get(map[string]string{"Range": "bytes=9-17", "Accept-Encoding": "gzip"})
	if w.Code != 206 || w.Body.String() != "executor " {
		t.Fatalf("range: %d %q", w.Code, w.Body.String())
	}
}

// ... and through the server's static path, which looks for NAME first.
func TestGzipOnlyServed(t *testing.T) {
	var z bytes.Buffer
	zw := gzip.NewWriter(&z)
	zw.Write([]byte("// the module's glue\n"))
	zw.Close()
	s := testServer(t, fstest.MapFS{"executor-web.js.gz": {Data: z.Bytes()}, "index.html": {Data: []byte("<p>tab</p>")}})
	token := claimAs(t, s)
	for path, want := range map[string]string{"/executor-web.js": "// the module's glue\n", "/": "<p>tab</p>"} {
		if a := do(s, "GET", path, "", token); a.code != 200 || a.body != want {
			t.Errorf("%s: %d %q", path, a.code, a.body)
		}
	}
}

// A controller silent for the lease has let go; a viewer then gets control.
func TestSessionLease(t *testing.T) {
	var s TabSessions
	if !s.beat("tab-a-00001", false)["control"].(bool) || s.beat("tab-b-00001", false)["control"].(bool) {
		t.Fatal("first tab not in control")
	}
	if !s.putScreen("tab-a-00001", []byte("png")) || s.getScreen() == nil {
		t.Fatal("screen")
	}
	s.seen["tab-a-00001"] = s.seen["tab-a-00001"].Add(-sessionLease) // A's tab closed without a word
	if a := s.beat("tab-b-00001", false); !a["control"].(bool) || a["viewers"].(int) != 0 {
		t.Fatalf("after the lease: %v", a)
	}
	if s.getScreen() != nil {
		t.Fatal("A's screen outlived it")
	}
}

func TestBrowserURL(t *testing.T) {
	for addr, want := range map[string]string{
		"127.0.0.1:8090": "http://127.0.0.1:8090/", "0.0.0.0:8090": "http://127.0.0.1:8090/",
		"[::]:9000": "http://127.0.0.1:9000/", "[::1]:9000": "http://[::1]:9000/",
	} {
		a, _ := net.ResolveTCPAddr("tcp", addr)
		if got := browserURL("http", a); got != want {
			t.Errorf("%s: %s, want %s", addr, got, want)
		}
	}
}
