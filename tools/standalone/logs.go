package main

// The tab's logs, as tools/web/server/app.py: the page and its Worker
// send what they print (log.js), a batch a second, appended to
// DATA/logs/DAY/SESSION.log (a paired browser's; a file stops at 64 MB;
// kept 14 days). /api/shot: the Screenshot button's PNG
// beside it, DAY/SESSION-HHMMSS.png, and a line in the log saying so.

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"time"
	"unicode/utf8"
)

var logSessionRe = regexp.MustCompile(`^[A-Za-z0-9-]{8,40}$`)

const (
	logFileMax  = 64 << 20
	logKeepDays = 14
	shotMax     = 16 << 20
)

func (s *Server) logDir() string { return filepath.Join(s.data, "logs") }

func (s *Server) logPrune() {
	cutoff := time.Now().UTC().AddDate(0, 0, -logKeepDays).Format("2006-01-02")
	ents, _ := os.ReadDir(s.logDir())
	for _, e := range ents {
		if e.Name() < cutoff {
			os.RemoveAll(filepath.Join(s.logDir(), e.Name()))
		}
	}
}

func init() {
	handle("POST", "/api/log", true, func(c *ctx, m []string) error {
		session := c.query["session"]
		if !logSessionRe.MatchString(session) {
			return httpErr(400, "session: 8-40 letters, digits, -")
		}
		body, err := c.bodyBytes()
		if err != nil {
			return err
		}
		text := strings.ToValidUTF8(string(body), string(utf8.RuneError))
		s := c.s
		now := time.Now().UTC()
		day := now.Format("2006-01-02")
		if err := os.MkdirAll(filepath.Join(s.logDir(), day), 0o755); err != nil {
			return err
		}
		path := filepath.Join(s.logDir(), day, session+".log")
		s.logMu.Lock()
		defer s.logMu.Unlock()
		st, err := os.Stat(path)
		isNew := err != nil
		if isNew {
			s.logPrune()
		} else if st.Size() > logFileMax {
			return c.sendJSON(200, map[string]any{"full": true})
		}
		f, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0o644)
		if err != nil {
			return err
		}
		defer f.Close()
		var b bytes.Buffer
		if isNew {
			who := fmt.Sprintf("%q", c.s.pairing.name(c.browser))
			fmt.Fprintf(&b, "# %sZ %s %s %s\n", now.Format("2006-01-02 15:04:05"), c.address(), who, c.r.UserAgent())
		}
		b.WriteString(text)
		if !strings.HasSuffix(text, "\n") {
			b.WriteByte('\n')
		}
		if _, err := f.Write(b.Bytes()); err != nil {
			return err
		}
		return c.sendJSON(200, map[string]any{})
	})

	shotTagRe := regexp.MustCompile(`^[a-z0-9]{0,8}$`)
	handle("POST", "/api/shot", true, func(c *ctx, m []string) error {
		session := c.query["session"]
		if !logSessionRe.MatchString(session) {
			return httpErr(400, "session: 8-40 letters, digits, -")
		}
		data, err := c.bodyLimit(shotMax)
		if err != nil {
			return err
		}
		if !bytes.HasPrefix(data, []byte("\x89PNG\r\n\x1a\n")) {
			return httpErr(400, "a PNG")
		}
		note := strings.ReplaceAll(c.query["note"], "\n", " ")
		if r := []rune(note); len(r) > 500 {
			note = string(r[:500])
		}
		tag := c.query["tag"] // "1x": Executor's 1x frame beside the scaled one
		if !shotTagRe.MatchString(tag) {
			return httpErr(400, "tag: up to 8 letters and digits")
		}
		if tag != "" {
			tag = "-" + tag
		}
		s := c.s
		now := time.Now().UTC()
		day := now.Format("2006-01-02")
		dir := filepath.Join(s.logDir(), day)
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return err
		}
		s.logMu.Lock()
		defer s.logMu.Unlock()
		stamp := now.Format("150405")
		name := fmt.Sprintf("%s-%s%s.png", session, stamp, tag)
		for i := 2; ; i++ {
			if _, err := os.Stat(filepath.Join(dir, name)); err != nil {
				break
			}
			name = fmt.Sprintf("%s-%s-%d%s.png", session, stamp, i, tag)
		}
		if err := os.WriteFile(filepath.Join(dir, name), data, 0o644); err != nil {
			return err
		}
		f, err := os.OpenFile(filepath.Join(dir, session+".log"), os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0o644)
		if err != nil {
			return err
		}
		line := "# screenshot " + day + "/" + name
		if note != "" {
			line += ": " + note
		}
		f.WriteString(line + "\n")
		f.Close()
		return c.sendJSON(200, map[string]any{"name": day + "/" + name})
	})

	handle("GET", "/api/logs", true, func(c *ctx, m []string) error {
		root := c.s.logDir()
		out := []map[string]any{}
		days, _ := os.ReadDir(root)
		sort.Slice(days, func(i, j int) bool { return days[i].Name() > days[j].Name() })
		for _, d := range days {
			files, _ := os.ReadDir(filepath.Join(root, d.Name()))
			for _, f := range files { // ReadDir sorts by name
				if info, err := f.Info(); err == nil {
					out = append(out, map[string]any{"log": d.Name() + "/" + f.Name(), "size": info.Size(),
						"mtime": info.ModTime().Unix()})
				}
			}
		}
		return c.sendJSON(200, out)
	})

	handle("GET", `/api/logs/(\d{4}-\d\d-\d\d)/([A-Za-z0-9-]{8,60}\.(?:log|png))`, true, func(c *ctx, m []string) error {
		dir := filepath.Join(c.s.logDir(), m[1])
		if st, err := os.Stat(filepath.Join(dir, m[2])); err != nil || !st.Mode().IsRegular() {
			return httpErr(404, "no such log")
		}
		c.w.Header().Set("Cache-Control", "no-store")
		return sendFile(c, source{os.DirFS(dir), m[2]}, "", nil)
	})
}
