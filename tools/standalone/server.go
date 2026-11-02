package main

// The server: tools/web/server/app.py's API without accounts.
//
//	/                 the tab (embedded, or --site DIR)
//	/login            the password, when there is one (else everything is open)
//	/library.json     the library (library.go); /disks/NAME its images, with Range;
//	                  /covers/NAME their covers
//	/api/server       { standalone, version, password, signed_in } (no sign-in needed)
//	/api/me           { name, owner, mode: "standalone" }, always (signed out too)
//	/api/session      POST ?id=ID[&take=1] the controlling tab (session.go);
//	                  /api/session/release, PUT/GET /api/session/screen
//	/api/login        POST { password }       /api/logout POST
//	/api/settings     GET the settings; POST some of them (settings.go)
//	/api/sync/...     overlays (overlay.go) and volumes (volumes.go), as app.py:
//	    GET    overlays/KEY?since=V   records (u32 LE block, 4 KB) newer than V;
//	                                  X-Sync-Version, X-Sync-Reset
//	    POST   overlays/KEY?stage=T   records kept for a commit with T
//	    POST   overlays/KEY?base=V[&stage=T][&replace=1][&name=&size=] -> { version }
//	    DELETE overlays/KEY           Revert -> { version }
//	    POST   overlays/KEY/merge     into the library image -> { blocks, id, key }
//	    GET/PUT/DELETE volumes/KEY    { version, manifest }
//	    POST   objects/missing; PUT/GET objects/SHA
//	    GET    (nothing after sync/)  { overlays, volumes }
//	/api/relay        GET { connections, sockets } open now
//	/relay            WebSocket: the tab's sockets (relay.go)
//	/api/log, /api/shot, /api/logs[/DAY/NAME]   the tab's logs (logs.go)
//
// With a password, everything but /login, /api/login, /api/server,
// /api/log and /api/shot needs the session cookie. Writes and /relay
// from another site are refused (Origin against Host, Sec-Fetch-Site).
// Every answer is cross-origin isolated (COOP/COEP).

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"net"
	"net/http"
	"net/url"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	cookieName     = "executor_session"
	signedOutBody  = 64 << 10
	loginFailures  = 10 // per address in loginWindow, then 429
	loginWindow    = 10 * time.Minute
	sessionLife    = 30 * 24 * time.Hour
	maxConnections = 64
	idle           = 60 * time.Second
)

var version = "dev" // -ldflags "-X main.version=..."

type HTTPError struct {
	status int
	msg    string
	extra  map[string]any
}

func (e *HTTPError) Error() string { return e.msg }

// httpErr: status, message, then key, value pairs for the JSON answer.
func httpErr(status int, msg string, kv ...any) error {
	e := &HTTPError{status: status, msg: msg, extra: map[string]any{}}
	for i := 0; i+1 < len(kv); i += 2 {
		e.extra[kv[i].(string)] = kv[i+1]
	}
	return e
}

type Server struct {
	data         string
	settingsPath string
	mu           sync.RWMutex
	doc          map[string]any  // what settings.json has (the schema's document)
	hash         string          // ... and the password's
	override     func(*Settings) // the command line's
	eff          Settings        // what applies
	running      Settings        // what the listener was made with
	pwOverride   string          // --password-file's hash
	site         fs.FS
	siteName     string
	quiet        bool

	ids      *Ids
	overlays *OverlayStore
	volumes  *VolumeStore
	relay    *Relay
	tabs     TabSessions
	logMu    sync.Mutex

	sessMu   sync.Mutex
	sessions map[string]time.Time
	failures map[string][]time.Time
}

func (s *Server) settings() Settings {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.eff
}

