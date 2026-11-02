package main

// settings.schema.json (web/executor's; the copy here is checked equal by
// go test): the settings' defaults, types and ranges. What of JSON Schema
// it uses, and so what this checks: type (one or a list), enum, minimum,
// maximum, minLength, maxLength, pattern, items, maxItems, properties,
// additionalProperties: false, readOnly, default. web/executor/settings.js
// checks the same; the cases in web/tests/settings_cases.json hold both
// to it.

import (
	_ "embed"
	"encoding/json"
	"fmt"
	"math"
	"regexp"
	"sort"
	"strings"
)

//go:embed settings.schema.json
var schemaJSON []byte

type schemaNode struct {
	Type       any                    `json:"type"`
	Enum       []any                  `json:"enum"`
	Minimum    *float64               `json:"minimum"`
	Maximum    *float64               `json:"maximum"`
	MinLength  *int                   `json:"minLength"`
	MaxLength  *int                   `json:"maxLength"`
	Pattern    string                 `json:"pattern"`
	MaxItems   *int                   `json:"maxItems"`
	Items      *schemaNode            `json:"items"`
	Properties map[string]*schemaNode `json:"properties"`
	Additional *bool                  `json:"additionalProperties"`
	Default    any                    `json:"default"`
	ReadOnly   bool                   `json:"readOnly"`
	Scope      string                 `json:"x-scope"`
	Apply      string                 `json:"x-apply"`
	Version    int                    `json:"x-version"`
	re         *regexp.Regexp
}

var settingsSchema = mustSchema(schemaJSON)

func mustSchema(b []byte) *schemaNode {
	var n schemaNode
	if err := json.Unmarshal(b, &n); err != nil {
		panic("settings.schema.json: " + err.Error())
	}
	var compile func(*schemaNode)
	compile = func(n *schemaNode) {
		if n.Pattern != "" {
			n.re = regexp.MustCompile(n.Pattern)
		}
		if n.Items != nil {
			compile(n.Items)
		}
		for _, p := range n.Properties {
			compile(p)
		}
	}
	compile(&n)
	return &n
}

// fieldError: what is wrong, and where (dotted: "machine.ram_mb").
type fieldError struct{ Path, Msg string }

func (e *fieldError) Error() string {
	if e.Path == "" {
		return e.Msg
	}
	return e.Path + ": " + e.Msg
}

func join(path, key string) string {
	if path == "" {
		return key
	}
	return path + "." + key
}

func jsonType(v any) string {
	switch x := v.(type) {
	case nil:
		return "null"
	case bool:
		return "boolean"
	case float64:
		if x == math.Trunc(x) && !math.IsInf(x, 0) {
			return "integer"
		}
		return "number"
	case string:
		return "string"
	case []any:
		return "array"
	case map[string]any:
		return "object"
	}
	return fmt.Sprintf("%T", v)
}

func (n *schemaNode) types() []string {
	switch t := n.Type.(type) {
	case string:
		return []string{t}
	case []any:
		out := []string{}
		for _, x := range t {
			out = append(out, x.(string))
		}
		return out
	}
	return nil
}

func typeOK(want []string, got string) bool {
	if len(want) == 0 {
		return true
	}
	for _, w := range want {
		if w == got || (w == "number" && got == "integer") {
			return true
		}
	}
	return false
}

// validate checks v (as encoding/json decodes into any) against n.
// writing: a readOnly value is refused.
func (n *schemaNode) validate(v any, path string, writing bool) error {
	bad := func(f string, a ...any) error { return &fieldError{path, fmt.Sprintf(f, a...)} }
	if writing && n.ReadOnly {
		return bad("read-only")
	}
	got := jsonType(v)
	if want := n.types(); !typeOK(want, got) {
		return bad("%s, not %s", strings.Join(want, " or "), got)
	}
	if n.Enum != nil {
		found := false
		for _, e := range n.Enum {
			if e == v || (jsonType(e) == "integer" && got == "integer" && e.(float64) == v.(float64)) {
				found = true
			}
		}
		if !found {
			names := []string{}
			for _, e := range n.Enum {
				b, _ := json.Marshal(e)
				names = append(names, string(b))
			}
			return bad("one of %s", strings.Join(names, ", "))
		}
	}
	switch x := v.(type) {
	case float64:
		if n.Minimum != nil && x < *n.Minimum {
			return bad("at least %v", *n.Minimum)
		}
		if n.Maximum != nil && x > *n.Maximum {
			return bad("at most %v", *n.Maximum)
		}
	case string:
		l := len([]rune(x))
		if n.MinLength != nil && l < *n.MinLength {
			return bad("at least %d characters", *n.MinLength)
		}
		if n.MaxLength != nil && l > *n.MaxLength {
			return bad("at most %d characters", *n.MaxLength)
		}
		if n.re != nil && !n.re.MatchString(x) {
			return bad("%q does not match %s", x, n.Pattern)
		}
	case []any:
		if n.MaxItems != nil && len(x) > *n.MaxItems {
			return bad("at most %d items", *n.MaxItems)
		}
		if n.Items != nil {
			for i, e := range x {
				if err := n.Items.validate(e, fmt.Sprintf("%s[%d]", path, i), writing); err != nil {
					return err
				}
			}
		}
	case map[string]any:
		for _, k := range sortedKeys(x) {
			p, ok := n.Properties[k]
			if !ok {
				if n.Additional != nil && !*n.Additional {
					return &fieldError{join(path, k), "no such setting"}
				}
				continue
			}
			if err := p.validate(x[k], join(path, k), writing); err != nil {
				return err
			}
		}
	}
	return nil
}

