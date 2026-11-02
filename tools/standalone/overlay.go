package main

// Overlays: a disk's changed 4 KB blocks (web/executor/overlay.js), by
// the tab's key, versioned: every upload
// is a new version and its blocks carry it, so a tab that has version V
// asks for the blocks newer than V; an upload names the version it is on
// (base) and is refused (409 { version }) when another came in between.
// Large uploads are staged and committed as one version; a replace drops
// the blocks not in it, and so does a delete (Revert): both are a reset,
// and a tab older than it drops its own blocks first.
//
// Each overlay is one file, KEY.overlay: beside its image when KEY is a
// library disk's (NAME-ID, main.js) and the images directory takes it,
// else in DATA/overlays. The file is a log:
//
//	"XOVL" u32 format, then records of a 12-byte head (kind, a, b; u32 LE):
//	  block   a = block number, b = its version; 4096 bytes follow
//	  meta    a = length; that much JSON follows: { name, size }
//	  commit  a = the version, b = the reset version after it
//
// Records count once a commit follows them: a load drops what comes
// after the last commit (a write cut short), and a commit whose reset
// differs from the one before drops the blocks before it. Each upload is
// appended and synced; the file is written again without the blocks
// superseded once they are most of it (and no download is reading it).
// Merge writes the blocks into the image and removes the overlay.

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	block      = 4096
	record     = 4 + block // the API's: block number (u32 LE), the block
	headLen    = 12
	ovMagic    = "XOVL"
	ovFormat   = 1
	kindBlock  = 1
	kindMeta   = 2
	kindCommit = 3
	stageAge   = 24 * time.Hour
)

var (
	keyRe   = regexp.MustCompile(`^[\w.-]{1,200}$`)
	tokenRe = regexp.MustCompile(`^[\w-]{8,64}$`)
)

func checkKey(key string) error {
	if !keyRe.MatchString(key) {
		return httpErr(400, "bad key")
	}
	return nil
}

func checkRecords(body []byte) error {
	if len(body)%record != 0 {
		return httpErr(400, "records are 4 + 4096 bytes")
	}
	return nil
}

type blockAt struct {
	version uint32
	off     int64 // of its data in the file
}

type overlay struct {
	mu       sync.Mutex
	key      string
	path     string
	fallback string   // where it goes if path can't be made
	f        *os.File // nil: no file yet
	end      int64    // the file's committed length
	version  uint32
	reset    uint32
	name     string
	size     int64
	index    map[uint32]blockAt
	dead     int // superseded blocks in the file
	readers  int
}

type OverlayStore struct {
	mu       sync.Mutex
	data     string
	staging  string
	overlays map[string]*overlay
	// where: the image directory for KEY, if KEY is a library disk's.
	where func(key string) (imageDir, imagePath string)
}

func newOverlayStore(data string, where func(string) (string, string)) *OverlayStore {
	s := &OverlayStore{data: filepath.Join(data, "overlays"), staging: filepath.Join(data, "staging"),
		overlays: map[string]*overlay{}, where: where}
	os.MkdirAll(s.data, 0o755)
	os.MkdirAll(s.staging, 0o755)
	return s
}