func (s *Server) applySettings(doc map[string]any, hash string) {
	eff := typed(doc, hash, s.data)
	if s.override != nil {
		s.override(&eff)
	}
	if s.pwOverride != "" {
		eff.PasswordHash = s.pwOverride
	}
	s.mu.Lock()
	old := s.eff
	s.doc, s.hash, s.eff = doc, hash, eff
	s.mu.Unlock()
	if old.Images != eff.Images || old.Images == "" {
		paths := []string{}
		for _, d := range catalog(eff.Images, eff.Include, eff.Exclude) {
			paths = append(paths, filepath.Join(eff.Images, d.Name))
		}
		s.ids.warm(paths)
	}
}

func (s *Server) passwordSet() bool { return s.settings().PasswordHash != "" }

// imageFor: the library image whose overlay key is key, if one is (by
// the ids known: the tab has fetched the disk to have the key).
func (s *Server) imageFor(key string) (string, string) {
	st := s.settings()
	for _, d := range catalog(st.Images, st.Include, st.Exclude) {
		p := filepath.Join(st.Images, d.Name)
		if id := s.ids.peek(p); id != "" && overlayKey(d.Name, id) == key {
			return st.Images, p
		}
	}
	return "", ""
}

// ---- a request ----
type ctx struct {
	s      *Server
	w      http.ResponseWriter
	r      *http.Request
	query  map[string]string
	authed bool // signed in, or no password
	signed bool // has a session (with a password)
	body   []byte
	read   bool
}

func (c *ctx) has(k string) bool { _, ok := c.r.URL.Query()[k]; return ok }

func (c *ctx) bodyBytes() ([]byte, error) {
	if c.read {
		return c.body, nil
	}
	limit := int64(signedOutBody)
	if c.authed {
		limit = int64(c.s.settings().MaxUploadMB) << 20
	}
	return c.bodyLimit(limit)
}

func (c *ctx) bodyLimit(limit int64) ([]byte, error) {
	if c.r.ContentLength > limit {
		c.w.Header().Set("Connection", "close")
		return nil, httpErr(413, "too large")
	}
	b, err := io.ReadAll(io.LimitReader(c.r.Body, limit+1))
	if err != nil {
		return nil, httpErr(400, "the body: "+err.Error())
	}
	if int64(len(b)) > limit {
		c.w.Header().Set("Connection", "close")
		return nil, httpErr(413, "too large")
	}
	c.body, c.read = b, true
	return b, nil
}

func (c *ctx) jsonBody() (map[string]any, error) {
	b, err := c.bodyBytes()
	if err != nil {
		return nil, err
	}
	if len(b) == 0 {
		b = []byte("{}")
	}
	var v any
	if json.Unmarshal(b, &v) != nil {
		return nil, httpErr(400, "not JSON")
	}
	m, ok := v.(map[string]any)
	if !ok {
		return nil, httpErr(400, "not a JSON object")
	}
	return m, nil
}

func (c *ctx) sendJSON(status int, v any) error {
	b, _ := json.Marshal(v)
	c.w.Header().Set("Cache-Control", "no-store")
	return c.sendBytes(status, b, "application/json")
}

func (c *ctx) sendBytes(status int, b []byte, ctype string) error {
	c.w.Header().Set("Content-Type", ctype)
	c.w.Header().Set("Content-Length", strconv.Itoa(len(b)))
	c.w.WriteHeader(status)
	if c.r.Method != "HEAD" {
		c.w.Write(b)
	}
	return nil
}

func (c *ctx) redirect(to string) error {
	c.w.Header().Set("Location", to)
	return c.sendBytes(302, nil, "text/plain")
}

// address: the client's, X-Forwarded-For's through trusted proxies.
func (c *ctx) address() string {
	addr, _, err := net.SplitHostPort(c.r.RemoteAddr)
	if err != nil {
		addr = c.r.RemoteAddr
	}
	trusted := map[string]bool{}
	for _, t := range c.s.settings().TrustedProxies {
		trusted[t] = true
	}
	if trusted[addr] {
		hops := strings.Split(c.r.Header.Get("X-Forwarded-For"), ",")
		for i := len(hops) - 1; i >= 0; i-- {
			hop := strings.TrimSpace(hops[i])
			if hop == "" {
				continue
			}
			addr = hop
			if !trusted[hop] {
				break
			}
		}
	}
	return addr
}

