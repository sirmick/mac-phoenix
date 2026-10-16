"""guest.py — drive a mac-phoenix guest through its HTTP API and bridge.

Used by bake.py (installers, archives, copies run in the guest) and
test_image.py (launching what's on an image). Steps, as recipes give them:

  {"launch": "Vol:Folder:App"}           through the bridge
  {"open": "Vol:Folder:Document"}        open with its creator (a Disk Copy
                                         image is mounted)
  {"copy": {"from": PATH, "to": FOLDER}} BridgeAgent COPY: both forks, Finder info
  {"click": {"button": "Install"}}       by name, from /api/ui: a control or dialog
                                         item in the front window ("button", or
                                         "item" for any item's text)
  {"popup": [x, y], "choose": "Install"} press a popup menu, release on the named
                                         item (read from the open menu)
  {"wait_for": {"button": "Restart"}, "timeout": s}  until it is in the front window
  {"click": [x, y]}, {"dclick": [x, y]}  screen coordinates (640x480): for what an
                                         app draws itself (a VISE option list)
  {"drag": [[x, y], [x2, y2]]}           press, move, release (popup menus)
  {"key": "return"}                      an /api/keypress name or ADB keycode
  {"cmd_key": 12}                        Command + an ADB keycode (12 = Q)
  {"wait": seconds}
  {"wait_app": "Name", "timeout": s}     until the front app is Name
  {"wait_app_not": "Name", "timeout": s} until it isn't
  {"wait_finder": seconds}               after a restart: until the Finder is back
  {"quit": true}                         quit the front app (the bridge's Quit)
  {"close": "Name"}                      quit Name and get the Finder back,
                                         past modal dialogs
  {"shot": "name"}                       a screenshot into the work dir
"""

import json
import os
import subprocess
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def roms(storage):
    return {"uae": os.path.expanduser(os.environ.get("MACEMU_ROM", "~/roms/quadra.rom")),
            "kpx": os.path.join(storage, "roms", "g3.rom")}


