"""The service: Executor's tab for its owner and the people they invite.

    /                 the tab (the site directory: web/executor + the module)
    /login, /account  signing in, an invitation, sign out; the owner's invitations
    /library.json     the library (library.py); /disks/NAME its images, with Range
    /api/me           { name, owner }, 401 when signed out
    /api/login        POST { name, password }      /api/logout POST
    /api/register     POST { invite, name, password }
    /api/invites      GET (owner) the invitations; POST a new one -> { code }
    /api/sync/...     the user's synced overlays and volumes (sync.py):
        GET    overlays/KEY?since=V   blocks newer than V: records of block
                                      number (uint32 LE) + 4 KB; X-Sync-Version,
                                      X-Sync-Reset: 1 when it was deleted since V
        POST   overlays/KEY?name=&size=  records -> { version }
        DELETE overlays/KEY           Revert -> { version }
        GET    volumes/KEY            { version, manifest }
        PUT    volumes/KEY            { manifest } -> { version } (409 { missing })
        DELETE volumes/KEY            Reset -> { version }
        POST   objects/missing        { shas } -> { missing }
        PUT    objects/SHA, GET objects/SHA
        GET    (nothing after sync/)  what is kept: { overlays, volumes }
    /relay            WebSocket, the relay's (relay.py: a stub)

Everything but /login and its API needs a session (the cookie), unless
the configuration makes the page public; sync always does. Writes from
another site are refused (Origin, Sec-Fetch-Site; the cookie is
SameSite=Strict too).
"""
import http.cookies
import http.server
import json
import os
import posixpath
import re
import threading
import time
from urllib.parse import parse_qs, unquote, urlsplit

from . import accounts as accounts_mod
from . import library, relay, static
from .sync import SyncError, SyncStore

HERE = os.path.dirname(os.path.abspath(__file__))
COOKIE = "executor_session"
LOGIN_FAILURES = 10                     # per address in LOGIN_WINDOW s, then 429
LOGIN_WINDOW = 600


class HttpError(Exception):
    def __init__(self, status, message="", **extra):
        super().__init__(message)
        self.status = status
        self.extra = extra


class App:
    def __init__(self, cfg):
        self.cfg = cfg
        data = cfg["server"]["data"]
        os.makedirs(data, exist_ok=True)
        self.accounts = accounts_mod.Accounts(os.path.join(data, "accounts.sqlite"))
        self.sync = SyncStore(data)
        self.failures = {}
        self.failures_lock = threading.Lock()

    def note_failure(self, address):
        now = time.time()
        with self.failures_lock:
            times = [t for t in self.failures.get(address, []) if t > now - LOGIN_WINDOW]
            times.append(now)
            self.failures[address] = times

    def too_many_failures(self, address):
        now = time.time()
        with self.failures_lock:
            return len([t for t in self.failures.get(address, []) if t > now - LOGIN_WINDOW]) >= LOGIN_FAILURES


ROUTES = []


