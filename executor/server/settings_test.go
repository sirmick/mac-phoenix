package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
)

// The schema here is executor/web's.
func TestSchemaCopy(t *testing.T) {
	page, err := os.ReadFile("../web/settings.schema.json")
	if err != nil {
		t.Skip(err)
	}
	if !bytes.Equal(page, schemaJSON) {
		t.Fatal("settings.schema.json differs from executor/web's: cp ../web/settings.schema.json .")
	}
}

type settingsCases struct {
	Patches []struct {
		Start  map[string]any `json:"start"`
		Patch  map[string]any `json:"patch"`
		Error  string         `json:"error"`
		Expect map[string]any `json:"expect"`
	} `json:"patches"`
	Migrations []struct {
		V1     map[string]any `json:"v1"`
		Error  string         `json:"error"`
		Expect map[string]any `json:"expect"`
	} `json:"migrations"`
}

func loadCases(t *testing.T) settingsCases {
	b, err := os.ReadFile("../tests/web/settings_cases.json")
	if err != nil {
		t.Skip(err)
	}
	var c settingsCases
	if err := json.Unmarshal(b, &c); err != nil {
		t.Fatal(err)
	}
	return c
}

func at(doc map[string]any, path string) any {
	var v any = doc
	for _, k := range strings.Split(path, ".") {
		m, _ := v.(map[string]any)
		v = m[k]
	}
	return v
}

func expectAt(t *testing.T, what string, doc, expect map[string]any) {
	for p, want := range expect {
		if got := at(doc, p); !reflect.DeepEqual(got, want) {
			t.Errorf("%s: %s = %#v, want %#v", what, p, got, want)
		}
	}
}

func TestSettingsCases(t *testing.T) {
	c := loadCases(t)
	if err := check(defaultDoc()); err != nil {
		t.Fatalf("the defaults: %v", err)
	}
	for i, k := range c.Patches {
		what, _ := json.Marshal(k.Patch)
		doc := defaultDoc()
		if k.Start != nil {
			var err error
			if doc, err = patchSettings(doc, k.Start); err != nil {
				t.Fatalf("%d start: %v", i, err)
			}
		}
		next, err := patchSettings(doc, k.Patch)
		if k.Error != "" {
			var fe *fieldError
			if !errors.As(err, &fe) || fe.Path != k.Error {
				t.Errorf("%s: %v, want an error at %s", what, err, k.Error)
			}
			continue
		}
		if err != nil {
			t.Errorf("%s: %v", what, err)
			continue
		}
		expectAt(t, string(what), next, k.Expect)
	}
	for _, k := range c.Migrations {
		what, _ := json.Marshal(k.V1)
		dir := t.TempDir()
		path := filepath.Join(dir, "settings.json")
		os.WriteFile(path, what, 0o600)
		doc, err := loadSettings(path)
		if k.Error != "" {
			if err == nil || !strings.Contains(err.Error(), k.Error) {
				t.Errorf("%s: %v, want an error at %s", what, err, k.Error)
			}
			continue
		}
		if err != nil {
			t.Errorf("%s: %v", what, err)
			continue
		}
		expectAt(t, string(what), doc, k.Expect)
		// saved as version 2, and read back the same
		again, err := loadSettings(path)
		if err != nil || !reflect.DeepEqual(again, doc) {
			t.Errorf("%s: read again: %v", what, err)
		}
	}
}

// POST and GET /api/settings: saved, kept across a restart, invalid ones
// refused with where; a password and a paired list from before the claim dropped.
func TestSettingsAPI(t *testing.T) {
	data := t.TempDir()
	path := filepath.Join(data, "conf", "settings.json")
	os.MkdirAll(filepath.Dir(path), 0o755)
	os.WriteFile(path, []byte(`{"version":2,"password_hash":"aa$bb","network":{"password":true,"paired":[],"port":9000}}`), 0o600)
	var token string
	start := func() *Server {
		doc, err := loadSettings(path)
		if err != nil {
			t.Fatal(err)
		}
		s := &Server{data: data, settingsPath: path, ids: newIds(filepath.Join(data, "ids.json"))}
		token, _ = s.claim.take() // each start: a new claim
		s.applySettings(doc)
		return s
	}
	call := func(s *Server, method, body string) (int, map[string]any) {
		r := httptest.NewRequest(method, "/api/settings", strings.NewReader(body))
		r.RemoteAddr = "192.0.2.7:5000" // not loopback: nothing special
		r.Header.Set("Content-Type", "application/json")
		r.AddCookie(&http.Cookie{Name: cookieName(r.Host), Value: token})
		w := httptest.NewRecorder()
		s.ServeHTTP(w, r)
		var v map[string]any
		json.Unmarshal(w.Body.Bytes(), &v)
		return w.Code, v
	}
	s := start()
	if b, _ := os.ReadFile(path); bytes.Contains(b, []byte("password")) || bytes.Contains(b, []byte("paired")) {
		t.Errorf("the old password or paired list kept: %s", b)
	}
	code, v := call(s, "POST", `{"machine":{"ram_mb":96},"network":{"port":9001}}`)
	if code != 200 {
		t.Fatalf("POST: %d %v", code, v)
	}
	if got := v["restart_needed"].([]any); len(got) != 1 || got[0] != "network.port" {
		t.Errorf("restart_needed %v", got)
	}
	if v["can_change_server"] != true {
		t.Errorf("can_change_server %v", v["can_change_server"])
	}
	code, v = call(s, "POST", `{"sound":{"volume":"loud"}}`)
	if code != 400 || v["path"] != "sound.volume" {
		t.Errorf("invalid: %d %v", code, v)
	}
	if code, v = call(s, "POST", `{"advanced":{"data":"/elsewhere"}}`); code != 400 || v["path"] != "advanced.data" {
		t.Errorf("data is read-only: %d %v", code, v)
	}
	s = start() // again, from the file
	_, v = call(s, "GET", "")
	st := v["settings"].(map[string]any)
	if at(st, "machine.ram_mb") != 96.0 || at(st, "network.port") != 9001.0 || at(st, "sound.volume") != 80.0 {
		t.Errorf("not kept: %v", st)
	}
	if at(st, "network.paired") != nil || at(st, "advanced.data") != data {
		t.Errorf("read-only ones: %v", st)
	}
}
