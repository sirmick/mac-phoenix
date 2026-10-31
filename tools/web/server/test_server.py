#!/usr/bin/env python3
"""tools/web/server's own checks: ranges, accounts, the library, sync, the
relay stub (ctest executor.web.server; no browser needed).

    python3 tools/web/server/test_server.py
"""
import base64
import hashlib
import http.client
import json
import os
import shutil
import socket
import struct
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from server import app, config, static  # noqa: E402


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

    def test_signed_out(self):
        r, _ = self.request("GET", "/")
        self.assertEqual(r.status, 302)
        self.assertEqual(r.getheader("Location"), "/login?next=/")
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
        r, data = self.request("POST", "/api/sync/overlays/a.img-1-2?name=a.img&size=1048576",
                               rec(0, 1) + rec(7, 2), cookie=cookie)
        v1 = json.loads(data)["version"]
        r, data = self.request("POST", "/api/sync/overlays/a.img-1-2", rec(7, 3) + rec(9, 4), cookie=cookie)
        v2 = json.loads(data)["version"]
        self.assertEqual(v2, v1 + 1)
        r, data = self.request("GET", "/api/sync/overlays/a.img-1-2?since=0", cookie=cookie)
        self.assertEqual(r.getheader("X-Sync-Version"), str(v2))
        got = {struct.unpack_from("<I", data, i)[0]: data[i + 4] for i in range(0, len(data), 4100)}
        self.assertEqual(got, {0: 1, 7: 3, 9: 4})
        r, data = self.request("GET", f"/api/sync/overlays/a.img-1-2?since={v1}", cookie=cookie)
        self.assertEqual(len(data), 2 * 4100)               # 7 and 9
        r, data = self.request("POST", "/api/sync/overlays/a.img-1-2", b"short", cookie=cookie)
        self.assertEqual(r.status, 400)
        r, data = self.request("DELETE", "/api/sync/overlays/a.img-1-2", cookie=cookie)
        v3 = json.loads(data)["version"]
        r, data = self.request("GET", f"/api/sync/overlays/a.img-1-2?since={v2}", cookie=cookie)
        self.assertEqual((r.getheader("X-Sync-Reset"), data), ("1", b""))
        r, data = self.request("GET", f"/api/sync/overlays/a.img-1-2?since={v3}", cookie=cookie)
        self.assertEqual(r.getheader("X-Sync-Reset"), "0")

    def test_volume_sync(self):
        cookie = self.login()
        blob = b"kept across browsers"
        sha = hashlib.sha256(blob).hexdigest()
        manifest = {"dirs": ["/executor"], "files": {"/executor/note": {"size": len(blob), "mtime": 5, "sha": sha}}}
        r, data = self.request("PUT", "/api/sync/volumes/own", {"manifest": manifest}, cookie=cookie)
        self.assertEqual((r.status, json.loads(data)["missing"]), (409, [sha]))
        r, data = self.request("POST", "/api/sync/objects/missing", {"shas": [sha]}, cookie=cookie)
        self.assertEqual(json.loads(data)["missing"], [sha])
        self.assertEqual(self.request("PUT", f"/api/sync/objects/{sha}", b"other", cookie=cookie)[0].status, 400)
        self.assertEqual(self.request("PUT", f"/api/sync/objects/{sha}", blob, cookie=cookie)[0].status, 204)
        r, data = self.request("PUT", "/api/sync/volumes/own", {"manifest": manifest}, cookie=cookie)
        self.assertEqual(r.status, 200)
        version = json.loads(data)["version"]
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


if __name__ == "__main__":
    unittest.main()
