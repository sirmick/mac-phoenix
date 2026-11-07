package main

// Uploads: disk images the tab sends (main.js uploadImage) go into the
// library as uploads/NAME (IMAGES/uploads/NAME, its record NAME.json
// tagged "uploaded"), offered whatever the include/exclude patterns, and
// so served, kept and overlaid like the library's own.
//
//	POST   /api/uploads/NAME[?replace=1]  the image as the body (at most
//	                                      disks.max_image_mb) -> its library entry
//	DELETE /api/uploads/NAME              it, its record and its overlay

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"sync"
)

const uploadsDir = "uploads"

// uploadName: a file name (no path, not hidden) with an image's
// extension (main.js IMAGE).
var uploadName = regexp.MustCompile(`(?i)^[^/\\\x00-\x1f.][^/\\\x00-\x1f]{0,250}\.(img|image|dsk|hfs|iso|toast|cdr)$`)

var uploadsMu sync.Mutex // a name's image and record change together

func (s *Server) upload(c *ctx, name string) error {
	if !uploadName.MatchString(name) {
		return httpErr(400, "not a disk image's name (.img .image .dsk .hfs .iso .toast .cdr)")
	}
	st := s.settings()
	if st.Images == "" {
		return httpErr(404, "no library")
	}
	if _, err := os.Stat(filepath.Join(st.Images, name)); err == nil {
		return httpErr(409, "the library has an image of that name")
	}
	dir := filepath.Join(st.Images, uploadsDir)
	path := filepath.Join(dir, name)
	replace := c.query["replace"] == "1"
	if _, err := os.Stat(path); err == nil && !replace {
		return httpErr(409, "uploaded already (replace=1 replaces it)")
	}
	limit := int64(st.MaxImageMB) << 20
	if c.r.ContentLength > limit {
		c.w.Header().Set("Connection", "close")
		return httpErr(413, "too large", "limit_mb", st.MaxImageMB)
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	// Written beside it and renamed: a cut-short upload leaves nothing.
	tmp, err := os.CreateTemp(dir, "."+name+".*.tmp")
	if err != nil {
		return err
	}
	defer os.Remove(tmp.Name())
	h := sha256.New()
	n, err := io.Copy(io.MultiWriter(tmp, h), io.LimitReader(c.r.Body, limit+1))
	c.read = err == nil
	if err == nil && n > limit {
		err = httpErr(413, "too large", "limit_mb", st.MaxImageMB)
		c.w.Header().Set("Connection", "close")
	} else if err == nil && n == 0 {
		err = httpErr(400, "no image: the body is empty")
	} else if err == nil {
		err = tmp.Sync()
	}
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		var he *HTTPError
		if !errors.As(err, &he) {
			err = httpErr(400, "the body: "+err.Error())
		}
		return err
	}
	record, _ := json.Marshal(map[string]any{"title": name, "tags": []string{"uploaded"}, "note": ""})

	uploadsMu.Lock()
	defer uploadsMu.Unlock()
	if _, err := os.Stat(path); err == nil && !replace {
		return httpErr(409, "uploaded already (replace=1 replaces it)")
	}
	if old, err := s.ids.get(path); err == nil {
		if err := s.overlays.drop(overlayKey(name, old)); err != nil {
			return err
		}
	}
	if err := os.Chmod(tmp.Name(), 0o644); err != nil {
		return err
	}
	if err := os.Rename(tmp.Name(), path); err != nil {
		return err
	}
	s.ids.set(path, hex.EncodeToString(h.Sum(nil))[:24])
	if err := writeAtomic(path+".json", record, 0o644); err != nil {
		os.Remove(path)
		return err
	}
	for _, d := range catalog(st.Images, st.Include, st.Exclude) {
		if d.Name == uploadsDir+"/"+name {
			return c.sendJSON(201, d)
		}
	}
	return httpErr(500, "uploaded but not in the library")
}

func (s *Server) removeUpload(name string) error {
	if !uploadName.MatchString(name) {
		return httpErr(400, "not a disk image's name")
	}
	st := s.settings()
	path := filepath.Join(st.Images, uploadsDir, name)
	uploadsMu.Lock()
	defer uploadsMu.Unlock()
	id, err := s.ids.get(path)
	if err != nil {
		return httpErr(404, "no such upload")
	}
	if err := s.overlays.drop(overlayKey(name, id)); err != nil {
		return err
	}
	os.Remove(path + ".json")
	return os.Remove(path)
}

func init() {
	handle("POST", "/api/uploads/([^/]+)", true, func(c *ctx, m []string) error {
		return c.s.upload(c, m[1])
	})
	handle("DELETE", "/api/uploads/([^/]+)", true, func(c *ctx, m []string) error {
		if err := c.s.removeUpload(m[1]); err != nil {
			return err
		}
		c.w.WriteHeader(204)
		return nil
	})
}
