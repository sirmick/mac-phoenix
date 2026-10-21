"""guest.py — drive a mac-phoenix guest through its HTTP API and bridge.

Used by bake.py (installers, archives, copies run in the guest) and
test_image.py (launching what's on an image). Steps, as recipes give them:

  {"launch": "Vol:Folder:App"}           through the bridge
  {"open": "Vol:Folder:Document"}        open with its creator (a Disk Copy
                                         image is mounted)
  {"copy": {"from": PATH, "to": FOLDER}} BridgeAgent COPY: both forks, Finder info
                                         ("replace": true: into the folder of the same
                                         name, files replaced: an update's INTO folders)
  {"click": {"button": "Install"}}       by name, from /api/ui: a control or dialog
                                         item in the front window ("button", or
                                         "item" for any item's text); else the
                                         same text as drawn (/api/text)
  {"click": {...}, "shift": true}        with Shift held: a multiple selection
  {"click": {"text": "Easy Install"}}    by drawn text alone (/api/text): what an
                                         app draws itself; exact, else a prefix
  {"click": {"row": T}}                  the start of the list row that reads T: the
                                         left edge of the dialog user item holding
                                         the text (+ "inset", default 8), at the
                                         text's height: a disclosure triangle an
                                         app draws in its own list
  {"click": {"text": T, "offset": [dx, dy]}}  relative to the drawn text's top
                                         left: the last resort (give the step a
                                         "note" saying why)
  {"popup": {"text": "Easy Install"}, "choose": "Custom Install"}  press a popup
                                         menu where its current value is drawn
                                         ({"button": name}: a popup control),
                                         release on the named item (where the open
                                         menu drew it); front window, "exact" as for
                                         clicks
  {"popup": {"text": "Install Location:", "offset": [126, 5]}, "choose": T}
                                         ... beside a label, relative to its top left:
                                         a value drawn by PowerPC code the text feed
                                         doesn't see (the last resort: a "note" says why)
  {"popup": [x, y], "choose": "Install"} ... pressed at a screen point
  {"check": {"text": "Selected size: 4,302K"}}  fail the bake unless it is drawn
                                         in the front window (exact, else a prefix);
                                         {"text_not": T}: nothing there starts with T;
                                         {"button": name, "value": 1}: a control's
                                         value; polled for "timeout" s (default 5)
  {"wait_for": {"button": "Restart"}, "timeout": s}  until it is in the front window
                                         ({"text": ...}: until it is drawn; with
                                         "front": true, inside the front window)
                                         ("logged": true: drawn since boot, the draw
                                         log: a desktop label a window covers)
  {STEP..., "until": {...}, "max": n}    repeat a step until a find succeeds
                                         ("Switch Disk" until the disk shows)
  {"dclick": {"text": T, "outside_controls": true}}  double-click drawn text that
                                         isn't a control's label (a Standard File entry);
                                         "exact": true: no prefix matches
  {"click": [x, y]}, {"dclick": [x, y]}  screen coordinates (640x480): exploring
                                         only (bake.py --do records a name if one
                                         is there); recipes don't use them
  {"drag": [[x, y], [x2, y2]]}           press, move, release (popup menus)
  {"key": "return"}                      an /api/keypress name or ADB keycode
  {"cmd_key": 12}                        Command + an ADB keycode (12 = Q)
  {"type": "Mac Phoenix"}                ASCII typed on a US keyboard (into the
                                         field with the focus; "tab" moves on)
  {"menu": ["Special", "Shut Down"]}     a menu item, by the drawn text of both
  (System 6, no bridge: "launch" and "open" select each name in the Finder
  and press Command-O; the guest is shut down with Special > Shut Down)
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
            "kpx": os.path.join(storage, "roms", "g3.rom"),
            "se": os.path.join(storage, "roms", "256KB ROMs", "1987-03 - B2E362A8 - Mac SE.ROM"),
            "iici": os.path.join(storage, "roms", "iici.rom")}


# A guest's "backend" here: the emulator's --backend and the machine's own
# arguments. "se" is the 68k core with a Mac SE ROM, 4 MB and its screen.
MACHINE_ARGS = {"uae": ("uae", []), "kpx": ("kpx", []),
                "se": ("uae", ["--ram", "4", "--screen", "512x342"]),
                "iici": ("uae", ["--ram", "8", "--screen", "640x480"])}   # colour, 68030, 24-bit


# ASCII -> (ADB keycode, shift) on a US keyboard, for "type".
TYPE_KEYS = {}
for _keys, _shifted, _codes in (
        ("asdfhgzxcv", "ASDFHGZXCV", range(0x00, 0x0A)),
        ("bqweryt123465=97-80]ou[ip", "BQWERYT!@#$^%+(&_*)}OU{IP",
         [0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
          0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23]),
        ("lj'k;\\,/nm.\t `", 'LJ"K:|<?NM>\t ~',
         [0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32])):
    for _k, _s, _c in zip(_keys, _shifted, _codes):
        TYPE_KEYS.setdefault(_k, (_c, False))
        TYPE_KEYS.setdefault(_s, (_c, True))


def has_agent(backend):
    """Whether BridgeAgent runs there (System 7 and later: not the SE's 6.0.8)."""
    return backend not in ("se", "iici")


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
        self.agent = has_agent(backend)         # BridgeAgent there (System 7.5+; set False for a 7.1 guest:
                                                # no File System Manager, no Host volume, no agent)
        emu_backend, machine_args = MACHINE_ARGS[backend]
        args = [os.path.join(ROOT, "build", "mac-phoenix"), "--config", "/dev/null", "--network", "none",
                "--bridge", "--backend", emu_backend, "--rom", roms(storage)[backend], "--port", str(port),
                *machine_args,
                "--timeout", str(timeout), "--dismiss-shutdown-dialog",
                # what recipes are written for; Mac OS 8.6 and later take the
                # largest mode the driver offers at their first boot
                "--max-resolution", "640x480", *extra]
        self.host = os.path.join(work, "host")  # the Host volume (the bridge's ExtFS root): on
        os.makedirs(self.host, exist_ok=True)   # disk with the rest of the run, not in /tmp
        args += ["--extfs", self.host]
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

    def close_energy_saver(self, timeout=20):
        """Energy Saver's startup message closed, if it comes up within timeout s."""
        start = time.time()
        while time.time() - start < timeout:
            at = self.find({"button": "Close Message"})
            if at:
                self.click(*at)
                self.pause(1)
                return
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
        if what.get("logged"):                  # drawn at some point (the draw log), even if
            draws = (self.get("/api/text?since=0") or {}).get("draws", [])   # covered now
            hit = next((d for d in reversed(draws) if d["text"].strip() == what["text"]), None)
        else:
            hit = self.locate(what, ui)
        if hit:
            l, t, r, b = hit["rect"]
            return (l + r) // 2, (t + b) // 2
        return None

    def locate(self, what, ui=None):
        """What find() finds: the control, dialog item or drawn string
        (its "rect" in screen coordinates), or None."""
        name = what.get("button") or what.get("item") or what.get("control")
        if name:
            ui = ui or self.ui()
            for w in ui["windows"]:
                if not w.get("front"):
                    continue
                hits = [c for c in w.get("controls", []) if c["title"] == name and c["visible"]]
                hits += [i for i in w.get("items", []) if i.get("text") == name and i.get("visible", True)]
                if hits:
                    return hits[0]
        within, skip = None, []
        if what.get("front") or what.get("outside_controls") or name:
            ui = ui or self.ui()
            w = next((w for w in ui["windows"] if w.get("front")), None)
            if what.get("front") or name:       # only the front window's own text (a button
                                                # named in a window behind doesn't count)
                if not w:
                    return None                 # no window up yet (a desktop label isn't one)
                within = w["rect"]
            if what.get("outside_controls") and w:   # not a button's own label: a list entry
                skip = [c["rect"] for c in w.get("controls", [])]
        return self.drawn(what.get("text") or name, app=ui.get("app") if ui else None, within=within, skip=skip,
                          exact=what.get("exact", False))

    def row_start(self, what):
        """(x, y) at the start of the list row that reads what["row"]: the
        left edge (plus "inset", default 8) of the smallest dialog user item
        holding the drawn text (an app's own list; else the front window),
        at the text's vertical centre."""
        self.mouse(x=0, y=0)                    # the cursor off the list, so it reads whole
        time.sleep(0.5)
        ui = self.ui()
        w = next((w for w in ui["windows"] if w.get("front")), None)
        t = w and self.drawn(what["row"], app=ui.get("app"), within=w["rect"], exact=what.get("exact", False))
        if not t:
            raise NotFound(f"no row {what['row']!r} in the front window")
        tl, tt, tr, tb = t["rect"]
        boxes = [i["rect"] for i in w.get("items", []) if i["type"] == "user" and i.get("visible", True)
                 and i["rect"][0] <= tl and i["rect"][1] <= tt and tr <= i["rect"][2] and tb <= i["rect"][3]]
        l = min(boxes, key=lambda r: (r[2] - r[0]) * (r[3] - r[1]))[0] if boxes else w["rect"][0]
        return l + what.get("inset", 8), (tt + tb) // 2

    def check(self, what, timeout=5):
        """Fail unless the guest shows what (polled for `timeout` seconds,
        while a click's effect is drawn): {"text": T} drawn in the front
        window (exact, else a prefix; "exact": true for exact only);
        {"text_not": T}: nothing there starts with T; {"button": name,
        "value": v}: the control's value (a checkbox's 1 or 0)."""
        deadline = time.time() + timeout
        while True:
            ui = self.ui()
            if "value" in what:
                hit = self.locate({k: v for k, v in what.items() if k != "value"}, ui)
                got = hit.get("value") if hit else None
                ok = got == what["value"]
            else:
                w = next((w for w in ui["windows"] if w.get("front")), None)
                if "text_not" in what:
                    drawn = (self.get("/api/text", 10) or {}).get("text", [])
                    l, t, r, b = w["rect"] if w else (0, 0, 0, 0)
                    got = [x["text"].strip() for x in drawn if l <= x["rect"][0] and t <= x["rect"][1]
                           and x["rect"][2] <= r and x["rect"][3] <= b]
                    ok = bool(w) and not any(s.startswith(what["text_not"]) for s in got)
                else:
                    got = self.locate({"front": True, **what}, ui)
                    ok = bool(got)
            if ok:
                return
            if time.time() > deadline:
                front = self.summary().get("front") or {}
                raise RuntimeError(f"check {what} failed (got {got!r}; front window drawn: "
                                   f"{front.get('drawn')})")
            self.pause(1)

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
            if m["hierarchical"] and any(i["text"].strip() == name for i in m["items"]):
                menu = m
        if menu is None:
            self.mouse(button=0, down=False)
            raise NotFound(f"no open menu with {name!r}")     # not open yet: "until" retries
        # Where the open menu drew the item; else counted from the checked
        # one, which opens under the pointer.
        t = self.drawn(name)
        if t and t.get("port") == "screen":
            ty = (t["rect"][1] + t["rect"][3]) // 2
        else:
            texts = [i["text"].strip() for i in menu["items"]]   # some pad with a space
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

    def finder_copy(self, src, dst):
        """Copy without the bridge (System 6): the item's window opened in the
        Finder, the item dragged onto the destination volume's desktop icon
        (dst "Vol:"), then until the Finder's copy progress has gone."""
        parts = [p for p in src.split(":") if p]
        dvol = [p for p in dst.split(":") if p]
        if len(dvol) != 1:
            raise RuntimeError(f"Finder copy: {dst!r}: only to a volume's top level")
        self.finder_open(":".join(parts[:-1]))
        item, w = self.finder_item(parts[-1], in_window=True)
        target, _ = self.finder_item(dvol[0], in_window=False)
        centre = lambda r: ((r[0] + r[2]) // 2, (r[1] + r[3]) // 2)
        ix, iy = centre(item["rect"])
        tx, ty = centre(target["rect"])
        self.drag((ix, iy - 16), (tx, ty - 16))   # by the icons, just above their labels
        quiet = 0
        deadline = time.time() + 900
        while quiet < 3 and time.time() < deadline:
            busy = any("remaining to be copied" in x["text"] or x["text"].strip().startswith("Copying")
                       for x in (self.get("/api/text") or {}).get("text", []))
            quiet = 0 if busy else quiet + 1
            self.pause(1)

    def menu(self, title, item):
        """Choose `item` in the menu bar's `title` by their drawn text: press
        on the title, find the item where the open menu drew it, release
        there. A release the guest missed (the menu still drawn) is clicked
        again."""
        self.mouse(x=320, y=240)                # off the menu bar, so titles read whole
        time.sleep(1)
        t = next((x for x in (self.get("/api/text") or {}).get("text", [])
                  if x["text"].strip() == title and x["rect"][1] < 20), None)
        # Where the title is drawn; else (a menu bar drawn before the text feed
        # was installed: a first boot) along the bar until the menu that drops
        # down has the item.
        xs = [(t["rect"][0] + t["rect"][2]) // 2] if t else list(range(40, 400, 16))
        ty, hit = 10, None
        for tx in xs:
            self.mouse(x=tx, y=ty)
            time.sleep(0.4)
            seq = (self.get("/api/text") or {}).get("seq", 0)
            self.mouse(button=0, down=True)
            time.sleep(1.2)
            draws = (self.get(f"/api/text?since={seq}") or {}).get("draws", [])
            hit = next((d for d in reversed(draws) if d["text"].strip() == item and d["rect"][1] > 20), None)
            if hit:
                break
            self.mouse(button=0, down=False)    # released on the title: nothing chosen
            time.sleep(0.5)
        if not hit:
            raise NotFound(f"no {item!r} in the {title} menu")
        ix, iy = (hit["rect"][0] + hit["rect"][2]) // 2, (hit["rect"][1] + hit["rect"][3]) // 2
        self.mouse(x=ix, y=iy)
        time.sleep(0.6)
        self.mouse(button=0, down=False)
        time.sleep(1.5)
        still = any(x["text"].strip() == item and x["rect"] == hit["rect"]
                    for x in (self.get("/api/text") or {}).get("text", []))
        if still:                               # the release didn't reach MenuSelect
            self.mouse(button=0, down=True)
            time.sleep(0.1)
            self.mouse(button=0, down=False)
            time.sleep(1.5)

    def finder_item(self, name, in_window, redraw=None):
        """Where the Finder draws `name`: in the front window (in_window) or on
        the desktop; waited for (30 s). A label a window's edge clips is
        found whole in the draw log; `redraw` (a one-element list, False)
        allows one Select All when it's missing from the desktop (icons drawn
        before the text feed: the boot disk's)."""
        deadline = time.time() + 30
        while True:
            self.mouse(x=0, y=479)              # the cursor off the names, so they read whole
            time.sleep(0.5)
            ui = self.ui()
            w = next((w for w in ui["windows"] if w.get("front")), None)
            within = w["rect"] if in_window and w else None
            t = self.drawn(name, within=within, exact=True)
            if not t and within:                # a label the window's edge clips: the draw log
                l, tp, r, b = within            # has it whole; its latest draw in the window
                draws = (self.get("/api/text?since=0") or {}).get("draws", [])
                t = next((d for d in reversed(draws) if d["text"].strip() == name
                          and d["rect"][2] > l and d["rect"][0] < r
                          and tp <= d["rect"][1] and d["rect"][3] <= b), None)
                if t:                           # its visible part
                    t = dict(t, rect=[max(t["rect"][0], l + 2), t["rect"][1],
                                      min(t["rect"][2], r - 2), t["rect"][3]])
            if t:
                return t, w
            if time.time() > deadline:
                raise NotFound(f"no {name!r} in the Finder")
            if not in_window and redraw is not None and not redraw[0]:
                redraw[0] = True                # Select All redraws the desktop's labels,
                self.cmd_key(0x00)              # a click on an empty corner deselects them
                scr = (self.get("/api/text") or {}).get("screen", [640, 480])
                self.click(4, scr[1] - 4)
                continue
            self.pause(1)

    def finder_open(self, path):
        """Open Vol:Folder:...:Item in the Finder without the bridge (System
        6): each name selected where it is drawn (the volume on the desktop,
        the rest in the window the last one opened) and opened with
        Command-O."""
        redraw = [False]
        for i, name in enumerate(p for p in path.split(":") if p):
            t, _ = self.finder_item(name, in_window=i > 0, redraw=redraw)
            self.click((t["rect"][0] + t["rect"][2]) // 2, (t["rect"][1] + t["rect"][3]) // 2)
            self.cmd_key(0x1F)                  # O
            time.sleep(2)

    def cmd_key(self, keycode):
        """Command + an ADB keycode (0x0C Q, 0x2F period, 0x0D W)."""
        self.post("/api/keypress", {"key": 0x37, "down": True})
        time.sleep(0.1)
        self.post("/api/keypress", {"key": keycode})
        time.sleep(0.1)
        self.post("/api/keypress", {"key": 0x37, "down": False})
        time.sleep(1)

    def type_text(self, text):
        """ASCII typed as ADB key events on a US keyboard (a name, a serial)."""
        for ch in text:
            keycode, shift = TYPE_KEYS[ch]
            if shift:
                self.post("/api/keypress", {"key": 0x38, "down": True})
            self.post("/api/keypress", {"key": keycode})
            if shift:
                self.post("/api/keypress", {"key": 0x38, "down": False})
            time.sleep(0.05)
        time.sleep(0.5)

    # ---- apps

    def host_dir(self):
        """The host folder the guest sees as Host: (the bridge's ExtFS root)."""
        return self.host

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

        if self.agent:
            self.post("/api/quit", {}, 60)
        else:
            self.cmd_key(0x0C)                  # Command-Q (System 6: no bridge)
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
        """A clean shutdown, so every volume is flushed and marked unmounted:
        the bridge's, or without an agent (System 6) the Finder's Special >
        Shut Down, then the emulator stopped once the Mac says it's safe."""
        if not self.agent:
            try:
                self.menu("Special", "Shut Down")
                deadline = time.time() + 60
                while time.time() < deadline and not self.drawn("You may now switch off your Macintosh safely."):
                    self.pause(1)
            finally:
                self.stop()
            return
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
        if ("launch" in step or "open" in step) and not self.agent:
            self.finder_open(step.get("launch") or step["open"])    # no bridge (System 6)
        elif "launch" in step or "open" in step:
            body = {"path": step["launch"]} if "launch" in step else {"path": step["open"], "open": True}
            r = self.bridge("/api/launch", body)
            if not r.get("success"):
                raise RuntimeError(f"{body}: {r}")
        elif "copy" in step and not self.agent:
            self.finder_copy(step["copy"]["from"], step["copy"]["to"])
        elif "copy" in step:
            r = self.bridge("/api/copy", {"from": step["copy"]["from"], "to": step["copy"]["to"],
                                          "replace": bool(step["copy"].get("replace"))}, 700)
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
            if "row" in what:                   # the start of a list row (a disclosure triangle)
                self.click(*self.row_start(what))
                return
            if what.get("text") and not what.get("logged"):
                self.mouse(x=0, y=479)          # the cursor off the text, so it reads whole
                time.sleep(0.5)                 # (it sits where the last click was)
            pos = self.find(what)
            if not pos:
                raise NotFound(f"no {what} in the front window")
            if step.get("shift"):               # extend a selection (the Installer's list)
                self.post("/api/keypress", {"key": 0x38, "down": True})
                time.sleep(0.2)
            self.click(*pos)
            if step.get("shift"):
                self.post("/api/keypress", {"key": 0x38, "down": False})
                time.sleep(0.5)
        elif "check" in step:
            self.check(step["check"], step.get("timeout", 5))
        elif "popup" in step and isinstance(step["popup"], dict):
            if "offset" in step["popup"]:       # beside its label (a value drawn natively)
                t = self.drawn(step["popup"]["text"], app=self.app())
                pos = t and (t["rect"][0] + step["popup"]["offset"][0], t["rect"][1] + step["popup"]["offset"][1])
            else:
                pos = self.find({"front": True, **step["popup"]})   # its current value, or the control
            if not pos:
                raise NotFound(f"no popup {step['popup']} in the front window")
            self.choose(*pos, step["choose"])
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
        elif "type" in step:
            self.type_text(step["type"])
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
                if not step.get("optional"):    # optional: it may have come and gone between looks
                    raise RuntimeError(f"timeout: {step} (front app {self.app()!r})")
        elif "menu" in step:
            self.menu(*step["menu"])
        elif "wait_finder" in step:
            self.pause(5)                       # the restart under way, not the old Finder
            self.wait_finder(step["wait_finder"], agent=self.agent)
            if self.backend == "kpx":           # the G3's Energy Saver: "This computer is set to go
                self.close_energy_saver()       # into sleep" at every startup but the first
        elif "agent_exit" in step:              # BridgeAgent quits itself till the next boot
            self.post("/api/quit", {"agent": True}, 60)
            self.pause(2)
        elif "quit" in step:
            if self.agent:
                self.post("/api/quit", {}, 60)
            else:
                self.cmd_key(0x0C)              # Command-Q (System 6: no bridge)
            time.sleep(3)
        elif "close" in step:
            if not self.close_front(step["close"]):
                raise RuntimeError(f"{step['close']} would not quit")
        elif "shot" in step:
            self.shot(step["shot"])
        else:
            raise ValueError(f"unknown step {step}")
        return self.app()
