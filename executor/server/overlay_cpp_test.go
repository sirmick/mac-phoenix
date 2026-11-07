package main

// An overlay the desktop application wrote (executor/host/overlay.cpp:
// executor/tests/fixtures/overlay/cpp.overlay, a 64 KB image whose block n
// is all 'a'+n; block 2 written all 'P', then 10 'Q's at 9*4096+5, one
// version each) reads here as the tab's would.

import (
	"bytes"
	"path/filepath"
	"testing"
)

func TestOverlayFromTheDesktop(t *testing.T) {
	dir := filepath.Join("..", "tests", "fixtures", "overlay")
	s := newOverlayStore(t.TempDir(), func(string) (string, string) { return dir, "" })
	o, err := s.get("cpp")
	if err != nil {
		t.Fatal(err)
	}
	v, _, blocks, places := o.since(0)
	defer o.done()
	if v != 2 || len(blocks) != 2 || blocks[0] != 2 || blocks[1] != 9 {
		t.Fatalf("version %d blocks %v", v, blocks)
	}
	if o.name != "base.img" || o.size != 65536 {
		t.Fatalf("meta %q %d", o.name, o.size)
	}
	buf := make([]byte, block)
	if err := o.readBlock(places[0], buf); err != nil || !bytes.Equal(buf, bytes.Repeat([]byte{'P'}, block)) {
		t.Fatalf("block 2: %v %q", err, buf[:8])
	}
	want := bytes.Repeat([]byte{'a' + 9}, block)
	copy(want[5:], bytes.Repeat([]byte{'Q'}, 10))
	if err := o.readBlock(places[1], buf); err != nil || !bytes.Equal(buf, want) {
		t.Fatalf("block 9: %v %q", err, buf[:16])
	}
}
