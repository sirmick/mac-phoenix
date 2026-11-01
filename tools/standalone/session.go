package main

// One controlling tab, as tools/web/server/session.py: each tab says it
// is there every few seconds (POST /api/session?id=ID); the first gets
// control and keeps it while it does; the others are viewers, shown the
// controller's screen (PUT/GET /api/session/screen, a PNG). take=1
// takes control: the old controller sees it at its next call, saves and
// lets go (release); the new one waits for that (handing) before it
// reads the store. A tab silent for sessionLease has gone.

import (
	"bytes"
	"sync"
	"time"
)

const (
	sessionLease = 10 * time.Second
	screenMax    = 8 << 20
)

type TabSessions struct {
	mu      sync.Mutex
	holder  string
	handing string
	seen    map[string]time.Time
	screen  []byte
}

// prune forgets tabs silent for the lease. Under mu.
func (t *TabSessions) prune(now time.Time) {
	if t.seen == nil {
		t.seen = map[string]time.Time{}
	}
	for k, at := range t.seen {
		if now.Sub(at) >= sessionLease {
			delete(t.seen, k)
		}
	}
	if _, ok := t.seen[t.holder]; !ok {
		t.holder, t.screen = "", nil // its screen goes with it, not to the next controller
	}
	if _, ok := t.seen[t.handing]; !ok {
		t.handing = ""
	}
}

func (t *TabSessions) answer(id string) map[string]any {
	viewers := 0
	for k := range t.seen {
		if k != t.holder {
			viewers++
		}
	}
	return map[string]any{"control": t.holder == id, "held": t.holder != "" && t.holder != id,
		"handing": t.handing != "" && t.holder == id, "viewers": viewers}
}

func (t *TabSessions) beat(id string, take bool) map[string]any {
	t.mu.Lock()
	defer t.mu.Unlock()
	now := time.Now()
	t.prune(now)
	t.seen[id] = now
	if t.holder == "" {
		t.holder = id
	} else if take && t.holder != id {
		t.handing, t.holder, t.screen = t.holder, id, nil
	}
	return t.answer(id)
}

func (t *TabSessions) release(id string) {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.prune(time.Now())
	delete(t.seen, id)
	if t.holder == id {
		t.holder, t.screen = "", nil
	}
	if t.handing == id {
		t.handing = ""
	}
}

func (t *TabSessions) putScreen(id string, png []byte) bool {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.prune(time.Now())
	if t.holder != id {
		return false
	}
	t.screen = png
	return true
}

func (t *TabSessions) getScreen() []byte {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.prune(time.Now())
	if t.holder == "" {
		return nil
	}
	return t.screen
}

func sessionID(c *ctx) (string, error) {
	id := c.query["id"]
	if !logSessionRe.MatchString(id) {
		return "", httpErr(400, "id: 8-40 letters, digits, -")
	}
	return id, nil
}

func init() {
	handle("POST", "/api/session", true, func(c *ctx, m []string) error {
		id, err := sessionID(c)
		if err != nil {
			return err
		}
		return c.sendJSON(200, c.s.tabs.beat(id, c.query["take"] == "1"))
	})
	handle("POST", "/api/session/release", true, func(c *ctx, m []string) error {
		id, err := sessionID(c)
		if err != nil {
			return err
		}
		c.s.tabs.release(id)
		return c.sendJSON(200, map[string]any{})
	})
	handle("PUT", "/api/session/screen", true, func(c *ctx, m []string) error {
		id, err := sessionID(c)
		if err != nil {
			return err
		}
		png, err := c.bodyLimit(screenMax)
		if err != nil {
			return err
		}
		if !bytes.HasPrefix(png, []byte("\x89PNG\r\n\x1a\n")) {
			return httpErr(400, "a PNG")
		}
		if !c.s.tabs.putScreen(id, png) {
			return httpErr(409, "not the controlling tab")
		}
		c.w.WriteHeader(204)
		return nil
	})
	handle("GET", "/api/session/screen", true, func(c *ctx, m []string) error {
		png := c.s.tabs.getScreen()
		if png == nil {
			return httpErr(404, "no screen")
		}
		c.w.Header().Set("Cache-Control", "no-store")
		return c.sendBytes(200, png, "image/png")
	})
}
