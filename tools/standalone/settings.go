package main

// The settings: DATA/settings.json, GET/POST /api/settings. The keys are
// tools/web/server's configuration ([server], [library], [relay] of
// server.example.toml) in one flat object, less the accounts' (no
// session_days, no public: a password or nothing). Command-line flags
// override them for the run and are not saved.
//
// What a POST changes applies at once (the library, the relay's policy
// for new connections, the password), except bind, port and the TLS
// files: those at the next start.

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
	"strconv"
	"strings"
)

type RelaySettings struct {
	Enabled        bool   `json:"enabled"`
	Ports          []any  `json:"ports"` // ints and "A-B" ranges
	AllowPrivate   bool   `json:"allow_private"`
	MaxSockets     int    `json:"max_sockets"`
	MaxConnections int    `json:"max_connections"`
	Nameserver     string `json:"nameserver"` // QUERY's; "": the host's
}

type Settings struct {
	Bind           string        `json:"bind"`
	Port           int           `json:"port"`
	Images         string        `json:"images"`
	Include        []string      `json:"include"`
	Exclude        []string      `json:"exclude"`
	SecureCookies  any           `json:"secure_cookies"` // "auto", true or false
	TrustedProxies []string      `json:"trusted_proxies"`
	MaxUploadMB    int           `json:"max_upload_mb"`
	TLSCert        string        `json:"tls_cert"`
	TLSKey         string        `json:"tls_key"`
	Relay          RelaySettings `json:"relay"`
	PasswordHash   string        `json:"password_hash,omitempty"` // pbkdf2: salt$hash
}

func defaultSettings(data string) Settings {
	return Settings{
		Bind:           "127.0.0.1",
		Port:           8090,
		Images:         filepath.Join(data, "images"),
		Include:        []string{"*.img"},
		Exclude:        []string{},
		SecureCookies:  "auto",
		TrustedProxies: []string{},
		MaxUploadMB:    64,
		Relay: RelaySettings{
			Enabled: true,
			Ports: []any{7, 13, 20, 21, 23, 25, 37, 43, 53, 70, 79, 80, 110, 113, 119, 123, 143,
				194, 443, 563, 993, 995, 1080, 6667, 8080},
			MaxSockets:     32,
			MaxConnections: 4,
		},
	}
}

// check says what is wrong with s, if anything.
func (s *Settings) check() error {
	if s.Port < 0 || s.Port > 65535 {
		return errors.New("port: 0-65535")
	}
	if s.MaxUploadMB < 1 {
		return errors.New("max_upload_mb: at least 1")
	}
	switch v := s.SecureCookies.(type) {
	case bool:
	case string:
		if v != "auto" {
			return errors.New(`secure_cookies: "auto", true or false`)
		}
	default:
		return errors.New(`secure_cookies: "auto", true or false`)
	}
	if _, err := parsePorts(s.Relay.Ports); err != nil {
		return err
	}
	if s.Relay.MaxSockets < 0 || s.Relay.MaxConnections < 0 {
		return errors.New("relay: limits are not negative")
	}
	for _, p := range append(append([]string{}, s.Include...), s.Exclude...) {
		if _, err := filepath.Match(p, ""); err != nil {
			return fmt.Errorf("pattern %q: %v", p, err)
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
			return nil, fmt.Errorf("relay ports: %v is not a port or A-B", p)
		}
		lo, err := strconv.Atoi(strings.TrimSpace(a))
		if err != nil {
			return nil, fmt.Errorf("relay ports: %v", p)
		}
		hi := lo
		if b != "" {
			if hi, err = strconv.Atoi(strings.TrimSpace(b)); err != nil {
				return nil, fmt.Errorf("relay ports: %v", p)
			}
		}
		out = append(out, portRange{lo, hi})
	}
	return out, nil
}

func loadSettings(path, data string) (Settings, error) {
	s := defaultSettings(data)
	b, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return s, nil
	}
	if err != nil {
		return s, err
	}
	if err := strictUnmarshal(b, &s); err != nil {
		return s, fmt.Errorf("%s: %v", path, err)
	}
	return s, s.check()
}

func strictUnmarshal(b []byte, v any) error {
	d := json.NewDecoder(strings.NewReader(string(b)))
	d.DisallowUnknownFields()
	return d.Decode(v)
}

func saveSettings(path string, s Settings) error {
	b, err := json.MarshalIndent(s, "", "  ")
	if err != nil {
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