func sortedKeys(m map[string]any) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

// defaults: the schema's whole document, readOnly values left out.
func (n *schemaNode) defaults() any {
	if n.Properties == nil {
		return deepCopy(n.Default)
	}
	out := map[string]any{}
	for k, p := range n.Properties {
		if !p.ReadOnly {
			out[k] = p.defaults()
		}
	}
	return out
}

// fill: doc with what it lacks from the defaults.
func (n *schemaNode) fill(doc map[string]any) map[string]any {
	for k, p := range n.Properties {
		if p.ReadOnly {
			continue
		}
		v, ok := doc[k]
		if !ok {
			doc[k] = p.defaults()
		} else if m, isObj := v.(map[string]any); isObj && p.Properties != nil {
			doc[k] = p.fill(m)
		}
	}
	return doc
}

// lookup: the schema of a dotted path's setting.
func (n *schemaNode) lookup(path string) *schemaNode {
	for _, k := range strings.Split(path, ".") {
		if n == nil || n.Properties == nil {
			return nil
		}
		n = n.Properties[k]
	}
	return n
}

// leaves: the dotted paths a patch sets (or resets, with null).
func leaves(patch map[string]any, path string, n *schemaNode, out *[]string) {
	for k, v := range patch {
		p := join(path, k)
		var child *schemaNode
		if n != nil && n.Properties != nil {
			child = n.Properties[k]
		}
		if m, ok := v.(map[string]any); ok && child != nil && child.Properties != nil {
			leaves(m, p, child, out)
		} else if v == nil && child != nil && child.Properties != nil {
			for ck, c := range child.Properties {
				if !c.ReadOnly {
					*out = append(*out, join(p, ck))
				}
			}
		} else {
			*out = append(*out, p)
		}
	}
}

// mergePatch applies patch to doc (RFC 7396: null removes; fill puts the
// default back), in place.
func mergePatch(doc map[string]any, patch map[string]any) map[string]any {
	for k, v := range patch {
		if v == nil {
			delete(doc, k)
			continue
		}
		if pm, ok := v.(map[string]any); ok {
			dm, _ := doc[k].(map[string]any)
			if dm == nil {
				dm = map[string]any{}
			}
			doc[k] = mergePatch(dm, pm)
			continue
		}
		doc[k] = deepCopy(v)
	}
	return doc
}

func deepCopy(v any) any {
	switch x := v.(type) {
	case map[string]any:
		m := make(map[string]any, len(x))
		for k, e := range x {
			m[k] = deepCopy(e)
		}
		return m
	case []any:
		a := make([]any, len(x))
		for i, e := range x {
			a[i] = deepCopy(e)
		}
		return a
	}
	return v
}

// migrate brings a settings file to the schema's version. Version 1 (no
// "version"): tools/standalone's first settings, flat.
func migrate(doc map[string]any) (map[string]any, error) {
	ver := 1.0
	if v, ok := doc["version"]; ok {
		f, isNum := v.(float64)
		if !isNum {
			return nil, &fieldError{"version", "a number"}
		}
		ver = f
	}
	if ver > float64(settingsSchema.Version) {
		return nil, &fieldError{"version", fmt.Sprintf("%v is newer than this program's (%d)", ver, settingsSchema.Version)}
	}
	if ver < 2 {
		v1 := doc
		doc = map[string]any{"version": 2.0}
		put := func(cat, key string, v any) {
			m, _ := doc[cat].(map[string]any)
			if m == nil {
				m = map[string]any{}
				doc[cat] = m
			}
			m[key] = v
		}
		for k, v := range v1 {
			switch k {
			case "images", "include", "exclude":
				put("disks", k, v)
			case "bind", "port", "secure_cookies", "trusted_proxies", "max_upload_mb", "tls_cert", "tls_key":
				put("network", k, v)
			case "password_hash":
				doc[k] = v
			case "relay":
				r, ok := v.(map[string]any)
				if !ok {
					return nil, &fieldError{"relay", "an object"}
				}
				for rk, rv := range r {
					put("network", "relay_"+rk, rv)
				}
			default:
				return nil, &fieldError{k, "no such setting (version 1)"}
			}
		}
	}
	doc["version"] = float64(settingsSchema.Version)
	return doc, nil
}