def route(method, pattern, auth=True):
    def wrap(fn):
        ROUTES.append((method, re.compile(pattern), fn, auth))
        return fn
    return wrap


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "ExecutorWeb/1"
    app: App = None

    def log_message(self, fmt, *args):
        if not self.server.quiet:
            super().log_message(fmt, *args)

    # ---- plumbing ----
    def do_GET(self): self.dispatch("GET")
    def do_HEAD(self): self.dispatch("HEAD")
    def do_POST(self): self.dispatch("POST")
    def do_PUT(self): self.dispatch("PUT")
    def do_DELETE(self): self.dispatch("DELETE")

    def end_headers(self):
        self.send_header("Cache-Control", getattr(self, "cache", "no-cache"))
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "same-origin")
        super().end_headers()

    def body(self):
        if self._body is None:
            n = int(self.headers.get("Content-Length") or 0)
            if n > self.app.cfg["server"]["max_upload_mb"] << 20:
                self.close_connection = True
                raise HttpError(413, "too large")
            self._body = self.rfile.read(n) if n else b""
        return self._body

    def json_body(self):
        try:
            v = json.loads(self.body() or b"{}")
        except ValueError:
            raise HttpError(400, "not JSON")
        if not isinstance(v, dict):
            raise HttpError(400, "not a JSON object")
        return v

    def send_bytes(self, status, data, ctype="application/octet-stream", headers=()):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        for k, v in headers:
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def send_json(self, status, value, headers=()):
        self.cache = "no-store"
        self.send_bytes(status, json.dumps(value).encode(), "application/json", headers)

    def redirect(self, to):
        self.send_bytes(302, b"", "text/plain", [("Location", to)])

    def cookie(self):
        c = http.cookies.SimpleCookie()
        try:
            c.load(self.headers.get("Cookie", ""))
        except http.cookies.CookieError:
            return None
        return c[COOKIE].value if COOKIE in c else None

    def set_cookie(self, token, days):
        attrs = ["Path=/", "HttpOnly", "SameSite=Strict"]
        if self.app.cfg["server"]["secure_cookies"]:
            attrs.append("Secure")
        attrs.append(f"Max-Age={int(days * 86400)}" if token else "Max-Age=0")
        return ("Set-Cookie", f"{COOKIE}={token or ''}; " + "; ".join(attrs))

    def same_origin(self):
        site = self.headers.get("Sec-Fetch-Site")
        if site and site not in ("same-origin", "none"):
            return False
        origin = self.headers.get("Origin")
        return not origin or urlsplit(origin).netloc == self.headers.get("Host")

    def dispatch(self, method):
        self._body = None
        self.cache = "no-cache"
        url = urlsplit(self.path)
        path = unquote(url.path)
        self.query = {k: v[-1] for k, v in parse_qs(url.query).items()}
        self.user = self.app.accounts.session(self.cookie())
        try:
            if method in ("POST", "PUT", "DELETE") and not self.same_origin():
                raise HttpError(403, "not from this site")
            for m, pattern, fn, auth in ROUTES:
                match = pattern.fullmatch(path)
                if match and (m == method or (m == "GET" and method == "HEAD")):
                    if auth and not self.user:
                        raise HttpError(401, "sign in first")
                    fn(self, *match.groups())
                    break
            else:
                if method not in ("GET", "HEAD"):
                    raise HttpError(405, "no such method here")
                self.static(path)
        except (HttpError, SyncError) as e:
            self.send_json(e.status, {"error": str(e), **e.extra})
        except ValueError as e:
            self.send_json(400, {"error": str(e)})
        except (BrokenPipeError, ConnectionResetError):
            self.close_connection = True
            return
        # Keep-alive: the rest of a body no route read.
        if self._body is None and int(self.headers.get("Content-Length") or 0):
            self.close_connection = True

    def static(self, path):
        site = self.app.cfg["server"]["site"]
        norm = posixpath.normpath(path)
        if path.endswith("/"):
            norm = posixpath.join(norm, "index.html")
        if not self.user and not self.app.cfg["server"]["public"]:
            if norm.endswith(".html"):
                return self.redirect("/login?next=" + (path or "/"))
            raise HttpError(401, "sign in first")
        f = os.path.join(site, norm.lstrip("/"))
        if not site or not os.path.realpath(f).startswith(os.path.realpath(site) + os.sep):
            raise HttpError(404, "not found")
        if os.path.isdir(f):
            return self.redirect(path.rstrip("/") + "/")
        if not os.path.isfile(f):
            raise HttpError(404, "not found")
        static.send_file(self, f, head=self.command == "HEAD")


def page(name):
    def serve(h):
        with open(os.path.join(HERE, name), "rb") as f:
            h.send_bytes(200, f.read(), "text/html; charset=utf-8")
    return serve


route("GET", "/login", auth=False)(page("login.html"))
route("GET", "/account")(page("account.html"))


@route("GET", "/api/me", auth=False)
def me(h):
    if not h.user:
        raise HttpError(401, "signed out")
    h.send_json(200, {"name": h.user["name"], "owner": h.user["owner"]})


@route("POST", "/api/login", auth=False)
def login(h):
    address = h.client_address[0]
    if h.app.too_many_failures(address):
        raise HttpError(429, "too many attempts: try again later")
    b = h.json_body()
    user = h.app.accounts.check(str(b.get("name", "")), str(b.get("password", "")))
    if not user:
        h.app.note_failure(address)
        raise HttpError(401, "wrong name or password")
    days = h.app.cfg["server"]["session_days"]
    h.send_json(200, {"name": user["name"]}, [h.set_cookie(h.app.accounts.new_session(user["id"], days), days)])


@route("POST", "/api/logout", auth=False)
def logout(h):
    h.app.accounts.end_session(h.cookie())
    h.send_json(200, {}, [h.set_cookie(None, 0)])


@route("POST", "/api/register", auth=False)
def register(h):
    b = h.json_body()
    user = h.app.accounts.register(str(b.get("invite", "")), str(b.get("name", "")), str(b.get("password", "")))
    days = h.app.cfg["server"]["session_days"]
    h.send_json(200, {"name": user["name"]}, [h.set_cookie(h.app.accounts.new_session(user["id"], days), days)])


