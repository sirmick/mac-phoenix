#!/usr/bin/env python3
"""tools/web/server's own checks: ranges, accounts, the library, sync, the
relay (ctest executor.web.server; no browser needed).

    python3 tools/web/server/test_server.py
    EXECUTOR_WEB_SERVER_BINARY=PATH python3 tools/web/server/test_server.py

With EXECUTOR_WEB_SERVER_BINARY, the same checks run against that
program instead (tools/standalone's Go server, started for each class):
it has no accounts, its one password (--password-file) stands for the
owner's, and the checks of accounts and their backups are skipped.
"""
import base64
import contextlib
import hashlib
import http.client
import json
import os
import re
import shutil
import socket
import sqlite3
import struct
import subprocess
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from server import app, backup, config, relay, static  # noqa: E402

BINARY = os.environ.get("EXECUTOR_WEB_SERVER_BINARY")
accounts_only = unittest.skipIf(BINARY, "accounts: the Python server's")


class Binary:
    """The server under EXECUTOR_WEB_SERVER_BINARY as a child process, with
    the owner's password; server_address and shutdown as app.Server's."""
    def __init__(self, tmp, site, images, *extra):
        pw = os.path.join(tmp, "password")
        with open(pw, "w") as f:
            f.write("owner-password\n")
        self.proc = subprocess.Popen([BINARY, "--data", os.path.join(tmp, "data"), "--images", images,
                                      "--site", site, "--bind", "127.0.0.1", "--port", "0",
                                      "--password-file", pw, "--quiet", *extra],
                                     stdout=subprocess.PIPE, text=True)
        line = self.proc.stdout.readline()
        m = re.search(r":(\d+)/$", line.strip())
        if not m:
            self.proc.kill()
            raise RuntimeError(f"{BINARY}: {line!r}")
        self.server_address = ("127.0.0.1", int(m.group(1)))

    def shutdown(self):
        self.proc.terminate()
        self.proc.wait()


class ServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="executor-web-test-")
        site = os.path.join(cls.tmp, "site")
        images = os.path.join(cls.tmp, "images")
        os.makedirs(site)
        os.makedirs(images)
        with open(os.path.join(site, "index.html"), "w") as f:
            f.write("<p>tab</p>")
        cls.disk = bytes(range(256)) * 4096          # 1 MB
        with open(os.path.join(images, "a.img"), "wb") as f:
            f.write(cls.disk)
        with open(os.path.join(images, "a.img.json"), "w") as f:
            json.dump({"title": "Disk A", "note": "a note", "tags": ["68k"]}, f)
        with open(os.path.join(images, "secret.img"), "wb") as f:
            f.write(b"x")                             # no record: not offered
        if BINARY:
            cls.server = Binary(cls.tmp, site, images)
            cls.port = cls.server.server_address[1]
            return
        cfg = config.load(None, {"server": {"port": 0, "site": site, "data": os.path.join(cls.tmp, "data")},
                                 "library": {"images": images}})
        cls.server = app.Server(cfg, quiet=True)
        cls.port = cls.server.server_address[1]
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        accounts = cls.server.app.accounts
        owner = accounts.add_user("owner", "owner-password", owner=True)
        cls.invite = accounts.new_invite(owner["id"])

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        shutil.rmtree(cls.tmp)

    def request(self, method, path, body=None, headers=None, cookie=None):
        c = http.client.HTTPConnection("127.0.0.1", self.port)
        h = dict(headers or {})
        if cookie:
            h["Cookie"] = cookie
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
            h["Content-Type"] = "application/json"
        c.request(method, path, body, h)
        r = c.getresponse()
        data = r.read()
        c.close()
        return r, data

    def login(self, name="owner", password="owner-password"):
        r, _ = self.request("POST", "/api/login", {"name": name, "password": password})
        self.assertEqual(r.status, 200)
        return r.getheader("Set-Cookie").split(";")[0]

    @unittest.skipIf(BINARY, "static.py's own (the Go server's: go test)")
    def test_range_parsing(self):
        self.assertEqual(static.parse_range("bytes=0-99", 1000), (0, 100))
        self.assertEqual(static.parse_range("bytes=900-", 1000), (900, 1000))
        self.assertEqual(static.parse_range("bytes=-10", 1000), (990, 1000))
        self.assertEqual(static.parse_range("bytes=990-5000", 1000), (990, 1000))
        self.assertEqual(static.parse_range("bytes=1000-", 1000), "unsatisfiable")
        self.assertIsNone(static.parse_range("bytes=0-1,5-6", 1000))
        self.assertIsNone(static.parse_range("bytes=5-3", 1000))

    def test_signed_out(self):
        r, _ = self.request("GET", "/")
        self.assertEqual(r.status, 302)
        self.assertEqual(r.getheader("Location"), "/login?next=%2F")
        for path in ("/main.js", "/library.json", "/disks/a.img", "/api/sync", "/api/me"):
            self.assertEqual(self.request("GET", path)[0].status, 401, path)
        self.assertEqual(self.request("GET", "/login")[0].status, 200)
        r, _ = self.request("POST", "/api/login", {"name": "owner", "password": "wrong-password"})
        self.assertEqual(r.status, 401)

    def test_page_and_library(self):
        cookie = self.login()
        r, data = self.request("GET", "/", cookie=cookie)
        self.assertEqual((r.status, data), (200, b"<p>tab</p>"))
        self.assertEqual(self.request("GET", "/../data/accounts.sqlite", cookie=cookie)[0].status, 404)
        r, data = self.request("GET", "/library.json", cookie=cookie)
        lib = json.loads(data)
        self.assertEqual([d["title"] for d in lib], ["Disk A"])
        self.assertEqual(lib[0]["url"], "disks/a.img")
        self.assertEqual(lib[0]["size"], len(self.disk))
        self.assertEqual(self.request("GET", "/disks/secret.img", cookie=cookie)[0].status, 404)

    def test_logs(self):
        r, _ = self.request("POST", "/api/log?session=abc", b"x")
        self.assertEqual(r.status, 400)
        r, _ = self.request("POST", "/api/log?session=k1x-0011223344556677", b"one\ntwo")
        self.assertEqual(r.status, 200)                  # signed out too
        cookie = self.login()
        r, _ = self.request("POST", "/api/log?session=k1x-0011223344556677", b"three\n", cookie=cookie)
        self.assertEqual(r.status, 200)
        self.assertEqual(self.request("GET", "/api/logs")[0].status, 401)
        r, data = self.request("GET", "/api/logs", cookie=cookie)
        logs = json.loads(data)
        self.assertEqual([l["log"].split("/")[1] for l in logs], ["k1x-0011223344556677.log"])
        r, data = self.request("GET", "/api/logs/" + logs[0]["log"], cookie=cookie)
        lines = data.decode().splitlines()
        self.assertTrue(lines[0].startswith("# ") and "(signed out)" in lines[0])
        self.assertEqual(lines[1:], ["one", "two", "three"])
        self.assertEqual(self.request("GET", "/api/logs/2000-01-01/../../accounts.sqlite", cookie=cookie)[0].status, 404)
        self.assertEqual(self.request("POST", "/api/shot?session=k1x-0011223344556677", b"GIF89a")[0].status, 400)
        png = b"\x89PNG\r\n\x1a\n" + bytes(100000)  # over the signed-out body limit
        r, data = self.request("POST", "/api/shot?session=k1x-0011223344556677&note=the%20menu", png)
        self.assertEqual(r.status, 200)
        name = json.loads(data)["name"]
        self.assertEqual(self.request("GET", "/api/logs/" + name, cookie=cookie)[1], png)
        r, data = self.request("GET", "/api/logs/" + logs[0]["log"], cookie=cookie)
        self.assertEqual(data.decode().splitlines()[-1], f"# screenshot {name}: the menu")

    def test_disk_ranges(self):
        cookie = self.login()
        r, data = self.request("GET", "/disks/a.img", headers={"Range": "bytes=4096-8191"}, cookie=cookie)
        self.assertEqual(r.status, 206)
        self.assertEqual(r.getheader("Content-Range"), f"bytes 4096-8191/{len(self.disk)}")
        self.assertEqual(data, self.disk[4096:8192])
        etag = r.getheader("ETag")
        ident = hashlib.sha256(self.disk).hexdigest()[:24]
        self.assertEqual((etag, r.getheader("X-Image-Id")), (f'"{ident}"', ident))
        r, data = self.request("GET", "/disks/a.img", headers={"Range": "bytes=0-9", "If-Range": '"other"'},
                               cookie=cookie)
        self.assertEqual((r.status, len(data)), (200, len(self.disk)))
        r, _ = self.request("GET", "/disks/a.img", headers={"Range": "bytes=2000000-"}, cookie=cookie)
        self.assertEqual(r.status, 416)
        r, data = self.request("HEAD", "/disks/a.img", cookie=cookie)
        self.assertEqual((r.status, r.getheader("Accept-Ranges"), data), (200, "bytes", b""))
        r, _ = self.request("GET", "/disks/a.img", headers={"If-None-Match": etag}, cookie=cookie)
        self.assertEqual(r.status, 304)

    def test_cookie_and_cross_site(self):
        r, _ = self.request("POST", "/api/login", {"name": "owner", "password": "owner-password"})
        cookie = r.getheader("Set-Cookie").split(";")[0]
        self.assertIn("HttpOnly", r.getheader("Set-Cookie"))
        self.assertIn("SameSite=Strict", r.getheader("Set-Cookie"))
        self.assertNotIn("Secure", r.getheader("Set-Cookie"))           # http://127.0.0.1
        r, _ = self.request("POST", "/api/login", {"name": "owner", "password": "owner-password"},
                            headers={"Host": "executor.example"})
        self.assertIn("; Secure", r.getheader("Set-Cookie"))           # anywhere else
        self.assertEqual(self.request("GET", "/api/me", cookie=cookie)[0].status, 200)
        r, _ = self.request("DELETE", "/api/sync/volumes/own", cookie=cookie,
                            headers={"Origin": "https://elsewhere.example"})
        self.assertEqual(r.status, 403)
        r, _ = self.request("POST", "/api/logout", cookie=cookie)
        self.assertEqual(self.request("GET", "/api/me", cookie=cookie)[0].status, 401)

    @accounts_only
    def test_invite_and_cross_site(self):
        r, _ = self.request("POST", "/api/register", {"invite": "nonsense", "name": "bob", "password": "bob-password"})
        self.assertEqual(r.status, 400)
        r, _ = self.request("POST", "/api/register", {"invite": self.invite, "name": "carol",
                                                      "password": "carol-password"})
        self.assertEqual(r.status, 200)
        cookie = r.getheader("Set-Cookie").split(";")[0]
        self.assertIn("HttpOnly", r.getheader("Set-Cookie"))
        self.assertIn("SameSite=Strict", r.getheader("Set-Cookie"))
        self.assertNotIn("Secure", r.getheader("Set-Cookie"))           # http://127.0.0.1
        r, _ = self.request("POST", "/api/login", {"name": "carol", "password": "carol-password"},
                            headers={"Host": "executor.example"})
        self.assertIn("; Secure", r.getheader("Set-Cookie"))           # anywhere else
        r, data = self.request("GET", "/api/me", cookie=cookie)
        self.assertEqual(json.loads(data), {"name": "carol", "owner": False})
        self.assertEqual(self.request("POST", "/api/invites", {}, cookie=cookie)[0].status, 403)
        r, _ = self.request("POST", "/api/register", {"invite": self.invite, "name": "dave",
                                                      "password": "dave-password"})
        self.assertEqual(r.status, 400)                     # used
        r, _ = self.request("DELETE", "/api/sync/volumes/own", cookie=cookie,
                            headers={"Origin": "https://elsewhere.example"})
        self.assertEqual(r.status, 403)
        r, _ = self.request("POST", "/api/logout", cookie=cookie)
        self.assertEqual(self.request("GET", "/api/me", cookie=cookie)[0].status, 401)

    def test_overlay_sync(self):
        cookie = self.login()
        rec = lambda b, byte: struct.pack("<I", b) + bytes([byte]) * 4096
        post = lambda q, body: self.request("POST", f"/api/sync/overlays/a.img-x?{q}", body, cookie=cookie)
        self.assertEqual(post("", rec(0, 1))[0].status, 400)                    # no base
        r, data = post("base=0&name=a.img&size=1048576", rec(0, 1) + rec(7, 2))
        v1 = json.loads(data)["version"]
        self.assertEqual(v1, 1)
        r, data = post("base=0", rec(7, 9))                                      # another browser, behind
        self.assertEqual((r.status, json.loads(data)["version"]), (409, v1))
        r, data = post(f"base={v1}", rec(7, 3) + rec(9, 4))
        v2 = json.loads(data)["version"]
        r, data = self.request("GET", "/api/sync/overlays/a.img-x?since=0", cookie=cookie)
        self.assertEqual(r.getheader("X-Sync-Version"), str(v2))
        got = {struct.unpack_from("<I", data, i)[0]: data[i + 4] for i in range(0, len(data), 4100)}
        self.assertEqual(got, {0: 1, 7: 3, 9: 4})
        r, data = self.request("GET", f"/api/sync/overlays/a.img-x?since={v1}", cookie=cookie)
        self.assertEqual(len(data), 2 * 4100)               # 7 and 9
        self.assertEqual(post(f"base={v2}", b"short")[0].status, 400)
        # A replace, staged: nothing shows until the commit, then only its blocks.
        self.assertEqual(post("stage=token-1234", rec(1, 5))[0].status, 204)
        r, data = self.request("GET", f"/api/sync/overlays/a.img-x?since={v2}", cookie=cookie)
        self.assertEqual(data, b"")
        r, data = post(f"base={v2}&stage=token-1234&replace=1", rec(2, 6))
        v3 = json.loads(data)["version"]
        r, data = self.request("GET", f"/api/sync/overlays/a.img-x?since={v2}", cookie=cookie)
        self.assertEqual(r.getheader("X-Sync-Reset"), "1")
        got = {struct.unpack_from("<I", data, i)[0]: data[i + 4] for i in range(0, len(data), 4100)}
        self.assertEqual(got, {1: 5, 2: 6})
        r, data = self.request("DELETE", "/api/sync/overlays/a.img-x", cookie=cookie)
        v4 = json.loads(data)["version"]
        r, data = self.request("GET", f"/api/sync/overlays/a.img-x?since={v3}", cookie=cookie)
        self.assertEqual((r.getheader("X-Sync-Reset"), data), ("1", b""))
        r, data = self.request("GET", f"/api/sync/overlays/a.img-x?since={v4}", cookie=cookie)
        self.assertEqual(r.getheader("X-Sync-Reset"), "0")

    def test_volume_sync(self):
        cookie = self.login()
        blob = b"kept across browsers"
        sha = hashlib.sha256(blob).hexdigest()
        manifest = {"dirs": ["/executor"], "files": {"/executor/note": {"size": len(blob), "mtime": 5, "sha": sha}}}
        self.assertEqual(self.request("PUT", "/api/sync/volumes/own", {"manifest": manifest},
                                      cookie=cookie)[0].status, 400)            # no base
        r, data = self.request("PUT", "/api/sync/volumes/own", {"manifest": manifest, "base": 0}, cookie=cookie)
        self.assertEqual((r.status, json.loads(data)["missing"]), (409, [sha]))
        r, data = self.request("POST", "/api/sync/objects/missing", {"shas": [sha]}, cookie=cookie)
        self.assertEqual(json.loads(data)["missing"], [sha])
        self.assertEqual(self.request("PUT", f"/api/sync/objects/{sha}", b"other", cookie=cookie)[0].status, 400)
        self.assertEqual(self.request("PUT", f"/api/sync/objects/{sha}", blob, cookie=cookie)[0].status, 204)
        r, data = self.request("PUT", "/api/sync/volumes/own", {"manifest": manifest, "base": 0}, cookie=cookie)
        self.assertEqual(r.status, 200)
        version = json.loads(data)["version"]
        r, data = self.request("PUT", "/api/sync/volumes/own", {"manifest": manifest, "base": 0}, cookie=cookie)
        self.assertEqual((r.status, json.loads(data)["version"]), (409, version))   # behind
        r, data = self.request("GET", "/api/sync/volumes/own", cookie=cookie)
        self.assertEqual(json.loads(data), {"version": version, "manifest": manifest})
        self.assertEqual(self.request("GET", f"/api/sync/objects/{sha}", cookie=cookie)[1], blob)
        if BINARY:                                      # no one else
            self.request("DELETE", "/api/sync/volumes/own", cookie=cookie)
            r, data = self.request("GET", "/api/sync/volumes/own", cookie=cookie)
            self.assertEqual(json.loads(data), {"version": version + 1, "manifest": None})
            return
        # Someone else's are not ours.
        r, _ = self.request("POST", "/api/register", {"invite": self.server.app.accounts.new_invite(1),
                                                      "name": "erin", "password": "erin-password"})
        other = r.getheader("Set-Cookie").split(";")[0]
        r, data = self.request("GET", "/api/sync/volumes/own", cookie=other)
        self.assertEqual(json.loads(data), {"version": 0, "manifest": None})
        self.assertEqual(self.request("GET", f"/api/sync/objects/{sha}", cookie=other)[0].status, 404)
        r, data = self.request("DELETE", "/api/sync/volumes/own", cookie=cookie)
        r, data = self.request("GET", "/api/sync/volumes/own", cookie=cookie)
        self.assertEqual(json.loads(data), {"version": version + 1, "manifest": None})

    def raw(self, text):
        s = socket.create_connection(("127.0.0.1", self.port))
        s.sendall(text.encode("latin1"))
        data = b""
        while chunk := s.recv(65536):
            data += chunk
        s.close()
        return data

    def test_hostile_requests(self):
        # A negative or bad length: 400, the connection closed (not a hang or a read of everything).
        for length in ("-1", "abc"):
            data = self.raw(f"POST /api/login HTTP/1.1\r\nHost: x\r\nContent-Length: {length}\r\n\r\n")
            self.assertIn(b" 400 ", data.split(b"\r\n")[0])
        # Signed out, a large body is refused before it is read.
        r, _ = self.request("POST", "/api/login", b"x" * (100 << 10), headers={"Content-Type": "application/json"})
        self.assertEqual(r.status, 413)
        # CR/LF in the path can't make headers.
        data = self.raw("GET /a%0d%0aSet-Cookie:%20evil=1.html HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
        head = data.split(b"\r\n\r\n")[0]
        self.assertNotIn(b"\r\nSet-Cookie: evil", head)
        self.assertIn(b"Location: /login?next=%2Fa%0D%0ASet-Cookie", head)
        # ../ however spelled stays in the site.
        cookie = self.login()
        for path in ("/%2e%2e/data/accounts.sqlite", "/..%2fdata/accounts.sqlite", "/%2e%2e%2f%2e%2e/etc/passwd"):
            self.assertEqual(self.request("GET", path, cookie=cookie)[0].status, 404, path)
        # The relay, from another site.
        r, _ = self.request("GET", "/relay", headers={"Origin": "https://elsewhere.example", "Upgrade": "websocket",
                                                       "Sec-WebSocket-Key": "x"}, cookie=cookie)
        self.assertEqual(r.status, 403)

    def trust(self, proxies):
        if BINARY:
            r, _ = self.request("POST", "/api/settings", {"trusted_proxies": proxies}, cookie=self.login())
            self.assertEqual(r.status, 200)
        else:
            self.server.app.cfg["server"]["trusted_proxies"] = proxies

    def test_login_limits(self):
        self.trust(["127.0.0.1"])
        try:
            for i in range(10):                         # one client behind the proxy
                self.request("POST", "/api/login", {"name": "nobody", "password": "wrong-password"},
                             headers={"X-Forwarded-For": "203.0.113.5"})
            r, _ = self.request("POST", "/api/login", {"name": "owner", "password": "owner-password"},
                                headers={"X-Forwarded-For": "203.0.113.5"})
            self.assertEqual(r.status, 429)
            r, _ = self.request("POST", "/api/login", {"name": "owner", "password": "owner-password"},
                                headers={"X-Forwarded-For": "203.0.113.6"})
            self.assertEqual(r.status, 200)              # another one is not
        finally:
            self.trust([])
            if not BINARY:
                self.server.app.failures.clear()

    # ---- the relay ----
    def relay_open(self, origin=None):
        """A WebSocket on /relay; its first message (HELLO) checked."""
        cookie = self.login()
        s = socket.create_connection(("127.0.0.1", self.port), timeout=10)
        key = base64.b64encode(os.urandom(16)).decode()
        extra = f"Origin: {origin}\r\n" if origin else ""
        s.sendall((f"GET /relay HTTP/1.1\r\nHost: 127.0.0.1:{self.port}\r\nUpgrade: websocket\r\n"
                   f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n"
                   f"{extra}Cookie: {cookie}\r\n\r\n").encode())
        head = b""
        while b"\r\n\r\n" not in head:
            head += s.recv(1)
        self.assertIn(b"101", head.split(b"\r\n")[0])
        accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
        self.assertIn(b"Sec-WebSocket-Accept: " + accept, head)
        ws = WS(s)
        op, id, body = ws.recv()
        self.assertEqual((op, id), (relay.HELLO, 0))
        self.assertEqual(struct.unpack(">H", body[:2])[0], relay.VERSION)
        return ws

    def relay_policy(self, **cfg):
        c = dict(config.DEFAULTS["relay"], ports=["1-65535"], allow_private=True)
        c.update(cfg)
        if BINARY:
            r, _ = self.request("POST", "/api/settings", {"relay": c}, cookie=self.login())
            self.assertEqual(r.status, 200)
        else:
            self.server.app.relay = relay.Relay(c)

    def relay_sockets(self):
        """Sockets the relay has open now."""
        if BINARY:
            return json.loads(self.request("GET", "/api/relay", cookie=self.login())[1])["sockets"]
        return self.server.app.relay.sockets.get(1)

    def test_relay_tcp(self):
        self.relay_policy()
        echo = Echo()
        ws = self.relay_open(origin=f"http://127.0.0.1:{self.port}")
        ws.send(relay.CONNECT, 7, struct.pack(">IH", 0x7F000001, echo.port))
        self.assertEqual(ws.recv()[:2], (relay.CONNECTED, 7))
        ws.send(relay.SEND, 7, b"hello relay")
        got, acked = b"", 0
        while len(got) < 11 or acked < 11:
            op, id, body = ws.recv()
            self.assertEqual(id, 7)
            if op == relay.ACK:
                acked += struct.unpack(">I", body)[0]
            else:
                self.assertEqual(op, relay.DATA)
                got += body
        self.assertEqual(got, b"hello relay")
        ws.send(relay.CREDIT, 7, struct.pack(">I", len(got)))
        # More than a window, credited as it comes: flow control both ways.
        big = os.urandom(3 * relay.WINDOW)
        sent, got, acked = 0, b"", 0
        while len(got) < len(big):
            if sent < len(big) and sent - acked < relay.WINDOW:
                n = min(relay.WINDOW - (sent - acked), 16384, len(big) - sent)
                ws.send(relay.SEND, 7, big[sent:sent + n])
                sent += n
                continue
            op, id, body = ws.recv()
            if op == relay.ACK:
                acked += struct.unpack(">I", body)[0]
            elif op == relay.DATA:
                got += body
                ws.send(relay.CREDIT, 7, struct.pack(">I", len(body)))
        self.assertEqual(got, big)
        # Our FIN: the echo server closes, EOF.
        ws.send(relay.SHUTDOWN, 7)
        while (op := ws.recv()[0]) == relay.ACK:
            pass
        self.assertEqual(op, relay.EOF)
        ws.send(relay.CLOSE, 7, b"\0")
        # Refused, denied (a port off the list, a private host).
        free = socket.socket()
        free.bind(("127.0.0.1", 0))
        port = free.getsockname()[1]
        free.close()
        ws.send(relay.CONNECT, 8, struct.pack(">IH", 0x7F000001, port))
        self.assertEqual(ws.recv(), (relay.ERROR, 8, bytes([relay.REFUSED])))
        ws.send(relay.CLOSE, 8, b"\0")
        ws.close()
        echo.stop()
        self.relay_policy(ports=[80, "8000-8001"], allow_private=False)
        ws = self.relay_open()
        ws.send(relay.CONNECT, 1, struct.pack(">IH", 0x7F000001, 80))         # loopback
        self.assertEqual(ws.recv(), (relay.ERROR, 1, bytes([relay.DENIED])))
        ws.send(relay.CONNECT, 2, struct.pack(">IH", 0xC0A80001, 8001))       # 192.168.0.1
        self.assertEqual(ws.recv(), (relay.ERROR, 2, bytes([relay.DENIED])))
        ws.send(relay.CONNECT, 3, struct.pack(">IH", 0x08080808, 25))         # a port off the list
        self.assertEqual(ws.recv(), (relay.ERROR, 3, bytes([relay.DENIED])))
        ws.close()

    def test_relay_udp_dns_limits(self):
        self.relay_policy(max_sockets=2, max_connections=1)
        echo = Echo()
        ws = self.relay_open()
        ws.send(relay.UDP, 1)
        ws.send(relay.SENDTO, 1, struct.pack(">IH", 0x7F000001, echo.port) + b"a datagram")
        self.assertEqual(ws.recv(), (relay.DATAFROM, 1, struct.pack(">IH", 0x7F000001, echo.port) + b"a datagram"))
        ws.send(relay.LOOKUP, 5, b"localhost")
        op, id, body = ws.recv()
        self.assertEqual((op, id, body[0]), (relay.ADDRS, 5, 0))
        self.assertIn(struct.pack(">I", 0x7F000001), [body[i:i + 4] for i in range(1, len(body), 4)])
        ws.send(relay.LOOKUP, 6, b"no-such-host.invalid")
        self.assertEqual(ws.recv()[:2], (relay.ADDRS, 6))
        # Two sockets per user: a third is refused; a second WebSocket too.
        ws.send(relay.UDP, 2)
        ws.send(relay.UDP, 3)
        self.assertEqual(ws.recv(), (relay.ERROR, 3, bytes([relay.TOO_MANY])))
        s = socket.create_connection(("127.0.0.1", self.port), timeout=10)
        cookie = self.login()
        s.sendall((f"GET /relay HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                   f"Sec-WebSocket-Key: eA==\r\nCookie: {cookie}\r\n\r\n").encode())
        data = b""
        while not data.endswith(b"too many relay connections"):
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
        self.assertIn(b"\x88", data)
        s.close()
        # A frame without a mask ends the connection (1002); its sockets go.
        ws.s.sendall(bytes([0x82, 5]) + struct.pack(">BI", relay.CLOSE, 1))
        op, _, body = ws.recv_frame()
        self.assertEqual((op, struct.unpack(">H", body[:2])[0]), (0x8, 1002))
        ws.s.close()
        echo.stop()
        for _ in range(50):
            if not self.relay_sockets():
                break
            threading.Event().wait(0.1)
        self.assertFalse(self.relay_sockets())

    def test_relay_short_credit(self):
        self.relay_policy()
        ws = self.relay_open()
        ws.send(relay.UDP, 1)
        ws.send(relay.CREDIT, 1, b"\0\0")                # a u32 cut short
        while (frame := ws.recv_frame())[0] != 0x8:
            pass
        self.assertEqual(struct.unpack(">H", frame[2][:2])[0], 1008)
        ws.s.close()

    def test_relay_signed_out(self):
        s = socket.create_connection(("127.0.0.1", self.port), timeout=10)
        s.sendall(b"GET /relay HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                  b"Sec-WebSocket-Key: eA==\r\n\r\n")
        self.assertIn(b"401", s.recv(4096).split(b"\r\n")[0])
        s.close()

    @accounts_only
    def test_backup(self):
        cookie = self.login()
        rec = struct.pack("<I", 3) + b"b" * 4096
        r, data = self.request("POST", "/api/sync/overlays/backup-x?base=0&name=a.img&size=1048576", rec, cookie=cookie)
        self.assertEqual(r.status, 200)
        blob = b"an object"
        sha = hashlib.sha256(blob).hexdigest()
        self.assertEqual(self.request("PUT", f"/api/sync/objects/{sha}", blob, cookie=cookie)[0].status, 204)
        dest = os.path.join(self.tmp, "backup")
        data_dir = self.server.app.cfg["server"]["data"]
        dbs, objects = backup.backup(data_dir, dest)
        self.assertGreaterEqual(dbs, 2)                      # accounts + the owner's sync
        self.assertGreaterEqual(objects, 1)
        self.assertEqual(backup.backup(data_dir, dest)[1], 0)    # again: nothing new
        with contextlib.closing(sqlite3.connect(os.path.join(dest, "accounts.sqlite"))) as db:
            self.assertIn("owner", [n for (n,) in db.execute("select name from users")])
        found = [os.path.join(r, f) for r, _, fs in os.walk(os.path.join(dest, "users")) for f in fs]
        self.assertTrue(any(f.endswith(sha) for f in found))
        with contextlib.closing(sqlite3.connect(next(f for f in found if f.endswith("sync.sqlite")))) as db:
            self.assertEqual(db.execute("select count(*) from blocks where key = 'backup-x'").fetchone()[0], 1)


@unittest.skipUnless(shutil.which("openssl"), "no openssl to make a certificate")
class TlsTest(unittest.TestCase):
    """[server] tls_cert/tls_key: HTTPS directly, a self-signed certificate
    made here; the handshake in the connection's thread, so a client that
    never finishes one (or speaks plain http) holds up no one else."""
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="executor-web-tls-")
        site = os.path.join(cls.tmp, "site")
        images = os.path.join(cls.tmp, "images")
        os.makedirs(site)
        os.makedirs(images)
        with open(os.path.join(site, "index.html"), "w") as f:
            f.write("<p>tab</p>")
        cls.disk = bytes(range(256)) * 256            # 64 KB
        with open(os.path.join(images, "a.img"), "wb") as f:
            f.write(cls.disk)
        with open(os.path.join(images, "a.img.json"), "w") as f:
            json.dump({"title": "Disk A"}, f)
        cls.cert, key = os.path.join(cls.tmp, "tls.crt"), os.path.join(cls.tmp, "tls.key")
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-subj", "/CN=127.0.0.1", "-addext", "subjectAltName=IP:127.0.0.1",
                        "-keyout", key, "-out", cls.cert], check=True, capture_output=True)
        if BINARY:
            cls.server = Binary(cls.tmp, site, images, "--tls-cert", cls.cert, "--tls-key", key)
            cls.port = cls.server.server_address[1]
            return
        cfg = config.load(None, {"server": {"port": 0, "site": site, "data": os.path.join(cls.tmp, "data"),
                                            "tls_cert": cls.cert, "tls_key": key},
                                 "library": {"images": images}})
        cls.server = app.Server(cfg, quiet=True)
        cls.port = cls.server.server_address[1]
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.server.app.accounts.add_user("owner", "owner-password", owner=True)

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        shutil.rmtree(cls.tmp)

    def request(self, method, path, body=None, headers=None):
        import ssl
        ctx = ssl.create_default_context(cafile=self.cert)    # verified against the certificate
        c = http.client.HTTPSConnection("127.0.0.1", self.port, context=ctx, timeout=10)
        h = dict(headers or {})
        if isinstance(body, dict):
            body = json.dumps(body).encode()
            h["Content-Type"] = "application/json"
        c.request(method, path, body, h)
        r = c.getresponse()
        data = r.read()
        c.close()
        return r, data

    def test_https(self):
        r, _ = self.request("GET", "/")
        self.assertEqual((r.status, r.getheader("Location")), (302, "/login?next=%2F"))
        # Signed in by another host's name (a LAN address): the cookie is Secure.
        r, _ = self.request("POST", "/api/login", {"name": "owner", "password": "owner-password"},
                            headers={"Host": f"192.168.1.20:{self.port}", "Origin": f"https://192.168.1.20:{self.port}"})
        self.assertEqual(r.status, 200)
        self.assertIn("Secure", r.getheader("Set-Cookie"))
        cookie = r.getheader("Set-Cookie").split(";")[0]
        r, data = self.request("GET", "/", headers={"Cookie": cookie})
        self.assertEqual((r.status, data), (200, b"<p>tab</p>"))
        r, data = self.request("GET", "/disks/a.img", headers={"Cookie": cookie, "Range": "bytes=100-199"})
        self.assertEqual((r.status, data), (206, self.disk[100:200]))

    def test_stalled_and_plain_clients(self):
        # One connected and silent (no ClientHello), one speaking plain http:
        # HTTPS requests are answered meanwhile; the plain one is closed, not hung.
        silent = socket.create_connection(("127.0.0.1", self.port))
        plain = socket.create_connection(("127.0.0.1", self.port), timeout=10)
        try:
            plain.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n")
            for _ in range(3):
                self.assertEqual(self.request("GET", "/login")[0].status, 200)
            got = b""
            with contextlib.suppress(ConnectionError):
                while chunk := plain.recv(4096):
                    got += chunk
            self.assertNotIn(b"<p>tab</p>", got)
        finally:
            silent.close()
            plain.close()


class WS:
    """The tab's end of /relay: masked frames out, relay messages in."""
    def __init__(self, s):
        self.s = s
        self.buf = b""

    def send(self, op, id, body=b""):
        payload = struct.pack(">BI", op, id) + body
        mask = os.urandom(4)
        n = len(payload)
        head = bytes([0x82, 0x80 | n]) if n < 126 else (
            struct.pack(">BBH", 0x82, 0x80 | 126, n) if n < 65536 else struct.pack(">BBQ", 0x82, 0x80 | 127, n))
        self.s.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))

    def read(self, n):
        while len(self.buf) < n:
            chunk = self.s.recv(65536)
            if not chunk:
                raise EOFError
            self.buf += chunk
        data, self.buf = self.buf[:n], self.buf[n:]
        return data

    def recv_frame(self):
        b0, b1 = self.read(2)
        n = b1 & 0x7F
        if n == 126:
            n = struct.unpack(">H", self.read(2))[0]
        elif n == 127:
            n = struct.unpack(">Q", self.read(8))[0]
        return b0 & 0x0F, None, self.read(n)

    def recv(self):
        """The next relay message: (op, id, body); pings skipped."""
        while True:
            opcode, _, payload = self.recv_frame()
            if opcode == 0x2:
                op, id = struct.unpack(">BI", payload[:5])
                return op, id, payload[5:]
            if opcode == 0x8:
                raise EOFError(payload)

    def close(self):
        mask = os.urandom(4)
        self.s.sendall(bytes([0x88, 0x80]) + mask)
        try:
            while self.recv_frame()[0] != 0x8:
                pass
        except EOFError:
            pass
        self.s.close()


class Echo:
    """TCP and UDP echo on one loopback port (tests/lib/echo_server.py's, in a thread)."""
    def __init__(self):
        self.tcp = socket.socket()
        self.tcp.bind(("127.0.0.1", 0))
        self.port = self.tcp.getsockname()[1]
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.bind(("127.0.0.1", self.port))
        self.tcp.listen(4)
        self.running = True
        threading.Thread(target=self.tcp_loop, daemon=True).start()
        threading.Thread(target=self.udp_loop, daemon=True).start()

    def tcp_loop(self):
        while self.running:
            try:
                c, _ = self.tcp.accept()
            except OSError:
                return
            threading.Thread(target=self.serve, args=(c,), daemon=True).start()

    @staticmethod
    def serve(c):
        with c:
            while data := c.recv(65536):
                c.sendall(data)

    def udp_loop(self):
        while self.running:
            try:
                data, addr = self.udp.recvfrom(65536)
            except OSError:
                return
            self.udp.sendto(data, addr)

    def stop(self):
        self.running = False
        self.tcp.close()
        self.udp.close()


if __name__ == "__main__":
    unittest.main()
