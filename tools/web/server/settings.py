"""Each user's settings for the tab: users/ID/settings.json in the data
directory, the page's half of web/executor/settings.schema.json (x-scope
page; the server's own are server.toml's). The API is tools/standalone's
(settings.go):

    GET  -> { version, settings, scopes: ["page"], restart_needed: [],
              can_change_server: false }
    POST a JSON merge patch (RFC 7396) of settings: null puts a setting or a
         category back to its default. A server setting is refused (403).

The schema's checks as tools/standalone/schema.go makes them (the cases in
web/tests/settings_cases.json hold both to it).
"""
import copy
import fnmatch
import json
import math
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
SCHEMA_PATH = os.path.join(ROOT, "web", "executor", "settings.schema.json")


class FieldError(ValueError):
    def __init__(self, path, msg):
        super().__init__(f"{path}: {msg}" if path else msg)
        self.path = path


def _join(path, key):
    return f"{path}.{key}" if path else key


def json_type(v):
    if v is None:
        return "null"
    if isinstance(v, bool):
        return "boolean"
    if isinstance(v, int):
        return "integer"
    if isinstance(v, float):
        return "integer" if v.is_integer() and not math.isinf(v) else "number"
    if isinstance(v, str):
        return "string"
    if isinstance(v, list):
        return "array"
    if isinstance(v, dict):
        return "object"
    return type(v).__name__


def _same(a, b):
    return json_type(a) == json_type(b) and a == b


class Schema:
    def __init__(self, node):
        self.node = node
        self.version = node.get("x-version", 1)

    @classmethod
    def load(cls, path=SCHEMA_PATH):
        with open(path, encoding="utf-8") as f:
            return cls(json.load(f))

    # ---- checks ----
    def validate(self, v, node=None, path=""):
        n = self.node if node is None else node
        t = n.get("type")
        want = [t] if isinstance(t, str) else (t or [])
        got = json_type(v)
        if want and not any(w == got or (w == "number" and got == "integer") for w in want):
            raise FieldError(path, f"{' or '.join(want)}, not {got}")
        if "enum" in n and not any(_same(e, v) for e in n["enum"]):
            raise FieldError(path, "one of " + ", ".join(json.dumps(e) for e in n["enum"]))
        if got in ("integer", "number"):
            if "minimum" in n and v < n["minimum"]:
                raise FieldError(path, f"at least {n['minimum']}")
            if "maximum" in n and v > n["maximum"]:
                raise FieldError(path, f"at most {n['maximum']}")
        elif got == "string":
            if "minLength" in n and len(v) < n["minLength"]:
                raise FieldError(path, f"at least {n['minLength']} characters")
            if "maxLength" in n and len(v) > n["maxLength"]:
                raise FieldError(path, f"at most {n['maxLength']} characters")
            if "pattern" in n and not re.search(n["pattern"], v):
                raise FieldError(path, f"{json.dumps(v)} does not match {n['pattern']}")
        elif got == "array":
            if "maxItems" in n and len(v) > n["maxItems"]:
                raise FieldError(path, f"at most {n['maxItems']} items")
            if "items" in n:
                for i, e in enumerate(v):
                    self.validate(e, n["items"], f"{path}[{i}]")
        elif got == "object":
            props = n.get("properties", {})
            for k in sorted(v):
                if k not in props:
                    if n.get("additionalProperties") is False:
                        raise FieldError(_join(path, k), "no such setting")
                    continue
                self.validate(v[k], props[k], _join(path, k))

    def check(self, doc):
        """doc against the schema, and what it can't say."""
        self.validate(doc)
        for key in ("include", "exclude"):
            for p in doc.get("disks", {}).get(key, []):
                try:
                    fnmatch.translate(p)
                except re.error as e:
                    raise FieldError(f"disks.{key}", f"pattern {p!r}: {e}")
        for p in doc.get("network", {}).get("relay_ports", []):
            lo, _, hi = str(p).partition("-")
            lo, hi = int(lo), int(hi or lo)
            if lo < 1 or hi > 65535 or lo > hi:
                raise FieldError("network.relay_ports", f"{p}: ports are 1-65535, A-B with A <= B")

    # ---- the document ----
    def defaults(self, node=None):
        n = self.node if node is None else node
        if "properties" not in n:
            return copy.deepcopy(n.get("default"))
        return {k: self.defaults(p) for k, p in n["properties"].items() if not p.get("readOnly")}

    def fill(self, doc, node=None):
        n = self.node if node is None else node
        for k, p in n.get("properties", {}).items():
            if p.get("readOnly"):
                continue
            if k not in doc:
                doc[k] = self.defaults(p)
            elif isinstance(doc[k], dict) and "properties" in p:
                self.fill(doc[k], p)
        return doc

    def lookup(self, path):
        n = self.node
        for k in path.split("."):
            n = n.get("properties", {}).get(k)
            if n is None:
                return None
        return n

    def leaves(self, patch, path="", node=None):
        n = self.node if node is None else node
        out = []
        for k, v in patch.items():
            p = _join(path, k)
            child = n.get("properties", {}).get(k) if n else None
            if isinstance(v, dict) and child and "properties" in child:
                out += self.leaves(v, p, child)
            elif v is None and child and "properties" in child:
                out += [_join(p, ck) for ck in child["properties"]]
            else:
                out.append(p)
        return out

    def patch(self, doc, patch, scopes=("page", "server")):
        """doc with patch applied, checked (doc unchanged). A setting
        outside scopes: PermissionError."""
        patch = {k: v for k, v in patch.items() if k != "version"}
        for p in sorted(self.leaves(patch)):
            n = self.lookup(p)
            if n is None:
                raise FieldError(p, "no such setting")
            if n.get("readOnly"):
                raise FieldError(p, "read-only")
            if "properties" not in n and n.get("x-scope") not in scopes:
                raise PermissionError(f"{p}: set by the site's configuration")
        out = self.fill(merge_patch(copy.deepcopy(doc), patch))
        self.check(out)
        return out

    def page_only(self, doc):
        """doc less the server's settings (and categories with none left)."""
        out = {"version": doc.get("version", self.version)}
        for cat, n in self.node["properties"].items():
            if "properties" not in n:
                continue
            keep = {k: copy.deepcopy(doc.get(cat, {}).get(k)) for k, p in n["properties"].items()
                    if p.get("x-scope") == "page" and not p.get("readOnly")}
            if keep:
                out[cat] = keep
        return out


