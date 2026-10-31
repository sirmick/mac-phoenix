#!/usr/bin/env python3
"""tools/web/server's own checks: ranges, accounts, the library, sync, the
relay stub (ctest executor.web.server; no browser needed).

    python3 tools/web/server/test_server.py
"""
import base64
import contextlib
import hashlib
import http.client
import json
import os
import shutil
import socket
import sqlite3
import struct
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from server import app, backup, config, static  # noqa: E402


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

    def test_login_limits(self):
        app_ = self.server.app
        app_.cfg["server"]["trusted_proxies"] = ["127.0.0.1"]
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
            app_.cfg["server"]["trusted_proxies"] = []
            app_.failures.clear()

    def test_relay_stub(self):
        cookie = self.login()
        s = socket.create_connection(("127.0.0.1", self.port))
        key = base64.b64encode(os.urandom(16)).decode()
        s.sendall((f"GET /relay HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                   f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\nCookie: {cookie}\r\n\r\n").encode())
        data = b""
        while b"\x88" not in data.split(b"\r\n\r\n", 1)[-1] or b"\r\n\r\n" not in data:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
        s.close()
        head, frame = data.split(b"\r\n\r\n", 1)
        self.assertIn(b"101", head.split(b"\r\n")[0])
        accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
        self.assertIn(b"Sec-WebSocket-Accept: " + accept, head)
        self.assertEqual(frame[0], 0x88)
        self.assertEqual(struct.unpack(">H", frame[2:4])[0], 1013)

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


if __name__ == "__main__":
    unittest.main()