class Guest:
    def __init__(self, work, backend, disks, cdroms, port, storage, timeout=3600):
        self.base = f"http://localhost:{port}"
        self.backend, self.work = backend, work
        args = [os.path.join(ROOT, "build", "mac-phoenix"), "--config", "/dev/null", "--network", "none",
                "--bridge", "--backend", backend, "--rom", roms(storage)[backend], "--port", str(port),
                "--timeout", str(timeout), "--dismiss-shutdown-dialog"]
        for d in disks:
            args += ["--disk", d]
        for c in cdroms:
            args += ["--cdrom", c]
        self.log = open(os.path.join(work, "emulator.log"), "a")
        self.proc = subprocess.Popen(args, stdout=self.log, stderr=subprocess.STDOUT, cwd=ROOT)
        for _ in range(80):
            if self.get("/api/status") is not None:
                break
            time.sleep(0.5)
        self.post("/api/emulator/start", {})

    # ---- HTTP

    def call(self, path, body=None, timeout=30):
        req = urllib.request.Request(self.base + path, method="POST" if body is not None else "GET",
                                     data=json.dumps(body).encode() if body is not None else None)
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                data = r.read()
                if r.headers.get_content_type() != "application/json":
                    return data
                try:
                    return json.loads(data)
                except UnicodeDecodeError:          # app names come back in MacRoman
                    return json.loads(data.decode("mac_roman"))
        except OSError:
            return None

    def get(self, path, timeout=30):
        return self.call(path, None, timeout)

    def post(self, path, body, timeout=30):
        return self.call(path, body, timeout)

    def app(self):
        return (self.get("/api/app") or {}).get("app", "")

    def wait_finder(self, timeout, agent=False):
        """Until the Finder is up; with agent=True also until the bridge's
        agent is running (after a restart: the PowerPC machine's boot phase
        stays at desktop through one, so the Finder alone says nothing)."""
        start = time.time()
        r = self.post("/api/wait", {"condition": "boot=Finder", "timeout": timeout}, timeout + 10)
        if not (r or {}).get("ok"):
            raise RuntimeError(f"no Finder within {timeout}s")
        while agent and not (self.get("/api/status") or {}).get("bridge_agent_connected"):
            if time.time() - start > timeout:
                raise RuntimeError(f"no bridge agent within {timeout}s")
            time.sleep(2)

    def shot(self, name, front=False, scale=1.0):
        """A screenshot; front=True crops it to the front window, scale < 1
        shrinks it (cheaper for a driver to look at)."""
        data = self.get("/api/screenshot")
        if not isinstance(data, bytes):
            return None
        path = os.path.join(self.work, name + ".png")
        with open(path, "wb") as f:
            f.write(data)
        if front or scale != 1.0:
            from PIL import Image
            im = Image.open(path)
            if front:
                w = next((w for w in self.ui()["windows"] if w.get("front")), None)
                if w:
                    l, t, r, b = w["rect"]
                    im = im.crop((max(l - 4, 0), max(t - 22, 0), min(r + 4, im.width), min(b + 4, im.height)))
            if scale != 1.0:
                im = im.resize((max(int(im.width * scale), 1), max(int(im.height * scale), 1)))
            im.save(path)
        return path

    # ---- the guest's UI (/api/ui)

    def ui(self):
        return self.get("/api/ui", 10) or {"windows": [], "menus": []}

    def find(self, what, ui=None):
        """(x, y) of the centre of a named control or dialog item in the
        front window, or None. what: {"button": name} or {"item": text}."""
        ui = ui or self.ui()
        name = what.get("button") or what.get("item") or what.get("control")
        for w in ui["windows"]:
            if not w.get("front"):
                continue
            hits = [c for c in w.get("controls", []) if c["title"] == name and c["visible"]]
            hits += [i for i in w.get("items", []) if i.get("text") == name and i.get("visible", True)]
            for h in hits:
                l, t, r, b = h["rect"]
                return (l + r) // 2, (t + b) // 2
        return None

    def at(self, x, y, ui=None):
        """What is at screen point (x, y) in the front window, by name, or None."""
        ui = ui or self.ui()
        for w in ui["windows"]:
            if not w.get("front"):
                continue
            for c in w.get("controls", []):
                l, t, r, b = c["rect"]
                if c["title"] and c["visible"] and l <= x < r and t <= y < b:
                    return {"button": c["title"]}
            for i in w.get("items", []):
                l, t, r, b = i["rect"]
                if i.get("text") and i.get("visible", True) and l <= x < r and t <= y < b:
                    return {"item": i["text"]}
        return None

    def summary(self):
        """The front window and its named, visible controls and items: what
        a driver needs between steps, as text (cheaper than a screenshot)."""
        ui = self.ui()
        out = {"app": ui.get("app"), "front": None}
        for w in ui["windows"]:
            if w.get("front"):
                out["front"] = {"title": w["title"], "rect": w["rect"],
                                "controls": [{"title": c["title"], "rect": c["rect"], "value": c["value"],
                                              "enabled": c["enabled"]} for c in w.get("controls", [])
                                             if c["visible"] and c["title"]],
                                "text": [i["text"] for i in w.get("items", [])
                                         if i.get("type") in ("text", "edit") and i.get("text") and i.get("visible", True)]}
                break
        return out

    def choose(self, x, y, name, item_height=16):
        """Press the popup at (x, y), find `name` in the open menu, release on
        it. The menu opens with its checked item under the pointer."""
        self.mouse(x=x, y=y)
        time.sleep(0.4)
        self.mouse(button=0, down=True)
        time.sleep(1)
        menu = None
        for m in self.ui().get("menus", []):
            if m["hierarchical"] and any(i["text"] == name for i in m["items"]):
                menu = m
        if menu is None:
            self.mouse(button=0, down=False)
            raise RuntimeError(f"no open menu with {name!r}")
        texts = [i["text"] for i in menu["items"]]
        current = next((k for k, i in enumerate(menu["items"]) if i["mark"]), 0)
        dy = (texts.index(name) - current) * item_height
        self.mouse(x=x, y=y + dy)
        time.sleep(0.6)
        self.mouse(button=0, down=False)
        time.sleep(1)

    # ---- input

    def mouse(self, **kw):
        self.post("/api/mouse", kw)

    def click(self, x, y):
        self.mouse(x=x, y=y)
        time.sleep(0.4)
        self.mouse(button=0, down=True)
        time.sleep(0.2)
        self.mouse(button=0, down=False)
        time.sleep(1)

    def dclick(self, x, y):
        self.mouse(x=x, y=y)
        time.sleep(0.4)
        for _ in range(2):
            self.mouse(button=0, down=True)
            time.sleep(0.08)
            self.mouse(button=0, down=False)
            time.sleep(0.12)
        time.sleep(1)

    def drag(self, a, b):
        self.mouse(x=a[0], y=a[1])
        time.sleep(0.4)
        self.mouse(button=0, down=True)
        time.sleep(1)
        self.mouse(x=b[0], y=b[1])
        time.sleep(0.6)
        self.mouse(button=0, down=False)
        time.sleep(1)

    def cmd_key(self, keycode):
        """Command + an ADB keycode (0x0C Q, 0x2F period, 0x0D W)."""
        self.post("/api/keypress", {"key": 0x37, "down": True})
        time.sleep(0.1)
        self.post("/api/keypress", {"key": keycode})
        time.sleep(0.1)
        self.post("/api/keypress", {"key": 0x37, "down": False})
        time.sleep(1)

    # ---- apps

    def host_dir(self):
        """The host folder the guest sees as Host: (the bridge's ExtFS root)."""
        import glob
        for d in glob.glob("/tmp/macemu-bridge-*"):
            if os.path.isdir(os.path.join(d, "MacPhoenix", str(self.proc.pid))):
                return d
        return None

    def run_script(self, creator, script, out, wait=30):
        """A do-script that writes Host:<out>; its text once it appears
        (tests/test_threads_executor.sh drives Frontier the same way)."""
        path = os.path.join(self.host_dir(), out)
        if os.path.exists(path):
            os.unlink(path)
        self.post("/api/script", {"creator": creator, "script": script}, 60)
        for _ in range(wait):
            if os.path.exists(path) and os.path.getsize(path):
                time.sleep(0.5)
                with open(path, "rb") as f:
                    return f.read().decode("mac_roman").replace("\r", "\n")
            time.sleep(1)
        return None

    def frontier_ready(self, attempts=60):
        """After launching Frontier: until it answers scripts (it runs its
        start-up scripts first)."""
        for _ in range(attempts):
            if self.run_script("LAND", 'file.writeWholeFile ("Host:up.txt", "up")', "up.txt", 2) == "up":
                return True
        return False

    def bridge(self, path, body, timeout=240):
        """A bridge command, retried while the agent is still starting."""
        r = {}
        for _ in range(4):
            r = self.post(path, body, timeout) or {}
            if r.get("success") or "bridge agent" not in str(r.get("error", "")):
                break
            time.sleep(20)
        return r

    def close_front(self, name, patience=10):
        """Quit app `name` and get the Finder back in front: the bridge's
        Quit, then (only while `name` itself is in front, never the bridge's
        own agent) Escape or Return for a modal dialog and Command-Q."""
        def settle():
            deadline = time.time() + patience
            while time.time() < deadline:
                front = self.app()
                if front == "Finder":
                    return True
                if name.lower() in front.lower():
                    return False            # still there, in front: escalate
                time.sleep(1)               # BridgeAgent mid-command, or switching
            return False

        self.post("/api/quit", {}, 60)
        if settle():
            return True
        for keys in ("escape", "return", "cmd-q", "escape", "return", "cmd-q"):
            if name.lower() not in self.app().lower():
                if settle():
                    return True
                continue
            if keys in ("escape", "return"):     # cancel, or take the default button
                self.post("/api/keypress", {"key": keys})
                time.sleep(2)
                if name.lower() in self.app().lower():
                    self.post("/api/quit", {}, 60)
            else:
                self.cmd_key(0x0C)
            if settle():
                return True
        return False

    def shutdown(self):
        """A clean shutdown, so every volume is flushed and marked unmounted."""
        self.post("/api/shutdown", {}, 60)
        try:
            self.proc.wait(timeout=120)
        except subprocess.TimeoutExpired:
            self.proc.terminate()
            self.proc.wait(timeout=30)

    def stop(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=60)
            except subprocess.TimeoutExpired:
                self.proc.kill()

    # ---- steps

    def run(self, step):
        """Run one recipe step; returns a short result for logs."""
        if "launch" in step or "open" in step:
            body = {"path": step["launch"]} if "launch" in step else {"path": step["open"], "open": True}
            r = self.bridge("/api/launch", body)
            if not r.get("success"):
                raise RuntimeError(f"{body}: {r}")
        elif "copy" in step:
            r = self.bridge("/api/copy", {"from": step["copy"]["from"], "to": step["copy"]["to"]}, 700)
            if not r.get("success"):
                raise RuntimeError(f"copy {step['copy']}: {r}")
        elif "click" in step and isinstance(step["click"], dict):
            pos = self.find(step["click"])
            if not pos:
                raise RuntimeError(f"no {step['click']} in the front window")
            self.click(*pos)
        elif "popup" in step:
            self.choose(*step["popup"], step["choose"])
        elif "wait_for" in step:
            deadline = time.time() + step.get("timeout", 120)
            while not self.find(step["wait_for"]):
                if time.time() > deadline:
                    raise RuntimeError(f"timeout waiting for {step['wait_for']}")
                time.sleep(2)
        elif "click" in step:
            self.click(*step["click"])
        elif "dclick" in step:
            self.dclick(*step["dclick"])
        elif "drag" in step:
            self.drag(*step["drag"])
        elif "key" in step:
            self.post("/api/keypress", {"key": step["key"]})
            time.sleep(1)
        elif "cmd_key" in step:
            self.cmd_key(step["cmd_key"])
        elif "wait" in step:
            time.sleep(step["wait"])
        elif "wait_app" in step or "wait_app_not" in step:
            want, gone = step.get("wait_app"), step.get("wait_app_not")
            deadline = time.time() + step.get("timeout", 120)
            while time.time() < deadline:
                a = self.app()
                if (want and want in a) or (gone and gone not in a and a):
                    break
                time.sleep(2)
            else:
                raise RuntimeError(f"timeout: {step} (front app {self.app()!r})")
        elif "wait_finder" in step:
            time.sleep(15)
            self.wait_finder(step["wait_finder"], agent=True)
        elif "quit" in step:
            self.post("/api/quit", {}, 60)
            time.sleep(3)
        elif "close" in step:
            if not self.close_front(step["close"]):
                raise RuntimeError(f"{step['close']} would not quit")
        elif "shot" in step:
            self.shot(step["shot"])
        else:
            raise ValueError(f"unknown step {step}")
        return self.app()
