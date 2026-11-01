package main

// The settings: settings.json (--config; default DATA/settings.json), GET
// and POST /api/settings. settings.schema.json is their one description
// (schema.go): the categories of the tab's settings dialog
// (web/executor/settings.js), each setting's default, type, range, label
// and help. The page's (x-scope page) are the tab's: kept here for it, used
// by the page; the server's (x-scope server) are this program's. Version 1
// files (flat, this program's first) are migrated on load and saved as 2.
//
//	GET  -> { version, settings, scopes, restart_needed, can_change_server }
//	POST a JSON merge patch (RFC 7396) of settings: null puts a setting, or
//	     a whole category, back to its default; network.password a string
//	     sets the password ("": none). The settings after, as GET.
//
// Command-line flags override settings for the run and are not saved.
// What a POST changes applies at once (the library, the relay's policy
// for new connections, the password), except x-apply server ones (bind,
// port, the TLS files): at the next start, listed in restart_needed.

import (
	"crypto/pbkdf2"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
)

type RelaySettings struct {
	Enabled        bool
	Ports          []any // ints and "A-B" ranges
	AllowPrivate   bool
	MaxSockets     int
	MaxConnections int
	Nameserver     string // QUERY's; "": the host's
}

// Settings: what of the document this program uses, typed.
type Settings struct {
	Bind           string
	Port           int
	Images         string
	Include        []string
	Exclude        []string
	SecureCookies  any // "auto", true or false
	TrustedProxies []string
	MaxUploadMB    int
	TLSCert        string
	TLSKey         string
	Relay          RelaySettings
	PasswordHash   string // pbkdf2: salt$hash
}

// typed: doc's server settings; images "" is DATA/images.
func typed(doc map[string]any, hash, data string) Settings {
	var f struct {
		Disks struct {
			Images  string   `json:"images"`
			Include []string `json:"include"`
			Exclude []string `json:"exclude"`
		} `json:"disks"`
		Network struct {
			Bind                string   `json:"bind"`
			Port                int      `json:"port"`
			RelayEnabled        bool     `json:"relay_enabled"`
			RelayPorts          []any    `json:"relay_ports"`
			RelayAllowPrivate   bool     `json:"relay_allow_private"`
			RelayMaxSockets     int      `json:"relay_max_sockets"`
			RelayMaxConnections int      `json:"relay_max_connections"`
			RelayNameserver     string   `json:"relay_nameserver"`
			TrustedProxies      []string `json:"trusted_proxies"`
			SecureCookies       any      `json:"secure_cookies"`
			TLSCert             string   `json:"tls_cert"`
			TLSKey              string   `json:"tls_key"`
			MaxUploadMB         int      `json:"max_upload_mb"`
		} `json:"network"`
	}
	b, _ := json.Marshal(doc)
	json.Unmarshal(b, &f)
	d, n := f.Disks, f.Network
	if d.Images == "" {
		d.Images = filepath.Join(data, "images")
	} else {
		d.Images = expand(d.Images)
	}
	return Settings{Bind: n.Bind, Port: n.Port, Images: d.Images, Include: d.Include, Exclude: d.Exclude,
		SecureCookies: n.SecureCookies, TrustedProxies: n.TrustedProxies, MaxUploadMB: n.MaxUploadMB,
		TLSCert: n.TLSCert, TLSKey: n.TLSKey, PasswordHash: hash,
		Relay: RelaySettings{n.RelayEnabled, n.RelayPorts, n.RelayAllowPrivate, n.RelayMaxSockets,
			n.RelayMaxConnections, n.RelayNameserver}}
}

func defaultDoc() map[string]any { return settingsSchema.defaults().(map[string]any) }

// check: doc against the schema, and what it can't say.
func check(doc map[string]any) error {
	if err := settingsSchema.validate(doc, "", false); err != nil {
		return err
	}
	n, _ := doc["network"].(map[string]any)
	if n != nil {
		if p, ok := n["relay_ports"].([]any); ok {
			if _, err := parsePorts(p); err != nil {
				return &fieldError{"network.relay_ports", err.Error()}
			}
		}
	}
	d, _ := doc["disks"].(map[string]any)
	for _, key := range []string{"include", "exclude"} {
		l, _ := d[key].([]any)
		for _, p := range l {
			if _, err := filepath.Match(p.(string), ""); err != nil {
				return &fieldError{"disks." + key, fmt.Sprintf("pattern %q: %v", p, err)}
			}
		}
	}
	return nil
}

type portRange struct{ lo, hi int }

