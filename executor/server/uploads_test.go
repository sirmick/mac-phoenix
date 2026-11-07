package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// An upload is in the library as uploads/NAME, served with its id,
// overlaid beside it, and gone with its overlay when deleted.
func TestUploads(t *testing.T) {
	s := testServer(t, nil)
	token := claimAs(t, s)
	images := s.settings().Images
	image := bytes.Repeat([]byte("disk"), 16<<10)
	sum := sha256.Sum256(image)
	id := hex.EncodeToString(sum[:])[:24]

	a := do(s, "POST", "/api/uploads/My%20Disk.dsk", string(image), token, "Content-Type: application/octet-stream")
	if a.code != 201 {
		t.Fatalf("upload: %d %s", a.code, a.body)
	}
	v := a.json()
	if v["url"] != "disks/uploads/My%20Disk.dsk" || v["title"] != "My Disk.dsk" || v["size"] != float64(len(image)) ||
		strings.Join(toStrings(v["tags"]), ",") != "uploaded" {
		t.Errorf("entry: %v", v)
	}
	if got, _ := os.ReadFile(filepath.Join(images, "uploads", "My Disk.dsk")); !bytes.Equal(got, image) {
		t.Errorf("the image: %d bytes", len(got))
	}
	if !strings.Contains(do(s, "GET", "/library.json", "", token).body, `"disks/uploads/My%20Disk.dsk"`) {
		t.Errorf("not in library.json")
	}
	d := do(s, "GET", "/disks/uploads/My%20Disk.dsk", "", token, "Range: bytes=0-3")
	if d.code != 206 || d.body != "disk" || d.h.Get("X-Image-Id") != id {
		t.Errorf("served: %d %q %v", d.code, d.body, d.h)
	}

	// Its overlay is beside it.
	key := overlayKey("My Disk.dsk", id)
	if a := do(s, "POST", "/api/sync/overlays/"+key+"?base=0&name=My%20Disk.dsk&size=65536", string(rec(0, 1)), token); a.code != 200 {
		t.Fatalf("overlay: %d %s", a.code, a.body)
	}
	if _, err := os.Stat(filepath.Join(images, "uploads", key+".overlay")); err != nil {
		t.Errorf("overlay not beside it: %v", err)
	}
	if !strings.Contains(do(s, "GET", "/api/sync", "", token).body, key) {
		t.Errorf("overlay not in the summary")
	}

	// Again: refused without replace; replace drops the old overlay.
	if a := do(s, "POST", "/api/uploads/My%20Disk.dsk", "other", token); a.code != 409 {
		t.Errorf("again: %d", a.code)
	}
	if a := do(s, "POST", "/api/uploads/My%20Disk.dsk?replace=1", "other", token); a.code != 201 || a.json()["size"] != 5.0 {
		t.Errorf("replace: %d %s", a.code, a.body)
	}
	if _, err := os.Stat(filepath.Join(images, "uploads", key+".overlay")); err == nil {
		t.Errorf("the old overlay kept")
	}

	if a := do(s, "DELETE", "/api/uploads/My%20Disk.dsk", "", token); a.code != 204 {
		t.Errorf("delete: %d %s", a.code, a.body)
	}
	if a := do(s, "DELETE", "/api/uploads/My%20Disk.dsk", "", token); a.code != 404 {
		t.Errorf("delete again: %d", a.code)
	}
	ents, _ := os.ReadDir(filepath.Join(images, "uploads"))
	if len(ents) != 0 || strings.Contains(do(s, "GET", "/library.json", "", token).body, "uploads/") {
		t.Errorf("left: %v", ents)
	}
}

func TestUploadRefused(t *testing.T) {
	s := testServer(t, nil)
	token := claimAs(t, s)
	for _, name := range []string{"a.txt", ".hidden.img", "..img", "x.img.json", "a%2Fb.img", "a%5Cb.img", "%2E%2E%2Fx.img", "img"} {
		if a := do(s, "POST", "/api/uploads/"+name, "data", token); a.code != 400 && a.code != 404 && a.code != 405 { // 405: a / is no route
			t.Errorf("%s: %d %s", name, a.code, a.body)
		}
	}
	if a := do(s, "POST", "/api/uploads/a.img", "data", token); a.code != 409 {
		t.Errorf("the library's own name: %d", a.code)
	}
	if a := do(s, "POST", "/api/uploads/e.img", "", token); a.code != 400 {
		t.Errorf("empty: %d", a.code)
	}
	if a := do(s, "POST", "/api/uploads/e.img", "x", token, "Origin: https://elsewhere.example"); a.code != 403 {
		t.Errorf("another site: %d", a.code)
	}
	if a := do(s, "DELETE", "/api/uploads/..%2Fa.img", "", token); a.code != 400 && a.code != 404 && a.code != 405 { // 405: a / is no route
		t.Errorf("delete outside: %d", a.code)
	}
	if _, err := os.Stat(filepath.Join(s.settings().Images, "a.img")); err != nil {
		t.Errorf("the library's image: %v", err)
	}

	// The size limit, by Content-Length and by what came.
	doc := defaultDoc()
	doc["disks"].(map[string]any)["images"] = s.settings().Images
	doc["disks"].(map[string]any)["max_image_mb"] = 1.0
	s.applySettings(doc)
	big := strings.Repeat("x", 1<<20+1)
	if a := do(s, "POST", "/api/uploads/big.img", big, token); a.code != 413 || a.h.Get("Connection") != "close" {
		t.Errorf("too large: %d %v", a.code, a.h)
	}
	ents, _ := os.ReadDir(filepath.Join(s.settings().Images, "uploads"))
	if len(ents) != 0 {
		t.Errorf("left: %v", ents)
	}
}

func toStrings(v any) []string {
	out := []string{}
	l, _ := v.([]any)
	for _, x := range l {
		s, _ := x.(string)
		out = append(out, s)
	}
	return out
}