func hostName(host string) string {
	u, err := url.Parse("//" + host)
	if err != nil {
		return host
	}
	return u.Hostname()
}

func isLocal(host string) bool {
	switch host {
	case "localhost", "127.0.0.1", "::1":
		return true
	}
	return false
}

func (c *ctx) secure() bool {
	switch v := c.s.settings().SecureCookies.(type) {
	case bool:
		return v
	}
	return !isLocal(hostName(c.r.Host)) // "auto": but on this machine (plain http)
}

func (c *ctx) sameOrigin() bool {
	if site := c.r.Header.Get("Sec-Fetch-Site"); site != "" && site != "same-origin" && site != "none" {
		return false
	}
	origin := c.r.Header.Get("Origin")
	if origin == "" {
		return true
	}
	u, err := url.Parse(origin)
	return err == nil && u.Host == c.r.Host
}

// ---- sessions ----
func (s *Server) session(token string) bool {
	if token == "" {
		return false
	}
	s.sessMu.Lock()
	defer s.sessMu.Unlock()
	t, ok := s.sessions[token]
	if ok && time.Now().After(t) {
		delete(s.sessions, token)
		return false
	}
	return ok
}

func (s *Server) newSession() string {
	b := make([]byte, 24)
	rand.Read(b)
	t := hex.EncodeToString(b)
	s.sessMu.Lock()
	s.sessions[t] = time.Now().Add(sessionLife)
	s.saveSessions()
	s.sessMu.Unlock()
	return t
}

func (s *Server) endSession(token string) {
	s.sessMu.Lock()
	delete(s.sessions, token)
	s.saveSessions()
	s.sessMu.Unlock()
}

// The sessions last across restarts: DATA/sessions.json. Under sessMu.
func (s *Server) saveSessions() {
	now := time.Now()
	for k, t := range s.sessions {
		if now.After(t) {
			delete(s.sessions, k)
		}
	}
	if b, err := json.Marshal(s.sessions); err == nil {
		writeAtomic(filepath.Join(s.data, "sessions.json"), b, 0o600)
	}
}

func (s *Server) loadSessions() {
	s.sessions = map[string]time.Time{}
	if b, err := os.ReadFile(filepath.Join(s.data, "sessions.json")); err == nil {
		json.Unmarshal(b, &s.sessions)
	}
}

func (c *ctx) cookie() string {
	k, err := c.r.Cookie(cookieName)
	if err != nil {
		return ""
	}
	return k.Value
}

func (c *ctx) setCookie(token string) {
	attrs := []string{"Path=/", "HttpOnly", "SameSite=Strict"}
	if c.secure() {
		attrs = append(attrs, "Secure")
	}
	if token != "" {
		attrs = append(attrs, fmt.Sprintf("Max-Age=%d", int(sessionLife.Seconds())))
	} else {
		attrs = append(attrs, "Max-Age=0")
	}
	c.w.Header().Add("Set-Cookie", cookieName+"="+token+"; "+strings.Join(attrs, "; "))
}

func (s *Server) recentFailures(addr string, now time.Time) []time.Time {
	var out []time.Time
	for _, t := range s.failures[addr] {
		if now.Sub(t) < loginWindow {
			out = append(out, t)
		}
	}
	return out
}

// ---- routing ----
type route struct {
	method string
	re     *regexp.Regexp
	fn     func(c *ctx, m []string) error
	auth   bool
}

var routes []route