func parsePorts(spec []any) ([]portRange, error) {
	var out []portRange
	for _, p := range spec {
		var a, b string
		switch v := p.(type) {
		case float64:
			a = strconv.Itoa(int(v))
		case int:
			a = strconv.Itoa(v)
		case string:
			a, b, _ = strings.Cut(v, "-")
		default:
			return nil, fmt.Errorf("%v is not a port or A-B", p)
		}
		lo, err := strconv.Atoi(strings.TrimSpace(a))
		if err != nil {
			return nil, fmt.Errorf("%v is not a port or A-B", p)
		}
		hi := lo
		if b != "" {
			if hi, err = strconv.Atoi(strings.TrimSpace(b)); err != nil {
				return nil, fmt.Errorf("%v is not a port or A-B", p)
			}
		}
		if lo < 1 || hi > 65535 || lo > hi {
			return nil, fmt.Errorf("%v: ports are 1-65535, A-B with A <= B", p)
		}
		out = append(out, portRange{lo, hi})
	}
	return out, nil
}

// patchSettings: doc with patch applied (a merge patch, password taken
// out), checked; and whether it changes any of the server's settings.
// doc is not changed.
func patchSettings(doc, patch map[string]any) (map[string]any, bool, error) {
	patch = deepCopy(patch).(map[string]any)
	delete(patch, "version")
	// Each setting named exists and may be written; types and ranges are
	// the result's to check.
	paths := []string{}
	leaves(patch, "", settingsSchema, &paths)
	sort.Strings(paths)
	server := false
	for _, p := range paths {
		n := settingsSchema.lookup(p)
		if n == nil {
			return nil, false, &fieldError{p, "no such setting"}
		}
		if n.ReadOnly {
			return nil, false, &fieldError{p, "read-only"}
		}
		server = server || n.Scope == "server"
	}
	next := settingsSchema.fill(mergePatch(deepCopy(doc).(map[string]any), patch))
	if err := check(next); err != nil {
		return nil, false, err
	}
	return next, server, nil
}

// loadSettings: the file's document, migrated, filled with defaults and
// checked, and the password's hash (kept beside the settings, never sent).
func loadSettings(path string) (map[string]any, string, error) {
	b, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return defaultDoc(), "", nil
	}
	if err != nil {
		return nil, "", err
	}
	var doc map[string]any
	if err := json.Unmarshal(b, &doc); err != nil || doc == nil {
		return nil, "", fmt.Errorf("%s: not a JSON object", path)
	}
	old, _ := doc["version"].(float64)
	if doc, err = migrate(doc); err != nil {
		return nil, "", fmt.Errorf("%s: %v", path, err)
	}
	hash, _ := doc["password_hash"].(string)
	delete(doc, "password_hash")
	doc = settingsSchema.fill(doc)
	if err := check(doc); err != nil {
		return nil, "", fmt.Errorf("%s: %v", path, err)
	}
	if old < float64(settingsSchema.Version) { // migrated: saved as it is now
		if err := saveSettings(path, doc, hash); err != nil {
			return nil, "", err
		}
	}
	return doc, hash, nil
}

func saveSettings(path string, doc map[string]any, hash string) error {
	out := deepCopy(doc).(map[string]any)
	if hash != "" {
		out["password_hash"] = hash
	}
	b, err := json.MarshalIndent(out, "", "  ")
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	return writeAtomic(path, append(b, '\n'), 0o600)
}

// writeAtomic writes path through a temporary file beside it.
func writeAtomic(path string, data []byte, mode os.FileMode) error {
	tmp, err := os.CreateTemp(filepath.Dir(path), filepath.Base(path)+".*.tmp")
	if err != nil {
		return err
	}
	_, err = tmp.Write(data)
	if err == nil {
		err = tmp.Sync()
	}
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err == nil {
		err = os.Chmod(tmp.Name(), mode)
	}
	if err == nil {
		err = os.Rename(tmp.Name(), path)
	}
	if err != nil {
		os.Remove(tmp.Name())
	}
	return err
}

// ---- the password ----
const pbkdfRounds = 200000

func hashPassword(pw string) string {
	salt := make([]byte, 16)
	rand.Read(salt)
	key, _ := pbkdf2.Key(sha256.New, pw, salt, pbkdfRounds, 32)
	return hex.EncodeToString(salt) + "$" + hex.EncodeToString(key)
}

func checkPassword(hash, pw string) bool {
	salt, want, ok := strings.Cut(hash, "$")
	if !ok {
		return false
	}
	s, err1 := hex.DecodeString(salt)
	w, err2 := hex.DecodeString(want)
	if err1 != nil || err2 != nil {
		return false
	}
	key, err := pbkdf2.Key(sha256.New, pw, s, pbkdfRounds, len(w))
	return err == nil && subtle.ConstantTimeCompare(key, w) == 1
}
