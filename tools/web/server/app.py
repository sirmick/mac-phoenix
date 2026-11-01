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
        POST   overlays/KEY?stage=T   records kept for a commit with T
        POST   overlays/KEY?base=V[&stage=T][&replace=1][&name=&size=]
                                      records (and T's) as version V+1 -> { version };
                                      409 { version } when V isn't current
        DELETE overlays/KEY           Revert -> { version }
        GET    volumes/KEY            { version, manifest }
        PUT    volumes/KEY            { manifest, base } -> { version }
                                      (409 { version } or { missing })
        DELETE volumes/KEY            Reset -> { version }
        POST   objects/missing        { shas } -> { missing }
        PUT    objects/SHA, GET objects/SHA
        GET    (nothing after sync/)  what is kept: { overlays, volumes }
    /relay            WebSocket: the tab's sockets (relay.py, [relay])
    /api/log          POST ?session=ID, text: lines the tab printed (log.js),
                      appended to DATA/logs/DAY/ID.log (signed out too: 64 KB
                      a request; a file stops at 64 MB; kept 14 days)
    /api/shot         POST ?session=ID[&note=TEXT][&tag=1x], a PNG (16 MB): the tab's
                      Screenshot button; DATA/logs/DAY/ID-HHMMSS.png, noted in the log
    /api/logs         GET (owner) the logs and screenshots; /api/logs/DAY/NAME one

Everything but /login and its API needs a session (the cookie), unless
the configuration makes the page public; sync always does. Writes and
/relay from another site are refused (Origin against Host, and
Sec-Fetch-Site; the cookie is SameSite=Strict too). Behind a reverse
proxy: pass Host through (nginx: proxy_set_header Host $host), and name
the proxy in trusted_proxies so X-Forwarded-For gives the address the
sign-in limits count.

Limits: requests' bodies (64 KB signed out, max_upload_mb signed in), a
connection idle 60 s, MAX_CONNECTIONS at once, two password hashes at
a time (accounts.py), failed sign-ins per address and per name.
"""
import http.cookies
import http.server
import json
import os
import posixpath
import re
import shutil
import sys
import threading
import time
from urllib.parse import parse_qs, quote, unquote, urlsplit

from . import accounts as accounts_mod
from . import library, relay, static
from .sync import SyncError, SyncStore

HERE = os.path.dirname(os.path.abspath(__file__))
COOKIE = "executor_session"
LOGIN_FAILURES = 10                     # per address in LOGIN_WINDOW s, then 429
LOGIN_FAILURES_NAME = 20                # per name
LOGIN_WINDOW = 600
SIGNED_OUT_BODY = 64 << 10
MAX_CONNECTIONS = 64
IDLE = 60                               # s a connection may wait for a request
LOCAL = ("localhost", "127.0.0.1", "::1")


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
        self.ids = library.Ids(os.path.join(data, "image-ids.json"))
        self.failures = {}                  # ("address" | "name", value) -> times
        self.failures_lock = threading.Lock()
        self.relay = relay.Relay(cfg["relay"])
        self.log_lock = threading.Lock()

    def _recent(self, key, now):
        return [t for t in self.failures.get(key, []) if t > now - LOGIN_WINDOW]

    def note_failure(self, address, name):
        now = time.time()
        with self.failures_lock:
            for key in (("address", address), ("name", name)):
                self.failures[key] = self._recent(key, now) + [now]
            if len(self.failures) > 1000:
                self.failures = {k: v for k in list(self.failures) if (v := self._recent(k, now))}

    def too_many_failures(self, address, name):
        now = time.time()
        with self.failures_lock:
            return (len(self._recent(("address", address), now)) >= LOGIN_FAILURES
                    or len(self._recent(("name", name), now)) >= LOGIN_FAILURES_NAME)


ROUTES = []


def route(method, pattern, auth=True):
    def wrap(fn):
        ROUTES.append((method, re.compile(pattern), fn, auth))
        return fn
    return wrap


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "ExecutorWeb/1"
    timeout = IDLE
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

    def content_length(self):
        v = self.headers.get("Content-Length") or "0"
        if not v.isdigit():                 # negative, or not a number
            self.close_connection = True
            raise HttpError(400, "bad Content-Length")
        return int(v)

    def body(self):
        if self._body is None:
            n = self.content_length()
            limit = self.app.cfg["server"]["max_upload_mb"] << 20 if self.user else SIGNED_OUT_BODY
            if n > limit:
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

    def address(self):
        """The client's address: X-Forwarded-For's, through trusted proxies."""
        trusted = self.app.cfg["server"]["trusted_proxies"]
        addr = self.client_address[0]
        if addr in trusted:
            for hop in reversed([a.strip() for a in self.headers.get("X-Forwarded-For", "").split(",") if a.strip()]):
                addr = hop
                if hop not in trusted:
                    break
        return addr

    def secure(self):
        v = self.app.cfg["server"]["secure_cookies"]
        if v == "auto":                     # but on this machine (plain http)
            return urlsplit("//" + (self.headers.get("Host") or "")).hostname not in LOCAL
        return bool(v)

    def cookie(self):
        c = http.cookies.SimpleCookie()
        try:
            c.load(self.headers.get("Cookie", ""))
        except http.cookies.CookieError:
            return None
        return c[COOKIE].value if COOKIE in c else None

    def set_cookie(self, token, days):
        attrs = ["Path=/", "HttpOnly", "SameSite=Strict"]
        if self.secure():
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
            self.content_length()
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
        if self._body is None and self.headers.get("Content-Length", "0") != "0":
            self.close_connection = True

    def static(self, path):
        site = self.app.cfg["server"]["site"]
        norm = posixpath.normpath(path)
        if path.endswith("/"):
            norm = posixpath.join(norm, "index.html")
        if not self.user and not self.app.cfg["server"]["public"]:
            if norm.endswith(".html"):
                return self.redirect("/login?next=" + quote(path or "/", safe=""))
            raise HttpError(401, "sign in first")
        f = os.path.join(site, norm.lstrip("/"))
        if not site or not os.path.realpath(f).startswith(os.path.realpath(site) + os.sep):
            raise HttpError(404, "not found")
        if os.path.isdir(f):
            return self.redirect(quote(path.rstrip("/") + "/"))
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
    address = h.address()
    b = h.json_body()
    name = str(b.get("name", ""))
    if h.app.too_many_failures(address, name):
        raise HttpError(429, "too many attempts: try again later")
    user = h.app.accounts.check(name, str(b.get("password", "")))
    if not user:
        h.app.note_failure(address, name)
        raise HttpError(401, "wrong name or password")
    days = h.app.cfg["server"]["session_days"]
    h.send_json(200, {"name": user["name"]}, [h.set_cookie(h.app.accounts.new_session(user["id"], days), days)])


@route("POST", "/api/logout", auth=False)
def logout(h):
    h.app.accounts.end_session(h.cookie())
    h.send_json(200, {}, [h.set_cookie(None, 0)])


@route("POST", "/api/register", auth=False)
def register(h):
    address = h.address()
    if h.app.too_many_failures(address, ""):
        raise HttpError(429, "too many attempts: try again later")
    b = h.json_body()
    if not h.app.accounts.invite_valid(str(b.get("invite", ""))):
        h.app.note_failure(address, "")
        raise HttpError(400, "that invitation is not valid (used, expired or mistyped)")
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
    ident = h.app.ids.get(path)
    static.send_file(h, path, head=h.command == "HEAD", tag=ident, headers=[("X-Image-Id", ident)])


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
    s = user_sync(h)
    if "base" not in h.query:
        if "stage" not in h.query:
            raise HttpError(400, "base (the version this is on) or stage")
        s.overlay_stage(key, h.query["stage"], h.body())
        return h.send_bytes(204, b"")
    size = h.query.get("size")
    version = s.overlay_put(key, int(h.query["base"]), h.body(), name=h.query.get("name"),
                            size=int(size) if size else None, token=h.query.get("stage"),
                            replace=h.query.get("replace") == "1")
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
    b = h.json_body()
    if not isinstance(b.get("base"), int):
        raise HttpError(400, "base: the version this is on")
    h.send_json(200, {"version": user_sync(h).volume_put(key, b.get("manifest"), b["base"])})


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


# ---- the tab's logs ----
# The page and its Worker send what they print (Executor's stderr included)
# and their crashes, a batch a second: a tab whose renderer dies says
# nothing at the end, so what reached the server is what there is.
LOG_SESSION = re.compile(r"[A-Za-z0-9-]{8,40}")
LOG_FILE_MAX = 64 << 20                 # a session's file stops growing here
LOG_KEEP_DAYS = 14


def log_dir(h):
    return os.path.join(h.app.cfg["server"]["data"], "logs")


def log_prune(root):
    cutoff = time.strftime("%Y-%m-%d", time.gmtime(time.time() - LOG_KEEP_DAYS * 86400))
    for day in os.listdir(root):
        if day < cutoff:
            shutil.rmtree(os.path.join(root, day), ignore_errors=True)


@route("POST", "/api/log", auth=False)
def log_post(h):
    session = h.query.get("session", "")
    if not LOG_SESSION.fullmatch(session):
        raise HttpError(400, "session: 8-40 letters, digits, -")
    text = h.body().decode("utf-8", "replace")
    root = log_dir(h)
    day = time.strftime("%Y-%m-%d", time.gmtime())
    os.makedirs(os.path.join(root, day), exist_ok=True)
    path = os.path.join(root, day, session + ".log")
    with h.app.log_lock:
        new = not os.path.exists(path)
        if new:
            log_prune(root)
        elif os.path.getsize(path) > LOG_FILE_MAX:
            return h.send_json(200, {"full": True})
        with open(path, "a", encoding="utf-8") as f:
            if new:
                f.write(f"# {time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime())}Z {h.address()} "
                        f"{h.user['name'] if h.user else '(signed out)'} "
                        f"{h.headers.get('User-Agent', '')}\n")
            f.write(text if text.endswith("\n") else text + "\n")
    h.send_json(200, {})


# A screenshot from the tab's button (main.js): a PNG beside the session's
# log, DAY/SESSION-HHMMSS.png, and a line in the log saying so.
SHOT_MAX = 16 << 20


@route("POST", "/api/shot", auth=False)
def shot_post(h):
    session = h.query.get("session", "")
    if not LOG_SESSION.fullmatch(session):
        raise HttpError(400, "session: 8-40 letters, digits, -")
    n = h.content_length()
    if n > SHOT_MAX:
        h.close_connection = True
        raise HttpError(413, "too large")
    h._body = data = h.rfile.read(n)
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise HttpError(400, "a PNG")
    note = h.query.get("note", "").replace("\n", " ")[:500]
    tag = h.query.get("tag", "")
    if not re.fullmatch(r"[a-z0-9]{0,8}", tag):
        raise HttpError(400, "tag: up to 8 letters and digits")
    tag = "-" + tag if tag else ""
    day = time.strftime("%Y-%m-%d", time.gmtime())
    os.makedirs(os.path.join(log_dir(h), day), exist_ok=True)
    with h.app.log_lock:
        stamp = time.strftime("%H%M%S", time.gmtime())
        name, i = f"{session}-{stamp}{tag}.png", 1
        while os.path.exists(os.path.join(log_dir(h), day, name)):
            i += 1
            name = f"{session}-{stamp}-{i}{tag}.png"
        with open(os.path.join(log_dir(h), day, name), "wb") as f:
            f.write(data)
        with open(os.path.join(log_dir(h), day, session + ".log"), "a", encoding="utf-8") as f:
            f.write(f"# screenshot {day}/{name}{': ' + note if note else ''}\n")
    h.send_json(200, {"name": f"{day}/{name}"})


@route("GET", "/api/logs")
def logs_list(h):
    owner_only(h)
    root, out = log_dir(h), []
    for day in sorted(os.listdir(root) if os.path.isdir(root) else [], reverse=True):
        for name in sorted(os.listdir(os.path.join(root, day))):
            st = os.stat(os.path.join(root, day, name))
            out.append({"log": f"{day}/{name}", "size": st.st_size, "mtime": int(st.st_mtime)})
    h.send_json(200, out)


@route("GET", r"/api/logs/(\d{4}-\d\d-\d\d)/([A-Za-z0-9-]{8,60}\.(?:log|png))")
def logs_get(h, day, name):
    owner_only(h)
    path = os.path.join(log_dir(h), day, name)
    if not os.path.isfile(path):
        raise HttpError(404, "no such log")
    h.cache = "no-store"
    static.send_file(h, path, head=h.command == "HEAD")


# ---- the relay ----
@route("GET", "/relay")
def relay_ws(h):
    if not h.same_origin():
        raise HttpError(403, "not from this site")
    if not relay.upgrade(h):
        raise HttpError(400, "a WebSocket endpoint")
    h.close_connection = True
    h.app.relay.handle(h.connection, h.user)


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, cfg, quiet=False):
        self.app = App(cfg)
        self.quiet = quiet
        self.slots = threading.BoundedSemaphore(MAX_CONNECTIONS)
        handler = type("BoundHandler", (Handler,), {"app": self.app})
        super().__init__((cfg["server"]["bind"], cfg["server"]["port"]), handler)
        srv = cfg["server"]
        if srv.get("tls_cert"):
            import ssl
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ctx.load_cert_chain(os.path.expanduser(srv["tls_cert"]),
                                os.path.expanduser(srv.get("tls_key") or srv["tls_cert"]))
            # The handshake happens in the connection's thread (first read), not in accept().
            self.socket = ctx.wrap_socket(self.socket, server_side=True,
                                          do_handshake_on_connect=False)
        c = cfg["library"]
        self.app.ids.warm([os.path.join(c["images"], d["name"])
                           for d in library.catalog(c["images"], c["include"], c["exclude"])])

    # A thread a connection, MAX_CONNECTIONS at most: the rest are closed.
    def process_request(self, request, client_address):
        if not self.slots.acquire(blocking=False):
            self.shutdown_request(request)
            return
        super().process_request(request, client_address)

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.slots.release()

    # A client that drops the connection, or fails the TLS handshake (plain
    # http to the HTTPS port, a certificate it refuses): a line, not a traceback.
    def handle_error(self, request, client_address):
        import ssl
        e = sys.exc_info()[1]
        if isinstance(e, (ssl.SSLError, ConnectionError, TimeoutError)):
            if not self.quiet:
                print(f"{client_address[0]}: {type(e).__name__}: {e}", file=sys.stderr, flush=True)
            return
        super().handle_error(request, client_address)