func handle(method, pattern string, auth bool, fn func(c *ctx, m []string) error) {
	routes = append(routes, route{method, regexp.MustCompile("^(?:" + pattern + ")$"), fn, auth})
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	h := w.Header()
	h.Set("Cache-Control", "no-cache")
	h.Set("X-Content-Type-Options", "nosniff")
	h.Set("Referrer-Policy", "same-origin")
	h.Set("Cross-Origin-Opener-Policy", "same-origin")
	h.Set("Cross-Origin-Embedder-Policy", "require-corp")
	h.Set("Cross-Origin-Resource-Policy", "same-origin")
	c := &ctx{s: s, w: w, r: r, query: map[string]string{}}
	for k, v := range r.URL.Query() {
		c.query[k] = v[len(v)-1]
	}
	c.signed = s.passwordSet() && s.session(c.cookie())
	c.authed = !s.passwordSet() || c.signed
	err := s.dispatch(c)
	var he *HTTPError
	if errors.As(err, &he) {
		v := map[string]any{"error": he.msg}
		for k, x := range he.extra {
			v[k] = x
		}
		c.sendJSON(he.status, v)
	} else if err != nil {
		if !s.quiet {
			fmt.Fprintf(os.Stderr, "%s %s: %v\n", r.Method, r.URL.Path, err)
		}
		c.sendJSON(500, map[string]any{"error": err.Error()})
	}
	// Keep-alive: the rest of a body no route read.
	if !c.read && r.ContentLength != 0 {
		h.Set("Connection", "close")
	}
}

func (s *Server) dispatch(c *ctx) error {
	method := c.r.Method
	if (method == "POST" || method == "PUT" || method == "DELETE") && !c.sameOrigin() {
		return httpErr(403, "not from this site")
	}
	p := c.r.URL.Path
	for _, rt := range routes {
		m := rt.re.FindStringSubmatch(p)
		if m == nil || !(rt.method == method || (rt.method == "GET" && method == "HEAD")) {
			continue
		}
		if rt.auth && !c.authed {
			return httpErr(401, "sign in first")
		}
		return rt.fn(c, m)
	}
	if method != "GET" && method != "HEAD" {
		return httpErr(405, "no such method here")
	}
	return s.static(c, p)
}

func (s *Server) static(c *ctx, p string) error {
	norm := path.Clean("/" + p)
	if strings.HasSuffix(p, "/") {
		norm = path.Join(norm, "index.html")
	}
	if !c.authed {
		if strings.HasSuffix(norm, ".html") {
			if p == "" {
				p = "/"
			}
			return c.redirect("/login?next=" + pyQuote(p, ""))
		}
		return httpErr(401, "sign in first")
	}
	name := strings.TrimPrefix(norm, "/")
	if name == "" {
		name = "."
	}
	st, err := fs.Stat(s.site, name)
	if err != nil {
		st, err = fs.Stat(s.site, name+".gz") // embedded as NAME.gz only (static.go)
	}
	if err != nil {
		if norm == "/index.html" {
			return c.sendBytes(404, []byte("No site in this build: tools/standalone's Makefile embeds make_site.sh's output (or --site DIR).\n"), "text/plain; charset=utf-8")
		}
		return httpErr(404, "not found")
	}
	if st.IsDir() {
		return c.redirect(pyQuote(strings.TrimRight(p, "/")+"/", "/"))
	}
	return sendFile(c, source{s.site, name}, "", nil)
}

