package main

// Volumes (web/executor/worker.js: Executor's own volume, per System
// choice): a manifest, { dirs, files: {
// path: { size, mtime, sha } } }, versioned and based like the overlays;
// the files' contents are objects by SHA-256, shared by every volume,
// uploaded before the manifest that names them (409 { missing }).
//
// DATA/volumes/KEY.json is { version, manifest }; DATA/objects/AB/SHA the
// objects. Objects no manifest names are deleted an hour after they
// were written (an upload in progress names them only when it ends).

import (
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"sync"
	"time"
)

var shaRe = regexp.MustCompile(`^[0-9a-f]{64}$`)

const orphanAge = time.Hour

type VolumeStore struct {
	mu      sync.Mutex
	dir     string
	objects string
}

type volumeFile struct {
	Version  int64           `json:"version"`
	Manifest json.RawMessage `json:"manifest"`
}

func newVolumeStore(data string) *VolumeStore {
	v := &VolumeStore{dir: filepath.Join(data, "volumes"), objects: filepath.Join(data, "objects")}
	os.MkdirAll(v.dir, 0o755)
	os.MkdirAll(v.objects, 0o755)
	return v
}

func (v *VolumeStore) read(key string) (volumeFile, error) {
	var f volumeFile
	b, err := os.ReadFile(filepath.Join(v.dir, key+".json"))
	if errors.Is(err, os.ErrNotExist) {
		return volumeFile{Manifest: json.RawMessage("null")}, nil
	}
	if err != nil {
		return f, err
	}
	if err := json.Unmarshal(b, &f); err != nil {
		return f, err
	}
	if len(f.Manifest) == 0 {
		f.Manifest = json.RawMessage("null")
	}
	return f, nil
}

func (v *VolumeStore) write(key string, f volumeFile) error {
	b, _ := json.Marshal(f)
	return writeAtomic(filepath.Join(v.dir, key+".json"), b, 0o644)
}

func (v *VolumeStore) get(key string) (int64, json.RawMessage, error) {
	if err := checkKey(key); err != nil {
		return 0, nil, err
	}
	v.mu.Lock()
	defer v.mu.Unlock()
	f, err := v.read(key)
	return f.Version, f.Manifest, err
}

func (v *VolumeStore) put(key string, manifest any, base int64) (int64, error) {
	if err := checkKey(key); err != nil {
		return 0, err
	}
	m, ok := manifest.(map[string]any)
	if !ok {
		return 0, httpErr(400, "a manifest is { dirs, files }")
	}
	_, dirsOK := m["dirs"].([]any)
	files, filesOK := m["files"].(map[string]any)
	if !dirsOK || !filesOK {
		return 0, httpErr(400, "a manifest is { dirs, files }")
	}
	shas := []string{}
	for _, x := range files {
		f, ok := x.(map[string]any)
		sha, _ := f["sha"].(string)
		if !ok || !shaRe.MatchString(sha) {
			return 0, httpErr(400, "every file needs its sha")
		}
		shas = append(shas, sha)
	}
	// Under the lock: the collector can't delete what this names.
	v.mu.Lock()
	defer v.mu.Unlock()
	cur, err := v.read(key)
	if err != nil {
		return 0, err
	}
	if base != cur.Version {
		return 0, httpErr(409, "changed since", "version", cur.Version)
	}
	if missing := v.missingLocked(shas); len(missing) > 0 {
		return 0, httpErr(409, "objects missing", "missing", missing, "version", cur.Version)
	}
	b, _ := json.Marshal(manifest)
	if err := v.write(key, volumeFile{cur.Version + 1, b}); err != nil {
		return 0, err
	}
	v.collect()
	return cur.Version + 1, nil
}

func (v *VolumeStore) remove(key string) (int64, error) {
	if err := checkKey(key); err != nil {
		return 0, err
	}
	v.mu.Lock()
	defer v.mu.Unlock()
	cur, err := v.read(key)
	if err != nil {
		return 0, err
	}
	if err := v.write(key, volumeFile{cur.Version + 1, json.RawMessage("null")}); err != nil {
		return 0, err
	}
	v.collect()
	return cur.Version + 1, nil
}

func (v *VolumeStore) objectDir(sha string) (string, string) {
	return filepath.Join(v.objects, sha[:2]), sha
}

func (v *VolumeStore) missing(shas []string) []string {
	v.mu.Lock()
	defer v.mu.Unlock()
	return v.missingLocked(shas)
}

func (v *VolumeStore) missingLocked(shas []string) []string {
	seen := map[string]bool{}
	out := []string{}
	for _, s := range shas {
		if seen[s] {
			continue
		}
		seen[s] = true
		dir, name := v.objectDir(s)
		if st, err := os.Stat(filepath.Join(dir, name)); err != nil || !st.Mode().IsRegular() {
			out = append(out, s)
		}
	}
	sort.Strings(out)
	return out
}

func (v *VolumeStore) putObject(sha string, data []byte) error {
	sum := sha256.Sum256(data)
	if hex.EncodeToString(sum[:]) != sha {
		return httpErr(400, "the data is not that sha")
	}
	dir, name := v.objectDir(sha)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	tmp, err := os.CreateTemp(dir, name+".*.tmp")
	if err != nil {
		return err
	}
	_, err = tmp.Write(data)
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		os.Remove(tmp.Name())
		return err
	}
	v.mu.Lock() // new (or again): young, not collected
	defer v.mu.Unlock()
	path := filepath.Join(dir, name)
	if err := os.Rename(tmp.Name(), path); err != nil {
		os.Remove(tmp.Name())
		if _, serr := os.Stat(path); serr == nil {
			return nil // the same content is there already
		}
		return err
	}
	return nil
}