// get: the overlay for key, its file read if it has one.
func (s *OverlayStore) get(key string) (*overlay, error) {
	if err := checkKey(key); err != nil {
		return nil, err
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if o := s.overlays[key]; o != nil {
		return o, nil
	}
	o := &overlay{key: key, index: map[uint32]blockAt{}}
	name := key + ".overlay"
	candidates := []string{}
	if dir, _ := s.where(key); dir != "" {
		candidates = append(candidates, filepath.Join(dir, name))
	}
	candidates = append(candidates, filepath.Join(s.data, name))
	for _, p := range candidates {
		if _, err := os.Stat(p); err == nil {
			o.path = p
			if err := o.load(); err != nil {
				return nil, fmt.Errorf("%s: %v", p, err)
			}
			break
		}
	}
	if o.path == "" {
		o.path = candidates[0] // made at the first write (create falls back to DATA)
		o.fallback = filepath.Join(s.data, name)
	}
	s.overlays[key] = o
	return o, nil
}

func (o *overlay) load() error {
	f, err := os.OpenFile(o.path, os.O_RDWR, 0)
	if err != nil {
		return err
	}
	st, err := f.Stat()
	if err != nil {
		f.Close()
		return err
	}
	head := make([]byte, 8)
	if _, err := f.ReadAt(head, 0); err != nil || string(head[:4]) != ovMagic {
		f.Close()
		return errors.New("not an overlay")
	}
	if binary.LittleEndian.Uint32(head[4:]) != ovFormat {
		f.Close()
		return errors.New("an overlay of another format")
	}
	type pend struct {
		b  uint32
		at blockAt
	}
	var pending []pend
	var meta []byte
	at, good := int64(8), int64(8)
	h := make([]byte, headLen)
	for at+headLen <= st.Size() {
		if _, err := f.ReadAt(h, at); err != nil {
			break
		}
		kind, a, b := binary.LittleEndian.Uint32(h), binary.LittleEndian.Uint32(h[4:]), binary.LittleEndian.Uint32(h[8:])
		at += headLen
		switch kind {
		case kindBlock:
			if at+block > st.Size() {
				at = -1
				break
			}
			pending = append(pending, pend{a, blockAt{b, at}})
			at += block
		case kindMeta:
			if at+int64(a) > st.Size() || a > 1<<16 {
				at = -1
				break
			}
			meta = make([]byte, a)
			f.ReadAt(meta, at)
			at += int64(a)
		case kindCommit:
			if b != o.reset {
				o.dead += len(o.index)
				clear(o.index)
				o.reset = b
			}
			for _, p := range pending {
				if _, ok := o.index[p.b]; ok {
					o.dead++
				}
				o.index[p.b] = p.at
			}
			pending = pending[:0]
			if meta != nil {
				o.applyMeta(meta)
				meta = nil
			}
			o.version = a
			good = at
		default:
			at = -1
		}
		if at < 0 {
			break
		}
	}
	if good < st.Size() { // a write cut short
		if err := f.Truncate(good); err != nil {
			f.Close()
			return err
		}
	}
	o.f, o.end = f, good
	return nil
}

func (o *overlay) applyMeta(b []byte) {
	var m struct {
		Name *string `json:"name"`
		Size *int64  `json:"size"`
	}
	if json.Unmarshal(b, &m) == nil {
		if m.Name != nil {
			o.name = *m.Name
		}
		if m.Size != nil {
			o.size = *m.Size
		}
	}
}

func (o *overlay) create() error {
	if o.f != nil {
		return nil
	}
	f, err := os.OpenFile(o.path, os.O_RDWR|os.O_CREATE|os.O_EXCL, 0o644)
	if err != nil && o.fallback != "" && o.fallback != o.path {
		o.path = o.fallback // the images directory is read-only, say
		f, err = os.OpenFile(o.path, os.O_RDWR|os.O_CREATE|os.O_EXCL, 0o644)
	}
	if err != nil {
		return err
	}
	head := make([]byte, 8)
	copy(head, ovMagic)
	binary.LittleEndian.PutUint32(head[4:], ovFormat)
	if _, err := f.Write(head); err != nil {
		f.Close()
		os.Remove(o.path)
		return err
	}
	o.f, o.end = f, 8
	return nil
}

func putHead(buf *bytes.Buffer, kind, a, b uint32) {
	var h [headLen]byte
	binary.LittleEndian.PutUint32(h[:], kind)
	binary.LittleEndian.PutUint32(h[4:], a)
	binary.LittleEndian.PutUint32(h[8:], b)
	buf.Write(h[:])
}

// The blocks newer than since: (version, reset since, their numbers and
// places). The caller reads them with readBlock and then calls done.
func (o *overlay) since(since int64) (version uint32, reset bool, blocks []uint32, places []int64) {
	o.mu.Lock()
	defer o.mu.Unlock()
	for b, at := range o.index {
		if int64(at.version) > since {
			blocks = append(blocks, b)
		}
	}
	sort.Slice(blocks, func(i, j int) bool { return blocks[i] < blocks[j] })
	for _, b := range blocks {
		places = append(places, o.index[b].off)
	}
	if len(blocks) > 0 {
		o.readers++
	}
	return o.version, int64(o.reset) > since, blocks, places
}

func (o *overlay) readBlock(off int64, buf []byte) error {
	_, err := o.f.ReadAt(buf, off) // appends and the file don't move it while readers > 0
	return err
}

func (o *overlay) done() {
	o.mu.Lock()
	defer o.mu.Unlock()
	o.readers--
	o.maybeCompact()
}

// commit appends the records in buf (blocks of version v) and a commit,
// then updates the index; replace: the blocks before are dropped.
func (o *overlay) commit(buf *bytes.Buffer, v uint32, replace bool) error {
	if err := o.create(); err != nil {
		return err
	}
	reset := o.reset
	if replace {
		reset = v
	}
	putHead(buf, kindCommit, v, reset)
	if _, err := o.f.WriteAt(buf.Bytes(), o.end); err != nil {
		o.f.Truncate(o.end)
		return err
	}
	if err := o.f.Sync(); err != nil {
		o.f.Truncate(o.end)
		return err
	}
	if replace {
		o.dead += len(o.index)
		clear(o.index)
	}
	at := o.end
	data := buf.Bytes() // the records again, for the blocks' places
	for p := 0; p+headLen <= len(data); {
		kind, a, bv := binary.LittleEndian.Uint32(data[p:]), binary.LittleEndian.Uint32(data[p+4:]), binary.LittleEndian.Uint32(data[p+8:])
		p += headLen
		switch kind {
		case kindBlock:
			if _, ok := o.index[a]; ok {
				o.dead++
			}
			o.index[a] = blockAt{bv, at + int64(p)}
			p += block
		case kindMeta:
			o.applyMeta(data[p : p+int(a)])
			p += int(a)
		default:
			p = len(data)
		}
	}
	o.end += int64(buf.Len())
	o.version, o.reset = v, reset
	o.maybeCompact()
	return nil
}

// maybeCompact writes the file again with only the live blocks, when the
// superseded ones are most of it and nobody reads it. Under o.mu.
func (o *overlay) maybeCompact() {
	if o.f == nil || o.readers > 0 || o.dead < 256 || o.dead < len(o.index) {
		return
	}
	tmp := o.path + ".tmp"
	f, err := os.OpenFile(tmp, os.O_RDWR|os.O_CREATE|os.O_TRUNC, 0o644)
	if err != nil {
		return
	}
	fail := func() { f.Close(); os.Remove(tmp) }
	var head bytes.Buffer
	head.WriteString(ovMagic)
	binary.Write(&head, binary.LittleEndian, uint32(ovFormat))
	if o.name != "" || o.size != 0 {
		m, _ := json.Marshal(map[string]any{"name": o.name, "size": o.size})
		putHead(&head, kindMeta, uint32(len(m)), 0)
		head.Write(m)
	}
	if _, err := f.Write(head.Bytes()); err != nil {
		fail()
		return
	}
	blocks := make([]uint32, 0, len(o.index))
	for b := range o.index {
		blocks = append(blocks, b)
	}
	sort.Slice(blocks, func(i, j int) bool { return blocks[i] < blocks[j] })
	index := make(map[uint32]blockAt, len(blocks))
	at := int64(head.Len())
	rec := make([]byte, headLen+block)
	for _, b := range blocks {
		e := o.index[b]
		binary.LittleEndian.PutUint32(rec, kindBlock)
		binary.LittleEndian.PutUint32(rec[4:], b)
		binary.LittleEndian.PutUint32(rec[8:], e.version)
		if _, err := o.f.ReadAt(rec[headLen:], e.off); err != nil {
			fail()
			return
		}
		if _, err := f.Write(rec); err != nil {
			fail()
			return
		}
		index[b] = blockAt{e.version, at + headLen}
		at += int64(len(rec))
	}
	var c bytes.Buffer
	putHead(&c, kindCommit, o.version, o.reset)
	if _, err := f.Write(c.Bytes()); err != nil || f.Sync() != nil {
		fail()
		return
	}
	at += headLen
	f.Close()
	o.f.Close()
	if err := os.Rename(tmp, o.path); err != nil {
		os.Remove(tmp)
		o.f, _ = os.OpenFile(o.path, os.O_RDWR, 0)
		return
	}
	nf, err := os.OpenFile(o.path, os.O_RDWR, 0)
	if err != nil {
		o.f = nil
		return
	}
	o.f, o.end, o.index, o.dead = nf, at, index, 0
}

// ---- the API's operations ----

func (s *OverlayStore) stagePath(key, token string) string {
	return filepath.Join(s.staging, key+"."+token)
}

// stage keeps records for a commit to come (put with the token).
func (s *OverlayStore) stage(key, token string, body []byte) error {
	if err := checkKey(key); err != nil {
		return err
	}
	if !tokenRe.MatchString(token) {
		return httpErr(400, "bad stage token")
	}
	if err := checkRecords(body); err != nil {
		return err
	}
	f, err := os.OpenFile(s.stagePath(key, token), os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0o644)
	if err != nil {
		return err
	}
	_, err = f.Write(body)
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	return err
}

func (s *OverlayStore) put(key string, base int64, body []byte, name *string, size *int64, token string, replace bool) (uint32, error) {
	if token != "" && !tokenRe.MatchString(token) {
		return 0, httpErr(400, "bad stage token")
	}
	if err := checkRecords(body); err != nil {
		return 0, err
	}
	o, err := s.get(key)
	if err != nil {
		return 0, err
	}
	o.mu.Lock()
	defer o.mu.Unlock()
	if base != int64(o.version) { // (its staged blocks age out)
		return 0, httpErr(409, "changed since", "version", o.version)
	}
	v := o.version + 1
	var buf bytes.Buffer
	if (name != nil && *name != o.name) || (size != nil && *size != o.size) {
		m := map[string]any{}
		if name != nil {
			m["name"] = *name
		}
		if size != nil {
			m["size"] = *size
		}
		meta, _ := json.Marshal(m)
		putHead(&buf, kindMeta, uint32(len(meta)), 0)
		buf.Write(meta)
	}
	add := func(recs []byte) {
		for i := 0; i+record <= len(recs); i += record {
			putHead(&buf, kindBlock, binary.LittleEndian.Uint32(recs[i:]), v)
			buf.Write(recs[i+4 : i+record])
		}
	}
	if token != "" {
		staged, err := os.ReadFile(s.stagePath(key, token))
		if err != nil && !errors.Is(err, os.ErrNotExist) {
			return 0, err
		}
		add(staged[:len(staged)-len(staged)%record])
	}
	add(body)
	if err := o.commit(&buf, v, replace); err != nil {
		return 0, err
	}
	if token != "" {
		os.Remove(s.stagePath(key, token))
	}
	s.pruneStaged()
	return v, nil
}

func (s *OverlayStore) pruneStaged() {
	ents, _ := os.ReadDir(s.staging)
	for _, e := range ents {
		if info, err := e.Info(); err == nil && time.Since(info.ModTime()) > stageAge {
			os.Remove(filepath.Join(s.staging, e.Name()))
		}
	}
}

// remove: Revert, a version that is a reset.
func (s *OverlayStore) remove(key string) (uint32, error) {
	o, err := s.get(key)
	if err != nil {
		return 0, err
	}
	o.mu.Lock()
	defer o.mu.Unlock()
	v := o.version + 1
	var buf bytes.Buffer
	if err := o.commit(&buf, v, true); err != nil {
		return 0, err
	}
	return v, nil
}

// merge writes the overlay's blocks into its library image and removes
// the overlay: the image's content (and so its id and the overlay key
// the tab has for it) changes.
func (s *OverlayStore) merge(key string) (int, string, error) {
	o, err := s.get(key)
	if err != nil {
		return 0, "", err
	}
	_, image := s.where(key)
	if image == "" {
		return 0, "", httpErr(404, "not a library disk's overlay (or the disk has changed)")
	}
	o.mu.Lock()
	defer o.mu.Unlock()
	if o.readers > 0 {
		return 0, "", httpErr(409, "being read: try again")
	}
	img, err := os.OpenFile(image, os.O_RDWR, 0)
	if err != nil {
		return 0, "", httpErr(403, "the image can't be written: "+err.Error())
	}
	st, _ := img.Stat()
	buf := make([]byte, block)
	n := 0
	for b, at := range o.index {
		if int64(b)*block >= st.Size() {
			continue
		}
		if err := o.readBlock(at.off, buf); err != nil {
			img.Close()
			return n, "", err
		}
		if _, err := img.WriteAt(buf, int64(b)*block); err != nil {
			img.Close()
			return n, "", err
		}
		n++
	}
	if err := img.Sync(); err != nil {
		img.Close()
		return n, "", err
	}
	img.Close()
	if o.f != nil {
		o.f.Close()
		os.Remove(o.path)
	}
	s.mu.Lock()
	delete(s.overlays, key)
	s.mu.Unlock()
	return n, image, nil
}

type overlaySummary struct {
	Key     string `json:"key"`
	Name    string `json:"name"`
	Size    int64  `json:"size"`
	Version uint32 `json:"version"`
	Blocks  int    `json:"blocks"`
}

// summary: every overlay there is a file of, in DATA and beside images.
func (s *OverlayStore) summary(imageDirs ...string) []overlaySummary {
	keys := map[string]bool{}
	for _, dir := range append([]string{s.data}, imageDirs...) {
		ents, _ := os.ReadDir(dir)
		for _, e := range ents {
			if k, ok := strings.CutSuffix(e.Name(), ".overlay"); ok && keyRe.MatchString(k) {
				keys[k] = true
			}
		}
	}
	s.mu.Lock()
	for k := range s.overlays {
		keys[k] = true
	}
	s.mu.Unlock()
	out := []overlaySummary{}
	for k := range keys {
		o, err := s.get(k)
		if err != nil {
			continue
		}
		o.mu.Lock()
		if o.f != nil {
			out = append(out, overlaySummary{k, o.name, o.size, o.version, len(o.index)})
		}
		o.mu.Unlock()
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Key < out[j].Key })
	return out
}

// get's answer to GET: streams the records.
func (s *OverlayStore) serve(c *ctx, key string, since int64) error {
	o, err := s.get(key)
	if err != nil {
		return err
	}
	version, reset, blocks, places := o.since(since)
	if len(blocks) > 0 {
		defer o.done()
	}
	h := c.w.Header()
	h.Set("Cache-Control", "no-store")
	h.Set("Content-Type", "application/octet-stream")
	h.Set("Content-Length", strconv.Itoa(len(blocks)*record))
	h.Set("X-Sync-Version", strconv.FormatUint(uint64(version), 10))
	if reset {
		h.Set("X-Sync-Reset", "1")
	} else {
		h.Set("X-Sync-Reset", "0")
	}
	c.w.WriteHeader(200)
	if c.r.Method == "HEAD" {
		return nil
	}
	buf := make([]byte, record)
	for i, b := range blocks {
		binary.LittleEndian.PutUint32(buf, b)
		if err := o.readBlock(places[i], buf[4:]); err != nil {
			return nil // the length is promised: the connection ends short
		}
		if _, err := c.w.Write(buf); err != nil {
			return nil
		}
	}
	return nil
}