func init() {
	handle("GET", "/login", false, func(c *ctx, m []string) error {
		return c.sendBytes(200, []byte(loginPage), "text/html; charset=utf-8")
	})
	handle("GET", "/api/server", false, func(c *ctx, m []string) error {
		return c.sendJSON(200, map[string]any{"standalone": true, "version": version,
			"password": c.s.passwordSet(), "signed_in": c.authed})
	})
	// The mode, which the tab reads before anything: signed out too.
	handle("GET", "/api/me", false, func(c *ctx, m []string) error {
		return c.sendJSON(200, map[string]any{"name": "local", "owner": true, "mode": "standalone"})
	})
	handle("POST", "/api/login", false, func(c *ctx, m []string) error {
		s := c.s
		addr := c.address()
		now := time.Now()
		s.sessMu.Lock()
		many := len(s.recentFailures(addr, now)) >= loginFailures
		s.sessMu.Unlock()
		if many {
			return httpErr(429, "too many attempts: try again later")
		}
		b, err := c.jsonBody()
		if err != nil {
			return err
		}
		pw, _ := b["password"].(string)
		hash := s.settings().PasswordHash
		if hash == "" {
			return c.sendJSON(200, map[string]any{"name": "owner"}) // nothing to sign in to
		}
		if !checkPassword(hash, pw) {
			s.sessMu.Lock()
			s.failures[addr] = append(s.recentFailures(addr, now), now)
			if len(s.failures) > 1000 {
				for k := range s.failures {
					if len(s.recentFailures(k, now)) == 0 {
						delete(s.failures, k)
					}
				}
			}
			s.sessMu.Unlock()
			return httpErr(401, "wrong password")
		}
		c.setCookie(s.newSession())
		return c.sendJSON(200, map[string]any{"name": "owner"})
	})
	handle("POST", "/api/logout", false, func(c *ctx, m []string) error {
		c.s.endSession(c.cookie())
		c.setCookie("")
		return c.sendJSON(200, map[string]any{})
	})

	// ---- settings ----
	handle("GET", "/api/settings", true, func(c *ctx, m []string) error {
		return c.sendJSON(200, c.s.settingsView(c))
	})
	handle("POST", "/api/settings", true, func(c *ctx, m []string) error {
		s := c.s
		b, err := c.bodyBytes()
		if err != nil {
			return err
		}
		var patch map[string]any
		if json.Unmarshal(b, &patch) != nil || patch == nil {
			return httpErr(400, "not a JSON object")
		}
		// network.password: a string sets it; it is not a setting.
		var pw *string
		if n, ok := patch["network"].(map[string]any); ok {
			if p, ok := n["password"].(string); ok {
				pw = &p
				delete(n, "password")
			}
		}
		if pw != nil && !s.passwordSet() && !isLoopback(c.r.RemoteAddr) {
			return httpErr(403, "the password from this machine only, or set one")
		}
		s.mu.RLock()
		cur, hash := s.doc, s.hash
		s.mu.RUnlock()
		next, server, err := patchSettings(cur, patch)
		if err != nil {
			fe, _ := err.(*fieldError)
			path := ""
			if fe != nil {
				path = fe.Path
			}
			return c.sendJSON(400, map[string]any{"error": err.Error(), "path": path})
		}
		if server && !s.passwordSet() && !isLoopback(c.r.RemoteAddr) {
			return httpErr(403, "the server's settings from this machine only, or set a password")
		}
		if pw != nil {
			hash = ""
			if *pw != "" {
				hash = hashPassword(*pw)
			}
		}
		if err := saveSettings(s.settingsPath, next, hash); err != nil {
			return err
		}
		s.applySettings(next, hash)
		if pw != nil && *pw != "" && !c.signed {
			c.setCookie(s.newSession()) // whoever set it stays signed in
		}
		return c.sendJSON(200, s.settingsView(c))
	})

	// ---- the library ----
	handle("GET", "/library.json", true, func(c *ctx, m []string) error {
		st := c.s.settings()
		return c.sendJSON(200, catalog(st.Images, st.Include, st.Exclude))
	})
	handle("GET", "/disks/(.+)", true, func(c *ctx, m []string) error {
		st := c.s.settings()
		for _, d := range catalog(st.Images, st.Include, st.Exclude) {
			if d.Name != m[1] {
				continue
			}
			id, err := c.s.ids.get(filepath.Join(st.Images, d.Name))
			if err != nil {
				return httpErr(404, "not in the library")
			}
			return sendFile(c, source{os.DirFS(st.Images), d.Name}, id, map[string]string{"X-Image-Id": id})
		}
		return httpErr(404, "not in the library")
	})
	handle("GET", "/covers/(.+)", true, func(c *ctx, m []string) error {
		st := c.s.settings()
		for _, d := range catalog(st.Images, st.Include, st.Exclude) {
			if d.cover == m[1] {
				return sendFile(c, source{os.DirFS(st.Images), d.cover}, "", nil)
			}
		}
		return httpErr(404, "not in the library")
	})

	// ---- sync: overlays ----
	handle("GET", "/api/sync/?", true, func(c *ctx, m []string) error {
		st := c.s.settings()
		return c.sendJSON(200, map[string]any{"overlays": c.s.overlays.summary(st.Images),
			"volumes": c.s.volumes.summary()})
	})
	handle("GET", "/api/sync/overlays/([^/]+)", true, func(c *ctx, m []string) error {
		since, err := intQuery(c, "since", 0)
		if err != nil {
			return err
		}
		return c.s.overlays.serve(c, m[1], since)
	})
	handle("POST", "/api/sync/overlays/([^/]+)", true, func(c *ctx, m []string) error {
		body, err := c.bodyBytes()
		if err != nil {
			return err
		}
		if !c.has("base") {
			if !c.has("stage") {
				return httpErr(400, "base (the version this is on) or stage")
			}
			if err := c.s.overlays.stage(m[1], c.query["stage"], body); err != nil {
				return err
			}
			c.w.WriteHeader(204)
			return nil
		}
		base, err := intQuery(c, "base", 0)
		if err != nil {
			return err
		}
		var name *string
		var size *int64
		if c.has("name") {
			n := c.query["name"]
			name = &n
		}
		if c.query["size"] != "" {
			v, err := strconv.ParseInt(c.query["size"], 10, 64)
			if err != nil {
				return httpErr(400, "size: a number")
			}
			size = &v
		}
		v, err := c.s.overlays.put(m[1], base, body, name, size, c.query["stage"], c.query["replace"] == "1")
		if err != nil {
			return err
		}
		return c.sendJSON(200, map[string]any{"version": v})
	})
	handle("DELETE", "/api/sync/overlays/([^/]+)", true, func(c *ctx, m []string) error {
		v, err := c.s.overlays.remove(m[1])
		if err != nil {
			return err
		}
		return c.sendJSON(200, map[string]any{"version": v})
	})
	handle("POST", "/api/sync/overlays/([^/]+)/merge", true, func(c *ctx, m []string) error {
		n, image, err := c.s.overlays.merge(m[1])
		if err != nil {
			return err
		}
		id, err := c.s.ids.get(image)
		if err != nil {
			return err
		}
		return c.sendJSON(200, map[string]any{"blocks": n, "id": id, "key": overlayKey(filepath.Base(image), id)})
	})

	// ---- sync: volumes and objects ----
	handle("GET", "/api/sync/volumes/([^/]+)", true, func(c *ctx, m []string) error {
		v, manifest, err := c.s.volumes.get(m[1])
		if err != nil {
			return err
		}
		return c.sendJSON(200, map[string]any{"version": v, "manifest": manifest})
	})
	handle("PUT", "/api/sync/volumes/([^/]+)", true, func(c *ctx, m []string) error {
		b, err := c.jsonBody()
		if err != nil {
			return err
		}
		base, ok := b["base"].(float64)
		if !ok || base != float64(int64(base)) {
			return httpErr(400, "base: the version this is on")
		}
		v, err := c.s.volumes.put(m[1], b["manifest"], int64(base))
		if err != nil {
			return err
		}
		return c.sendJSON(200, map[string]any{"version": v})
	})
	handle("DELETE", "/api/sync/volumes/([^/]+)", true, func(c *ctx, m []string) error {
		v, err := c.s.volumes.remove(m[1])
		if err != nil {
			return err
		}
		return c.sendJSON(200, map[string]any{"version": v})
	})
	handle("POST", "/api/sync/objects/missing", true, func(c *ctx, m []string) error {
		b, err := c.jsonBody()
		if err != nil {
			return err
		}
		list, ok := b["shas"].([]any)
		if b["shas"] != nil && !ok {
			return httpErr(400, "shas is a list")
		}
		shas := []string{}
		for _, x := range list {
			sha := fmt.Sprint(x)
			if !shaRe.MatchString(sha) {
				return httpErr(400, "bad sha")
			}
			shas = append(shas, sha)
		}
		return c.sendJSON(200, map[string]any{"missing": c.s.volumes.missing(shas)})
	})
	handle("PUT", "/api/sync/objects/([0-9a-f]{64})", true, func(c *ctx, m []string) error {
		body, err := c.bodyBytes()
		if err != nil {
			return err
		}
		if err := c.s.volumes.putObject(m[1], body); err != nil {
			return err
		}
		c.w.WriteHeader(204)
		return nil
	})
	handle("GET", "/api/sync/objects/([0-9a-f]{64})", true, func(c *ctx, m []string) error {
		dir, name := c.s.volumes.objectDir(m[1])
		if _, err := os.Stat(filepath.Join(dir, name)); err != nil {
			return httpErr(404, "no such object")
		}
		c.w.Header().Set("Cache-Control", "private, max-age=31536000, immutable")
		return sendFile(c, source{os.DirFS(dir), name}, "", nil)
	})

	// ---- the relay ----
	handle("GET", "/api/relay", true, func(c *ctx, m []string) error {
		conns, socks := c.s.relay.counts()
		return c.sendJSON(200, map[string]any{"connections": conns, "sockets": socks})
	})
	handle("GET", "/relay", true, func(c *ctx, m []string) error {
		if !c.sameOrigin() {
			return httpErr(403, "not from this site")
		}
		return c.s.relay.serve(c)
	})
}

