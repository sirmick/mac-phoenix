package main

// Files over HTTP: validators,
// pre-compressed copies, byte ranges.
//
// For a request for NAME, when NAME.gz exists and the browser accepts
// gzip, NAME.gz goes out with Content-Encoding: gzip and NAME's type,
// X-Decoded-Length says NAME's size, and Vary: Accept-Encoding. Every
// file has an ETag (its size and time, the content's for the embedded
// site, or the caller's: a disk's id; "-gz" on the compressed copy's) and
// Last-Modified; a matching If-None-Match gets 304. A Range request (one
// range; If-Range, when sent, must match) gets those bytes of NAME
// itself, never of NAME.gz.
//
// The embedded site keeps only NAME.gz of its large files (the Makefile):
// NAME is then NAME.gz decompressed as it goes out, for a browser that
// doesn't take gzip or asks for a range.

import (
	"bufio"
	"compress/gzip"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"net/http"
	"path"
	"regexp"
	"strconv"
	"strings"
	"sync"
)

var mimeTypes = map[string]string{
	".html": "text/html", ".htm": "text/html", ".js": "text/javascript", ".mjs": "text/javascript",
	".css": "text/css", ".json": "application/json", ".wasm": "application/wasm",
	".png": "image/png", ".jpg": "image/jpeg", ".gif": "image/gif", ".svg": "image/svg+xml",
	".ico": "image/vnd.microsoft.icon", ".txt": "text/plain", ".log": "text/plain",
	".md": "text/markdown",
}

func guessType(name string) string {
	if t, ok := mimeTypes[strings.ToLower(path.Ext(name))]; ok {
		return t
	}
	return "application/octet-stream"
}

var rangeRe = regexp.MustCompile(`^\s*bytes\s*=\s*(\d*)\s*-\s*(\d*)\s*$`)

// parseRange: [start, end) of one "bytes=" range; ok false to send it
// all; unsatisfiable for a range outside the file.
func parseRange(header string, size int64) (start, end int64, ok, unsatisfiable bool) {
	m := rangeRe.FindStringSubmatch(header)
	if m == nil || (m[1] == "" && m[2] == "") {
		return 0, 0, false, false // several ranges, or nonsense: all of it
	}
	if m[1] == "" { // the last N bytes
		n, err := strconv.ParseInt(m[2], 10, 64)
		if err != nil {
			return 0, 0, false, false
		}
		if n == 0 {
			return 0, 0, false, true
		}
		return max(0, size-n), size, true, false
	}
	start, err := strconv.ParseInt(m[1], 10, 64)
	if err != nil {
		return 0, 0, false, false
	}
	end = size
	if m[2] != "" {
		last, err := strconv.ParseInt(m[2], 10, 64)
		if err != nil {
			return 0, 0, false, false
		}
		if last < start {
			return 0, 0, false, false // not a range: ignored
		}
		end = min(size, last+1)
	}
	if start >= size {
		return 0, 0, false, true
	}
	return start, end, true, false
}

// A file to send: what Open gives, whether from the embedded site, a
// site directory or the library.
type source struct {
	fsys fs.FS
	name string
}

func (s source) open(name string) (fs.File, fs.FileInfo, error) {
	f, err := s.fsys.Open(name)
	if err != nil {
		return nil, nil, err
	}
	st, err := f.Stat()
	if err != nil || st.IsDir() {
		f.Close()
		if err == nil {
			err = fs.ErrNotExist
		}
		return nil, nil, err
	}
	return f, st, nil
}

// The embedded files have no times: their ETags are their contents'.
var contentTags sync.Map // "name size" -> tag

func etagOf(src source, name string, st fs.FileInfo) string {
	if !st.ModTime().IsZero() {
		return fmt.Sprintf("%x-%x", st.Size(), st.ModTime().UnixNano())
	}
	k := name + " " + strconv.FormatInt(st.Size(), 10)
	if t, ok := contentTags.Load(k); ok {
		return t.(string)
	}
	f, err := src.fsys.Open(name)
	if err != nil {
		return fmt.Sprintf("%x", st.Size())
	}
	defer f.Close()
	h := sha256.New()
	io.Copy(h, f)
	t := hex.EncodeToString(h.Sum(nil))[:24]
	contentTags.Store(k, t)
	return t
}

