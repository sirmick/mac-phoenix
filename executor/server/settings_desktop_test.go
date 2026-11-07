package main

// A settings.json the desktop application wrote (frontends/desktop/
// settings.cpp: executor/tests/fixtures/settings/desktop.json, made by
// executor/tests/desktop_settings.cpp) loads here: version 2, valid for
// the schema, its values kept, the rest the defaults.

import (
	"os"
	"path/filepath"
	"testing"
)

func TestSettingsFromTheDesktop(t *testing.T) {
	b, err := os.ReadFile(filepath.Join("..", "tests", "fixtures", "settings", "desktop.json"))
	if err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(t.TempDir(), "settings.json")
	if err := os.WriteFile(path, b, 0o600); err != nil {
		t.Fatal(err)
	}
	doc, err := loadSettings(path)
	if err != nil {
		t.Fatal(err)
	}
	m := doc["machine"].(map[string]any)
	if doc["version"].(float64) != 2 || m["screen"] != "800x600" || m["system"] != "/library/boot-8.6.img" || m["ppc"] != true {
		t.Fatalf("machine %v version %v", m, doc["version"])
	}
	if v := doc["sound"].(map[string]any)["volume"]; v != 55.0 {
		t.Fatalf("volume %v", v)
	}
	if l := doc["disks"].(map[string]any)["mounted"].([]any); len(l) != 2 || l[0] != "/library/utilities.img" {
		t.Fatalf("mounted %v", l)
	}
	if p := doc["network"].(map[string]any)["port"]; p != 8090.0 { // a default filled in
		t.Fatalf("port %v", p)
	}
}
