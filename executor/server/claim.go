package main

// The claim (docs/executor/Standalone.md "Who uses it"): the first browser
// to load the tab has this server until it stops. The first GET of
// /index.html with no claim makes a token (256 random bits) and answers
// with it in a cookie (HttpOnly, SameSite=Strict, Secure over TLS); every
// route then needs that cookie: the site, the API, the disks, /relay, the
// logs. The claim is kept in memory only: each start of the server begins
// unclaimed, and the tab of the one before sees 401 (or 403, if another
// browser was first) and asks for a reload, which claims it again.
//
// Another browser gets a page saying the server is in use (403), and
// nothing else. A claim is taken only by a navigation from this site or
// none (the address bar), so another site can't take it by loading the
// address in the background. Within the browser that has it, one tab is
// in control (session.go).

import (
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"net"
	"sync"
)

type Claim struct {
	mu   sync.Mutex
	hash []byte // sha256 of the token; nil: unclaimed
}

// take: a new token, if no browser has the claim.
func (k *Claim) take() (string, bool) {
	k.mu.Lock()
	defer k.mu.Unlock()
	if k.hash != nil {
		return "", false
	}
	b := make([]byte, 32)
	rand.Read(b)
	token := hex.EncodeToString(b)
	h := sha256.Sum256([]byte(token))
	k.hash = h[:]
	return token, true
}

// holds: whether token is the claim's.
func (k *Claim) holds(token string) bool {
	if token == "" {
		return false
	}
	h := sha256.Sum256([]byte(token))
	k.mu.Lock()
	defer k.mu.Unlock()
	return k.hash != nil && subtle.ConstantTimeCompare(h[:], k.hash) == 1
}

func (k *Claim) claimed() bool {
	k.mu.Lock()
	defer k.mu.Unlock()
	return k.hash != nil
}

// cookieName: the claim's cookie for the address asked for. Cookies are
// kept by host, not port: two servers on one machine (8090 and 8091)
// each have their own.
func cookieName(host string) string {
	if _, port, err := net.SplitHostPort(host); err == nil && port != "" {
		return "executor_claim_" + port
	}
	return "executor_claim"
}

// mayClaim: the request may take the claim: a page load (not a fetch, an
// image, a frame) from this site or typed.
func (c *ctx) mayClaim() bool {
	if c.r.Method != "GET" || !c.sameOrigin() {
		return false
	}
	dest := c.r.Header.Get("Sec-Fetch-Dest")
	return dest == "" || dest == "document"
}

// refused: the answer to a request without the claim's cookie.
func (c *ctx) refused() error {
	if c.s.claim.claimed() {
		return httpErr(403, "in use by another browser", "claimed", true)
	}
	return httpErr(401, "the server has restarted: reload the tab", "reload", true)
}

// sendRefusedPage: the page another browser gets (or one that may not
// claim: a link to the tab, which may).
func (c *ctx) sendRefusedPage() error {
	c.w.Header().Set("Cache-Control", "no-store")
	if c.s.claim.claimed() {
		return c.sendBytes(403, []byte(inUsePage), "text/html; charset=utf-8")
	}
	return c.sendBytes(401, []byte(openPage), "text/html; charset=utf-8")
}

const inUsePage = `<!doctype html>
<html><head><meta charset="utf-8"><title>Executor: in use</title>
<meta name="viewport" content="width=device-width">
<style>body{font:15px system-ui,sans-serif;max-width:28em;margin:5em auto;padding:0 1em}</style>
</head><body>
<h1>Executor</h1>
<p id="in-use">Executor is in use by another browser.</p>
<p>The first browser to open it after the server starts has it. To use it here,
close it there and restart the server, then open this address again.</p>
</body></html>
`

const openPage = `<!doctype html>
<html><head><meta charset="utf-8"><title>Executor</title>
<meta name="viewport" content="width=device-width">
<style>body{font:15px system-ui,sans-serif;max-width:28em;margin:5em auto;padding:0 1em}</style>
</head><body>
<h1>Executor</h1>
<p><a id="open" href="/">Open Executor</a></p>
</body></html>
`
