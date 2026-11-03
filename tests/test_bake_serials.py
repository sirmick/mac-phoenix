#!/usr/bin/env python3
"""test_bake_serials.py -- provisioning/bake.py's {"serial": ID} steps, with a
fake serials file (MACPHOENIX_SERIALS) and a fake guest: the value is typed,
and is in no log line, annotation, bake key or error message; a missing or
empty serial stops the bake before a guest boots ("serial needed for ID",
exit status 3). No emulator."""
import io
import json
import os
import sys
import tempfile
import contextlib
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = tempfile.mkdtemp(prefix="bake-serials.")
SERIALS = os.path.join(WORK, "serials.json")
SECRET, NAME, ORG = "AB12-CD34-EF56", "Test Owner", "Test Org"
with open(SERIALS, "w") as f:
    json.dump({"defaults": {"name": NAME, "organization": ORG},
               "products": [{"id": "fake-1.0", "serial": SECRET, "name": "", "organization": ""},
                            {"id": "fake-empty", "serial": ""},
                            {"id": "fake-odd", "serial": "ABé"}]}, f)
os.environ["MACPHOENIX_SERIALS"] = SERIALS
sys.path.insert(0, os.path.join(ROOT, "provisioning"))
import bake  # noqa: E402

failures = []


def expect(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


def raises(fn, *a):
    try:
        fn(*a)
    except bake.SerialNeeded as e:
        return str(e)
    return None


SECRETS = (SECRET, NAME, ORG, "ABé")


def clean(text):
    return not any(s in text for s in SECRETS)


# values
expect(bake.serial_value("fake-1.0") == SECRET, "the product's serial")
expect(bake.serial_value("fake-1.0", "name") == NAME, "name from the defaults")
expect(bake.serial_value("fake-1.0", "organization") == ORG, "organization from the defaults")

# what stops a bake, and says why without a value
for product, want in (("fake-empty", "serial needed for fake-empty"),
                      ("fake-missing", "serial needed for fake-missing"),
                      ("fake-odd", "serial for fake-odd has a character")):
    msg = raises(bake.serial_value, product)
    expect(msg is not None and msg.startswith(want) and clean(msg), f"{product}: {msg!r}")
bake.SERIALS = os.path.join(WORK, "absent.json")
msg = raises(bake.serial_value, "fake-1.0")
expect(msg is not None and msg.startswith("serial needed for fake-1.0"), f"no serials file: {msg!r}")
bake.SERIALS = SERIALS

# the steps typed, logged and annotated
STEPS = [{"click": {"button": "Personalize"}}, {"serial": "fake-1.0", "field": "name"},
         {"key": "tab"}, {"serial": "fake-1.0"}, {"click": {"button": "OK"}}]


class FakeGuest:
    def __init__(self):
        self.typed, self.annotations, self.posts = [], [], []

    def type_text(self, text):
        self.typed.append(text)

    def annotate(self, text):
        self.annotations.append(text)

    def run(self, step):
        self.posts.append(json.dumps(step))


s = bake.Session.__new__(bake.Session)
logged = []
s.bake, s.steps, s.log, s.guest = {"name": "fake"}, STEPS, logged.append, FakeGuest()
s.run_steps()
expect(s.guest.typed == [NAME, SECRET], "typed: the name, then the serial")
expect(all(clean(t) for t in logged + s.guest.annotations + s.guest.posts),
       "no value in the log lines, the annotations or the other steps")

# the key: the definition only (the id), whatever the file holds
bake.base_identity = lambda os_name: "base"
bake.media_names = lambda b: []
definition = {"name": "fake-bake", "steps": STEPS}
k1 = bake.bake_key(definition)
with open(SERIALS) as f:
    d = json.load(f)
d["products"][0]["serial"] = "ZZ99-ZZ99"
with open(SERIALS, "w") as f:
    json.dump(d, f)
expect(bake.bake_key(definition) == k1, "the key doesn't change with the serial")
expect(clean(json.dumps(definition)), "the definition holds no value")

# bake.py NAME with a serial missing: exit status 3 before any guest boots
booted = []
bake.load_bake = lambda name: {"name": name, "steps": [{"serial": "fake-empty"}]}
bake.up_to_date = lambda b: False
bake.bake_run = lambda *a, **k: booted.append(a)
sys.argv = ["bake.py", "fake-bake"]
err = io.StringIO()
with contextlib.redirect_stderr(err):
    status = bake.main()
expect(status == bake.SERIAL_NEEDED == 3, f"exit status {status}")
expect("serial needed for fake-empty" in err.getvalue(), f"says so: {err.getvalue().strip()!r}")
expect(not booted, "no guest booted")

shutil.rmtree(WORK, ignore_errors=True)
print("PASS" if not failures else f"FAILED: {len(failures)}")
sys.exit(1 if failures else 0)