def merge_patch(doc, patch):
    """RFC 7396, in place."""
    for k, v in patch.items():
        if v is None:
            doc.pop(k, None)
        elif isinstance(v, dict):
            doc[k] = merge_patch(doc[k] if isinstance(doc.get(k), dict) else {}, v)
        else:
            doc[k] = copy.deepcopy(v)
    return doc


class SettingsStore:
    """users/ID/settings.json: the page's settings of each user."""
    def __init__(self, data_dir, schema=None):
        self.root = os.path.join(data_dir, "users")
        self.schema = schema or Schema.load()

    def _path(self, user_id):
        return os.path.join(self.root, str(int(user_id)), "settings.json")

    def get(self, user_id):
        doc = self.schema.defaults()
        try:
            with open(self._path(user_id), encoding="utf-8") as f:
                saved = json.load(f)
            # Kept only if still valid: a schema that narrowed a range
            # puts that setting back to its default.
            for cat, values in saved.items():
                if isinstance(values, dict) and isinstance(doc.get(cat), dict):
                    for k, v in values.items():
                        try:
                            doc = self.schema.patch(doc, {cat: {k: v}}, scopes=("page",))
                        except (ValueError, PermissionError):
                            pass
        except (OSError, ValueError):
            pass
        doc["version"] = self.schema.version
        return doc

    def post(self, user_id, patch):
        doc = self.schema.patch(self.get(user_id), patch, scopes=("page",))
        path = self._path(user_id)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        tmp = f"{path}.{os.getpid()}.tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.schema.page_only(doc), f, indent=2)
            f.write("\n")
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
        return doc

    def view(self, doc):
        return {"version": self.schema.version, "settings": self.schema.page_only(doc), "scopes": ["page"],
                "restart_needed": [], "can_change_server": False}