func intQuery(c *ctx, k string, def int64) (int64, error) {
	v, ok := c.query[k]
	if !ok {
		return def, nil
	}
	n, err := strconv.ParseInt(v, 10, 64)
	if err != nil {
		return 0, httpErr(400, k+": a number")
	}
	return n, nil
}

func isLoopback(remote string) bool {
	host, _, err := net.SplitHostPort(remote)
	if err != nil {
		host = remote
	}
	ip := net.ParseIP(host)
	return ip != nil && ip.IsLoopback()
}

func (s *Server) settingsView(c *ctx) map[string]any {
	s.mu.RLock()
	doc, eff, running := deepCopy(s.doc).(map[string]any), s.eff, s.running
	s.mu.RUnlock()
	doc["network"].(map[string]any)["password"] = eff.PasswordHash != ""
	doc["advanced"].(map[string]any)["data"] = s.data
	restart := []string{}
	if eff.Bind != running.Bind {
		restart = append(restart, "network.bind")
	}
	if eff.Port != running.Port && running.Port != 0 {
		restart = append(restart, "network.port")
	}
	if eff.TLSCert != running.TLSCert {
		restart = append(restart, "network.tls_cert")
	}
	if eff.TLSKey != running.TLSKey {
		restart = append(restart, "network.tls_key")
	}
	return map[string]any{"version": settingsSchema.Version, "settings": doc,
		"scopes": []string{"page", "server"}, "restart_needed": restart,
		"can_change_server": s.passwordSet() || isLoopback(c.r.RemoteAddr), "file": s.settingsPath}
}

const loginPage = `<!doctype html>
<html><head><meta charset="utf-8"><title>Executor: sign in</title>
<meta name="viewport" content="width=device-width">
<style>body{font:15px system-ui,sans-serif;max-width:22em;margin:5em auto}input,button{font:inherit;padding:.3em}#err{color:#b00}</style>
</head><body>
<h1>Executor</h1>
<form id="f"><p><label>Password <input id="pw" type="password" autofocus autocomplete="current-password"></label>
<button>Sign in</button></p><p id="err"></p></form>
<script>
document.getElementById('f').onsubmit = async (e) => {
    e.preventDefault();
    const res = await fetch('/api/login', { method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ password: document.getElementById('pw').value }) });
    if (res.ok) {
        const next = new URLSearchParams(location.search).get('next') || '/';
        location.href = next.startsWith('/') && !next.startsWith('//') ? next : '/';
    } else
        document.getElementById('err').textContent = (await res.json()).error;
};
</script></body></html>
`