def owner_only(h):
    if not h.user["owner"]:
        raise HttpError(403, "the owner's")


@route("GET", "/api/invites")
def invites(h):
    owner_only(h)
    h.send_json(200, h.app.accounts.invites())


@route("POST", "/api/invites")
def new_invite(h):
    owner_only(h)
    h.send_json(200, {"code": h.app.accounts.new_invite(h.user["id"], int(h.json_body().get("days", 7)))})


# ---- the library ----
def lib_args(h):
    c = h.app.cfg["library"]
    return c["images"], c["include"], c["exclude"]


def library_auth(h):
    if not h.user and not h.app.cfg["server"]["public"]:
        raise HttpError(401, "sign in first")


@route("GET", "/library.json", auth=False)
def library_json(h):
    library_auth(h)
    h.send_json(200, [{k: d[k] for k in ("title", "url", "note", "tags", "size")}
                      for d in library.catalog(*lib_args(h))])


@route("GET", "/disks/(.+)", auth=False)
def disk(h, name):
    library_auth(h)
    path = library.find(*lib_args(h), name)
    if not path:
        raise HttpError(404, "not in the library")
    static.send_file(h, path, head=h.command == "HEAD")


# ---- sync ----
def user_sync(h):
    return h.app.sync.user(h.user["id"])


@route("GET", "/api/sync/?")
def sync_summary(h):
    h.send_json(200, user_sync(h).summary())


@route("GET", "/api/sync/overlays/([^/]+)")
def overlay_get(h, key):
    version, reset, count, rows = user_sync(h).overlay_get(key, int(h.query.get("since", 0)))
    h.cache = "no-store"
    h.send_response(200)
    h.send_header("Content-Type", "application/octet-stream")
    h.send_header("Content-Length", str(count * (4 + 4096)))
    h.send_header("X-Sync-Version", str(version))
    h.send_header("X-Sync-Reset", "1" if reset else "0")
    h.end_headers()
    if h.command == "HEAD":
        rows.close()
        return
    for r in rows:
        h.wfile.write(r)


@route("POST", "/api/sync/overlays/([^/]+)")
def overlay_post(h, key):
    size = h.query.get("size")
    version = user_sync(h).overlay_put(key, h.query.get("name"), int(size) if size else None, h.body())
    h.send_json(200, {"version": version})


@route("DELETE", "/api/sync/overlays/([^/]+)")
def overlay_delete(h, key):
    h.send_json(200, {"version": user_sync(h).overlay_delete(key)})


@route("GET", "/api/sync/volumes/([^/]+)")
def volume_get(h, key):
    version, manifest = user_sync(h).volume_get(key)
    h.send_json(200, {"version": version, "manifest": manifest})


@route("PUT", "/api/sync/volumes/([^/]+)")
def volume_put(h, key):
    h.send_json(200, {"version": user_sync(h).volume_put(key, h.json_body().get("manifest"))})


@route("DELETE", "/api/sync/volumes/([^/]+)")
def volume_delete(h, key):
    h.send_json(200, {"version": user_sync(h).volume_delete(key)})


@route("POST", "/api/sync/objects/missing")
def objects_missing(h):
    shas = h.json_body().get("shas", [])
    if not isinstance(shas, list):
        raise HttpError(400, "shas is a list")
    s = user_sync(h)
    for sha in shas:
        s.object_path(str(sha))         # checks it
    h.send_json(200, {"missing": s.objects_missing(set(map(str, shas)))})


@route("PUT", "/api/sync/objects/([0-9a-f]{64})")
def object_put(h, sha):
    user_sync(h).object_put(sha, h.body())
    h.send_bytes(204, b"")


@route("GET", "/api/sync/objects/([0-9a-f]{64})")
def object_get(h, sha):
    path = user_sync(h).object_path(sha)
    if not os.path.isfile(path):
        raise HttpError(404, "no such object")
    h.cache = "private, max-age=31536000, immutable"
    static.send_file(h, path, head=h.command == "HEAD")


# ---- the relay ----
@route("GET", "/relay")
def relay_ws(h):
    if not relay.upgrade(h):
        raise HttpError(400, "a WebSocket endpoint")
    h.close_connection = True
    relay.handle(h.connection, h.user)


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, cfg, quiet=False):
        self.app = App(cfg)
        self.quiet = quiet
        handler = type("BoundHandler", (Handler,), {"app": self.app})
        super().__init__((cfg["server"]["bind"], cfg["server"]["port"]), handler)