// collect deletes the objects no manifest names, written an hour ago or
// more. Under v.mu.
func (v *VolumeStore) collect() {
	named := map[string]bool{}
	ents, _ := os.ReadDir(v.dir)
	for _, e := range ents {
		key, ok := strings.CutSuffix(e.Name(), ".json")
		if !ok {
			continue
		}
		f, err := v.read(key)
		if err != nil {
			return // a manifest we can't read: delete nothing
		}
		var m struct {
			Files map[string]struct {
				Sha string `json:"sha"`
			} `json:"files"`
		}
		if json.Unmarshal(f.Manifest, &m) == nil {
			for _, x := range m.Files {
				named[x.Sha] = true
			}
		}
	}
	subs, _ := os.ReadDir(v.objects)
	for _, sub := range subs {
		files, _ := os.ReadDir(filepath.Join(v.objects, sub.Name()))
		for _, f := range files {
			if !shaRe.MatchString(f.Name()) || named[f.Name()] {
				continue
			}
			if info, err := f.Info(); err == nil && time.Since(info.ModTime()) > orphanAge {
				os.Remove(filepath.Join(v.objects, sub.Name(), f.Name()))
			}
		}
	}
}

// ---- bundles: a volume's objects in one response, objects up in one
// request (web/executor/sync.js) ----
//
// A bundle is records: a sha (32 bytes), a length (uint32 LE), that many
// bytes; ended by a record with a zero sha and length (a bundle without
// it was cut short). The one down starts with the volume itself: a
// length (uint32 LE) and the JSON { version, manifest }.

var bundleEnd [36]byte

func bundleRecord(w io.Writer, sha []byte, data []byte) error {
	var head [36]byte
	copy(head[:32], sha)
	binary.LittleEndian.PutUint32(head[32:], uint32(len(data)))
	if _, err := w.Write(head[:]); err != nil {
		return err
	}
	_, err := w.Write(data)
	return err
}

// writeBundle writes KEY's volume and every object its manifest names
// (each once) to w. An object gone meanwhile (a newer manifest's
// collection) ends the bundle short: the tab asks again.
func (v *VolumeStore) writeBundle(w io.Writer, key string) error {
	version, manifest, err := v.get(key)
	if err != nil {
		return err
	}
	head, _ := json.Marshal(map[string]any{"version": version, "manifest": manifest})
	var n [4]byte
	binary.LittleEndian.PutUint32(n[:], uint32(len(head)))
	if _, err := w.Write(n[:]); err != nil {
		return err
	}
	if _, err := w.Write(head); err != nil {
		return err
	}
	var m struct {
		Files map[string]struct {
			Sha string `json:"sha"`
		} `json:"files"`
	}
	json.Unmarshal(manifest, &m)
	paths := make([]string, 0, len(m.Files))
	for p := range m.Files {
		paths = append(paths, p)
	}
	sort.Strings(paths) // (a folder's files together, the System Folder's first)
	seen := map[string]bool{}
	for _, p := range paths {
		sha := m.Files[p].Sha
		if seen[sha] || !shaRe.MatchString(sha) {
			continue
		}
		seen[sha] = true
		dir, name := v.objectDir(sha)
		data, err := os.ReadFile(filepath.Join(dir, name))
		if err != nil {
			return err
		}
		raw, _ := hex.DecodeString(sha)
		if err := bundleRecord(w, raw, data); err != nil {
			return err
		}
	}
	_, err = w.Write(bundleEnd[:])
	return err
}

// putBundle stores a bundle's objects (each checked against its sha); how
// many. limit: the bytes it may hold in all.
func (v *VolumeStore) putBundle(r io.Reader, limit int64) (int, error) {
	n := 0
	var head [36]byte
	for {
		if _, err := io.ReadFull(r, head[:]); err != nil {
			if err == io.EOF && n == 0 {
				return 0, nil
			}
			return n, httpErr(400, "a bundle cut short")
		}
		size := int64(binary.LittleEndian.Uint32(head[32:]))
		if head == bundleEnd {
			return n, nil
		}
		if limit -= size; limit < 0 {
			return n, httpErr(413, "too large")
		}
		data := make([]byte, size)
		if _, err := io.ReadFull(r, data); err != nil {
			return n, httpErr(400, "a bundle cut short")
		}
		if err := v.putObject(hex.EncodeToString(head[:32]), data); err != nil {
			return n, err
		}
		n++
	}
}

type volumeSummary struct {
	Key     string `json:"key"`
	Version int64  `json:"version"`
	Files   int    `json:"files"`
}

func (v *VolumeStore) summary() []volumeSummary {
	v.mu.Lock()
	defer v.mu.Unlock()
	out := []volumeSummary{}
	ents, _ := os.ReadDir(v.dir)
	for _, e := range ents {
		key, ok := strings.CutSuffix(e.Name(), ".json")
		if !ok {
			continue
		}
		f, err := v.read(key)
		if err != nil {
			continue
		}
		var m struct {
			Files map[string]any `json:"files"`
		}
		json.Unmarshal(f.Manifest, &m)
		out = append(out, volumeSummary{key, f.Version, len(m.Files)})
	}
	return out
}
