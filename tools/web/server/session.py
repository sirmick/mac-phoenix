"""One controlling tab a user (standalone mode: the server is the store).

Each tab says it is there every few seconds (POST /api/session?id=ID):
the first gets control and keeps it while it does; the others are
viewers, shown the controller's screen (PUT/GET /api/session/screen:
a PNG it sends when someone watches). take=1 takes control: the old
controller sees it at its next call, saves and lets go (release); the
new one waits for that (handing) before it reads the store. A
controller silent for LEASE seconds (its tab closed without a word)
has let go.
"""
import threading
import time

LEASE = 10                              # s a tab counts without a word
SCREEN_MAX = 8 << 20


class Sessions:
    def __init__(self):
        self.lock = threading.Lock()
        self.users = {}                 # user id -> state

    def _state(self, user, now):
        s = self.users.setdefault(user, {"holder": None, "seen": {}, "handing": None, "screen": None})
        s["seen"] = {k: t for k, t in s["seen"].items() if t > now - LEASE}
        if s["holder"] not in s["seen"]:
            s["holder"] = None
            s["screen"] = None                  # the lapsed tab's, not the next one's
        if s["handing"] not in s["seen"]:
            s["handing"] = None
        return s

    def _answer(self, s, sid):
        return {"control": s["holder"] == sid,
                "held": s["holder"] is not None and s["holder"] != sid,
                "handing": s["handing"] is not None and s["holder"] == sid,
                "viewers": sum(1 for k in s["seen"] if k != s["holder"])}

    def beat(self, user, sid, take=False):
        now = time.monotonic()
        with self.lock:
            s = self._state(user, now)
            s["seen"][sid] = now
            if s["holder"] is None:
                s["holder"] = sid
            elif take and s["holder"] != sid:
                s["handing"] = s["holder"]
                s["holder"] = sid
                s["screen"] = None
            return self._answer(s, sid)

    def release(self, user, sid):
        with self.lock:
            s = self._state(user, time.monotonic())
            s["seen"].pop(sid, None)
            if s["holder"] == sid:
                s["holder"] = None
                s["screen"] = None
            if s["handing"] == sid:
                s["handing"] = None

    def put_screen(self, user, sid, png):
        with self.lock:
            s = self._state(user, time.monotonic())
            if s["holder"] != sid:
                return False
            s["screen"] = png
            return True

    def screen(self, user):
        with self.lock:
            s = self._state(user, time.monotonic())
            return s["screen"] if s["holder"] else None
