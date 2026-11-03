package main

// The library: every image in the images
// directory with a record (NAME.img.json: title, tags, note) that the
// include/exclude patterns let through is in /library.json and served at
// disks/NAME; a record's "cover", a picture beside the image, at
// covers/NAME.
//
// Each disk's id is its content's (SHA-256, the first 24 digits): its
// ETag and X-Image-Id; the tab keys a disk's overlay on it (NAME-ID).
// Images uploaded through the tab (uploads.go) are in IMAGES/uploads,
// named uploads/NAME.
// Ids are kept in DATA/image-ids.json by path, size and time, worked out
// on first use (warm: the library's at start, in the background).

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"io"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"sync"
)

type Disk struct {
	Name  string   `json:"-"`
	Title string   `json:"title"`
	URL   string   `json:"url"`
	Note  string   `json:"note"`
	Tags  []string `json:"tags"`
	Size  int64    `json:"size"`
	Cover string   `json:"cover,omitempty"` // covers/NAME
	// The Systems the disk is for (make_library.py's record), shown in the
	// Disks panel's library table
	Systems []string `json:"systems,omitempty"`
	cover   string
}

func catalog(images string, include, exclude []string) []Disk {
	out := []Disk{}
	if images == "" {
		return out
	}
	match := func(name string, pats []string) bool {
		for _, p := range pats {
			if ok, _ := filepath.Match(p, name); ok {
				return true
			}
		}
		return false
	}
	out = catalogDir(out, images, "", func(name string) bool { return match(name, include) && !match(name, exclude) })
	// Uploads were sent to be offered: any image's name.
	return catalogDir(out, filepath.Join(images, uploadsDir), uploadsDir+"/", func(name string) bool { return uploadName.MatchString(name) })
}

// catalogDir: dir's images that offer lets through, named prefix+NAME.
func catalogDir(out []Disk, dir, prefix string, offer func(name string) bool) []Disk {
	ents, err := os.ReadDir(dir)
	if err != nil {
		return out
	}
	names := make([]string, 0, len(ents))
	for _, e := range ents {
		names = append(names, e.Name())
	}
	sort.Strings(names)
	for _, rec := range names {
		name, ok := strings.CutSuffix(rec, ".json")
		if !ok {
			continue
		}
		st, err := os.Stat(filepath.Join(dir, name))
		if err != nil || !st.Mode().IsRegular() || !offer(name) {
			continue
		}
		b, err := os.ReadFile(filepath.Join(dir, rec))
		if err != nil {
			continue
		}
		var r struct {
			Title   string   `json:"title"`
			Note    string   `json:"note"`
			Tags    []string `json:"tags"`
			Cover   string   `json:"cover"`
			Systems []string `json:"systems"`
		}
		if json.Unmarshal(b, &r) != nil {
			continue
		}
		if r.Title == "" {
			r.Title = name
		}
		if r.Tags == nil {
			r.Tags = []string{}
		}
		d := Disk{Name: prefix + name, Title: r.Title, Note: r.Note, Tags: r.Tags, Size: st.Size(),
			URL: "disks/" + pyQuote(prefix+name, "/"), Systems: r.Systems}
		if r.Cover != "" && r.Cover == filepath.Base(r.Cover) {
			if cst, err := os.Stat(filepath.Join(dir, r.Cover)); err == nil && cst.Mode().IsRegular() {
				d.cover, d.Cover = prefix+r.Cover, "covers/"+pyQuote(prefix+r.Cover, "/")
			}
		}
		out = append(out, d)
	}
	return out
}

var keyUnsafe = regexp.MustCompile(`[^\w.-]`)

// overlayKey is the tab's key for a disk's overlay (main.js): by its
// file's name (an upload's without uploads/).
func overlayKey(name, id string) string {
	return keyUnsafe.ReplaceAllString(path.Base(name)+"-"+id, "_")
}

type idEntry struct {
	Stamp [2]int64 `json:"stamp"` // size, time in ns
	ID    string   `json:"id"`
}

type Ids struct {
	path    string
	mu      sync.Mutex
	known   map[string]idEntry
	hashing map[string]*sync.Mutex
	warming sync.WaitGroup // warm's
}

func newIds(path string) *Ids {
	ids := &Ids{path: path, known: map[string]idEntry{}, hashing: map[string]*sync.Mutex{}}
	if b, err := os.ReadFile(path); err == nil {
		json.Unmarshal(b, &ids.known)
	}
	return ids
}

func stampOf(st os.FileInfo) [2]int64 { return [2]int64{st.Size(), st.ModTime().UnixNano()} }

// peek: the id if it is known for the file as it is now, without hashing.
func (ids *Ids) peek(path string) string {
	st, err := os.Stat(path)
	if err != nil {
		return ""
	}
	ids.mu.Lock()
	defer ids.mu.Unlock()
	if k, ok := ids.known[path]; ok && k.Stamp == stampOf(st) {
		return k.ID
	}
	return ""
}

func (ids *Ids) get(path string) (string, error) {
	st, err := os.Stat(path)
	if err != nil {
		return "", err
	}
	stamp := stampOf(st)
	ids.mu.Lock()
	if k, ok := ids.known[path]; ok && k.Stamp == stamp {
		ids.mu.Unlock()
		return k.ID, nil
	}
	busy := ids.hashing[path]
	if busy == nil {
		busy = &sync.Mutex{}
		ids.hashing[path] = busy
	}
	ids.mu.Unlock()
	busy.Lock()
	defer busy.Unlock()
	if id := ids.peek(path); id != "" {
		return id, nil
	}
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	id := hex.EncodeToString(h.Sum(nil))[:24]
	ids.mu.Lock()
	defer ids.mu.Unlock()
	ids.known[path] = idEntry{stamp, id}
	if b, err := json.Marshal(ids.known); err == nil {
		writeAtomic(ids.path, b, 0o644)
	}
	return id, nil
}

// set: path's id, worked out as it was written.
func (ids *Ids) set(path, id string) {
	st, err := os.Stat(path)
	if err != nil {
		return
	}
	ids.mu.Lock()
	defer ids.mu.Unlock()
	ids.known[path] = idEntry{stampOf(st), id}
	if b, err := json.Marshal(ids.known); err == nil {
		writeAtomic(ids.path, b, 0o644)
	}
}

func (ids *Ids) warm(paths []string) {
	ids.warming.Add(1)
	go func() {
		defer ids.warming.Done()
		for _, p := range paths {
			ids.get(p)
		}
	}()
}

// pyQuote is Python's urllib.parse.quote: %XX (upper case) for all but
// letters, digits, "_.-~" and safe.
func pyQuote(s, safe string) string {
	var b strings.Builder
	for i := 0; i < len(s); i++ {
		c := s[i]
		if 'a' <= c && c <= 'z' || 'A' <= c && c <= 'Z' || '0' <= c && c <= '9' ||
			strings.IndexByte("_.-~", c) >= 0 || strings.IndexByte(safe, c) >= 0 {
			b.WriteByte(c)
		} else {
			b.WriteString("%" + strings.ToUpper(hex.EncodeToString([]byte{c})))
		}
	}
	return b.String()
}