// sendFile answers with src's file; tag: its ETag's value (default: its
// size and time); extra: more headers.
func sendFile(c *ctx, src source, tag string, extra map[string]string) error {
	f, st, err := src.open(src.name)
	gz, gzst, gzerr := src.open(src.name + ".gz")
	hasGz := gzerr == nil
	if hasGz {
		defer gz.Close()
	}
	gzOnly := err != nil && hasGz
	if err != nil && !gzOnly {
		return httpErr(404, "not found")
	}
	var size int64
	if gzOnly {
		st = gzst
		if size, err = gzipSize(gz, gzst.Size()); err != nil {
			return err
		}
	} else {
		defer f.Close()
		size = st.Size()
	}
	r := c.r
	var start, end int64
	var ranged, unsat bool
	if h := r.Header.Get("Range"); h != "" {
		start, end, ranged, unsat = parseRange(h, size)
	}
	// ?bytes=A-B where the route offers it (X-Range-Encodings: gzip): a
	// range as a plain 200, gzip-compressed (overlay.js RemoteImage; disk
	// images are mostly zeros and code, a third to a half of their size).
	// Not Range: Chromium sends Accept-Encoding: identity with one and
	// fails a compressed 206.
	zipRange := false
	if q := r.URL.Query().Get("bytes"); q != "" && r.Header.Get("Range") == "" && extra["X-Range-Encodings"] == "gzip" {
		start, end, ranged, unsat = parseRange("bytes="+q, size)
		zipRange = ranged && strings.Contains(r.Header.Get("Accept-Encoding"), "gzip")
	}
	gzipped := !ranged && !unsat && hasGz && strings.Contains(r.Header.Get("Accept-Encoding"), "gzip")
	if tag == "" && gzOnly {
		tag = etagOf(src, src.name+".gz", st)
	} else if tag == "" {
		tag = etagOf(src, src.name, st)
	}
	etag := `"` + tag + `"`
	if gzipped {
		etag = `"` + tag + `-gz"`
	}
	h := c.w.Header()
	h.Set("ETag", etag)
	if !st.ModTime().IsZero() {
		h.Set("Last-Modified", st.ModTime().UTC().Format(http.TimeFormat))
	}
	h.Set("Accept-Ranges", "bytes")
	for k, v := range extra {
		h.Set(k, v)
	}
	if hasGz {
		h.Set("Vary", "Accept-Encoding")
	}
	if (ranged || unsat) && r.Header.Get("If-Range") != "" && strings.TrimSpace(r.Header.Get("If-Range")) != `"`+tag+`"` {
		ranged, unsat = false, false // another version: all of it
	}
	for _, t := range strings.Split(r.Header.Get("If-None-Match"), ",") {
		if strings.TrimSpace(t) == etag {
			h.Set("Content-Length", "0")
			c.w.WriteHeader(304)
			return nil
		}
	}
	if unsat {
		h.Set("Content-Range", fmt.Sprintf("bytes */%d", size))
		h.Set("Content-Length", "0")
		c.w.WriteHeader(416)
		return nil
	}
	var body io.Reader = f
	length := size
	if gzipped {
		body, length = gz, gzst.Size()
	} else if gzOnly {
		if body, err = gunzip(gz); err != nil {
			return err
		}
	}
	if !ranged {
		start, end = 0, length
	}
	h.Set("Content-Type", guessType(src.name))
	if gzipped {
		h.Set("Content-Encoding", "gzip")
	}
	if ranged && zipRange {
		h.Set("X-Content-Range", fmt.Sprintf("bytes %d-%d/%d", start, end-1, size)) // (a 200)
	} else if ranged {
		h.Set("Content-Range", fmt.Sprintf("bytes %d-%d/%d", start, end-1, size))
	}
	if zipRange {
		h.Set("Content-Encoding", "gzip")
		h.Set("Cache-Control", "no-store")
	} else {
		h.Set("Content-Length", strconv.FormatInt(end-start, 10))
	}
	h.Set("X-Decoded-Length", strconv.FormatInt(size, 10))
	if ranged && !zipRange {
		c.w.WriteHeader(206)
	} else {
		c.w.WriteHeader(200)
	}
	if r.Method == "HEAD" {
		return nil
	}
	if start > 0 {
		if sk, ok := body.(io.Seeker); ok {
			if _, err := sk.Seek(start, io.SeekStart); err != nil {
				return nil
			}
		} else if _, err := io.CopyN(io.Discard, body, start); err != nil {
			return nil
		}
	}
	if zipRange {
		zw, _ := gzip.NewWriterLevel(c.w, gzip.BestSpeed)
		io.CopyN(zw, body, end-start)
		zw.Close()
		return nil
	}
	io.CopyN(c.w, body, end-start)
	return nil
}

// gzipSize: a gzip file's decompressed size, from its trailer (ISIZE,
// the size mod 4 GB; the site's files are smaller).
func gzipSize(gz fs.File, n int64) (int64, error) {
	ra, ok := gz.(io.ReaderAt)
	var b [4]byte
	if !ok || n < 18 {
		return 0, errors.New("a .gz without its size")
	}
	if _, err := ra.ReadAt(b[:], n-4); err != nil {
		return 0, err
	}
	return int64(binary.LittleEndian.Uint32(b[:])), nil
}

func gunzip(gz fs.File) (io.Reader, error) {
	if sk, ok := gz.(io.Seeker); ok {
		sk.Seek(0, io.SeekStart)
	}
	return gzip.NewReader(bufio.NewReader(gz))
}
