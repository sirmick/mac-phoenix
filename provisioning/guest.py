"""guest.py — drive a mac-phoenix guest through its HTTP API and bridge.

Used by bake.py (installers, archives, copies run in the guest) and
test_image.py (launching what's on an image). Steps, as recipes give them:

  {"launch": "Vol:Folder:App"}           through the bridge
  {"open": "Vol:Folder:Document"}        open with its creator (a Disk Copy
                                         image is mounted)
  {"copy": {"from": PATH, "to": FOLDER}} BridgeAgent COPY: both forks, Finder info
  {"click": {"button": "Install"}}       by name, from /api/ui: a control or dialog
                                         item in the front window ("button", or
                                         "item" for any item's text); else the
                                         same text as drawn (/api/text)
  {"click": {"text": "Easy Install"}}    by drawn text alone (/api/text): what an
                                         app draws itself; exact, else a prefix
  {"click": {"text": T, "offset": [dx, dy]}}  relative to the drawn text's top
                                         left: a button an app draws beside its label
  {"popup": [x, y], "choose": "Install"} press a popup menu, release on the named
                                         item (where the open menu drew it)
  {"wait_for": {"button": "Restart"}, "timeout": s}  until it is in the front window
                                         ({"text": ...}: until it is drawn; with
                                         "front": true, inside the front window)
  {STEP..., "until": {...}, "max": n}    repeat a step until a find succeeds
                                         ("Switch Disk" until the disk shows)
  {"dclick": {"text": T, "outside_controls": true}}  double-click drawn text that
                                         isn't a control's label (a Standard File entry);
                                         "exact": true: no prefix matches
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
  {"reboot": true}                       (bake.py) the guest restarted fresh, same disks
  {"agent_exit": true}                   BridgeAgent quits itself until the next boot
                                         (out of Apple's Installer's way)
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


class NotFound(RuntimeError):
    """A step's control or text isn't on screen (yet): "until" retries these."""


def watch_url(port):
    """Where to watch a guest from another machine: the web UI on this host's
    LAN address, view only (?view: no input reaches the guest)."""
    import socket
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("10.255.255.255", 1))            # picks the LAN interface; sends nothing
            host = s.getsockname()[0]
    except OSError:
        host = "localhost"
    return f"http://{host}:{port}/?view"


class Guest:
    def __init__(self, work, backend, disks, cdroms, port, storage, timeout=3600, extra=()):
        self.base = f"http://localhost:{port}"
        self.backend, self.work = backend, work
        args = [os.path.join(ROOT, "build", "mac-phoenix"), "--config", "/dev/null", "--network", "none",
                "--bridge", "--backend", backend, "--rom", roms(storage)[backend], "--port", str(port),
                "--timeout", str(timeout), "--dismiss-shutdown-dialog", *extra]
        for d in disks:
            args += ["--disk", d]
        for c in cdroms:
            args += ["--cdrom", c]
        self.cancelled = False              # set from another thread: waits stop
        self.port = port
        import socket
        with socket.socket() as probe:          # someone else's emulator there would answer
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)   # TIME_WAIT is fine
            try:                                # every request in this one's place
                probe.bind(("", port))
            except OSError:
                raise RuntimeError(f"port {port} is already in use (an emulator left running?)")
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

    # ---- following along, and stopping

    def watch_url(self):
        return watch_url(self.port)

    def annotate(self, text):
        """A line for the web UI's driver banner (what is being done now)."""
        self.post("/api/annotate", {"text": text}, 5)

    def bomb(self):
        """The text of a system error alert on screen ("Sorry, a system error
        occurred", the app, the error type), or None."""
        found = (self.get("/api/text", 10) or {}).get("text", [])
        hit = next((t for t in found if "system error" in t["text"]), None)
        if not hit:
            return None
        top = hit["rect"][1]
        near = sorted((t for t in found if 0 <= t["rect"][1] - top < 60), key=lambda t: t["rect"][1])
        return " / ".join(t["text"].strip() for t in near)

    def tick(self):
        """In every wait: stop if cancelled, fail at once on a system error."""
        if self.cancelled:
            raise RuntimeError("cancelled")
        b = self.bomb()
        if b:
            self.shot("bomb")
            raise RuntimeError(f"system error on screen: {b}")

    def pause(self, seconds):
        """Sleep, checking tick() every couple of seconds."""
        end = time.time() + seconds
        while True:
            self.tick()
            left = end - time.time()
            if left <= 0:
                return
            time.sleep(min(left, 2))

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
            self.pause(2)

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
        front window, or None. what: {"button": name} or {"item": text}; or
        {"text": text}, drawn text only. A name /api/ui doesn't have is
        looked for as drawn text too (controls an app draws itself)."""
        name = what.get("button") or what.get("item") or what.get("control")
        if name:
            ui = ui or self.ui()
            for w in ui["windows"]:
                if not w.get("front"):
                    continue
                hits = [c for c in w.get("controls", []) if c["title"] == name and c["visible"]]
                hits += [i for i in w.get("items", []) if i.get("text") == name and i.get("visible", True)]
                for h in hits:
                    l, t, r, b = h["rect"]
                    return (l + r) // 2, (t + b) // 2
        within, skip = None, []
        if what.get("front") or what.get("outside_controls"):
            ui = ui or self.ui()
            w = next((w for w in ui["windows"] if w.get("front")), None)
            if what.get("front"):               # only the front window's own text
                within = w and w["rect"]
            if what.get("outside_controls") and w:   # not a button's own label: a list entry
                skip = [c["rect"] for c in w.get("controls", [])]
        t = self.drawn(what.get("text") or name, app=ui.get("app") if ui else None, within=within, skip=skip,
                       exact=what.get("exact", False))
        if t:
            l, top, r, b = t["rect"]
            return (l + r) // 2, (top + b) // 2
        return None

    def drawn(self, text, app=None, within=None, skip=(), exact=False):
        """The string on screen that reads text (exactly, else starting with
        it), from /api/text; the front app's first. within: only inside this
        screen rect (the front window). None if not there."""
        if not text:
            return None
        found = (self.get("/api/text", 10) or {}).get("text", [])
        if within:
            l, t, r, b = within
            found = [x for x in found if l <= x["rect"][0] and t <= x["rect"][1]
                     and x["rect"][2] <= r and x["rect"][3] <= b]
        def inside(x, c):
            return c[0] <= x["rect"][0] and c[1] <= x["rect"][1] and x["rect"][2] <= c[2] and x["rect"][3] <= c[3]
        found = [x for x in found if not any(inside(x, c) for c in skip)]
        for match in (lambda s: s == text, lambda s: not exact and s.startswith(text)):
            hits = [t for t in found if match(t["text"].strip())]
            hits.sort(key=lambda t: t.get("app") != app)
            if hits:
                return hits[0]
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
                title = w["title"]
                drawn = (self.get("/api/text", 10) or {}).get("text", [])
                out["front"]["drawn"] = [t["text"] for t in drawn if t.get("window") == title]
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
        # Where the open menu drew the item; else counted from the checked
        # one, which opens under the pointer.
        t = self.drawn(name)
        if t and t.get("port") == "screen":
            ty = (t["rect"][1] + t["rect"][3]) // 2
        else:
            texts = [i["text"] for i in menu["items"]]
            current = next((k for k, i in enumerate(menu["items"]) if i["mark"]), 0)
            ty = y + (texts.index(name) - current) * item_height
        self.mouse(x=x, y=ty)
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
        for _ in range(20):
            r = self.post(path, body, timeout) or {}
            if r.get("success") or "bridge agent" not in str(r.get("error", "")):
                break
            self.pause(3)
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
        if "until" in step:                     # repeat the step until something shows
            rest = {k: v for k, v in step.items() if k not in ("until", "max")}
            for _ in range(step.get("max", 10)):
                if self.find(step["until"]):
                    return
                try:
                    self.run(rest)
                except NotFound:                # not up yet: look again
                    pass
                self.pause(1)
            if not self.find(step["until"]):
                raise RuntimeError(f"{rest} never brought {step['until']}")
            return
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
            what = step["click"]
            if "offset" in what:                # relative to a label's top left: an
                self.mouse(x=0, y=0)            # app-drawn button beside its text
                time.sleep(0.5)                 # (the cursor off it, so it reads whole)
                t = self.drawn(what["text"], app=self.app())
                if not t:
                    raise NotFound(f"no {what['text']!r} drawn")
                self.click(t["rect"][0] + what["offset"][0], t["rect"][1] + what["offset"][1])
                return
            pos = self.find(what)
            if not pos:
                raise NotFound(f"no {what} in the front window")
            self.click(*pos)
        elif "popup" in step:
            self.choose(*step["popup"], step["choose"])
        elif "wait_for" in step:
            deadline = time.time() + step.get("timeout", 120)
            while not self.find(step["wait_for"]):
                if time.time() > deadline:
                    raise RuntimeError(f"timeout waiting for {step['wait_for']}")
                self.pause(2)
        elif "click" in step:
            self.click(*step["click"])
        elif "dclick" in step and isinstance(step["dclick"], dict):
            pos = self.find(step["dclick"])
            if not pos:
                raise NotFound(f"no {step['dclick']} in the front window")
            self.dclick(*pos)
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
            self.pause(step["wait"])
        elif "wait_app" in step or "wait_app_not" in step:
            want, gone = step.get("wait_app"), step.get("wait_app_not")
            deadline = time.time() + step.get("timeout", 120)
            while time.time() < deadline:
                a = self.app()
                if (want and want in a) or (gone and gone not in a and a):
                    break
                self.pause(2)
            else:
                raise RuntimeError(f"timeout: {step} (front app {self.app()!r})")
        elif "wait_finder" in step:
            self.pause(5)                       # the restart under way, not the old Finder
            self.wait_finder(step["wait_finder"], agent=True)
        elif "agent_exit" in step:              # BridgeAgent quits itself till the next boot
            self.post("/api/quit", {"agent": True}, 60)
            self.pause(2)
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
