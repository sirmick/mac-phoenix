#!/usr/bin/env python3
"""make_mpw_disk.py — an HFS disk with Apple's 68k MPW from E.T.O. #22.

The 7.5.5 image's MPW-PR has MPW Shell and the compilers but no Link and no
libraries. This copies MPW (Shell, Tools, Scripts) and Interfaces&Libraries
off the E.T.O. #22 CD image (pinned in provisioning/media.json as
eto22.toast: ~/storage/eto/eto22.toast) onto a fresh HFS volume named "MPW", which Executor
mounts beside the System's image. tests/guest/ot/ builds OTTest with it.

Usage: tools/mpw/make_mpw_disk.py ETO_TOAST OUT_IMAGE [SIZE_MB]
       (ETO_TOAST: ~/storage/eto/eto22.toast)
"""
import os
import re
import subprocess
import sys
import tempfile

# Big folders the build does not need.
SKIP = {
    ":MPW:Examples", ":MPW:MPW Goodies", ":MPW:Release Notes", ":MPW:ROM Maps",
    ":Interfaces&Libraries:DebuggingLibraries",
}
TREES = [":MPW", ":Interfaces&Libraries"]
LS = re.compile(r'^([dfF])\s+(\S+/\S+\s+)?.*?\s\w{3}\s+\d+\s+(?:\d{4}|\d\d:\d\d)\s(.*)$')


def run(env, *args):
    # HFS names are MacRoman; hand them to hfsutils as such.
    args = [a.encode("mac_roman") if isinstance(a, str) else a for a in args]
    r = subprocess.run(args, env=env, capture_output=True)
    if r.returncode:
        sys.exit("%s: %s" % (b" ".join(args).decode("mac_roman"), r.stderr.decode("mac_roman")))
    return r.stdout.decode("mac_roman")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    toast, out = sys.argv[1], sys.argv[2]
    size_mb = int(sys.argv[3]) if len(sys.argv) > 3 else 120
    # hfsutils keeps its current volume in $HOME; use a private one.
    home = tempfile.mkdtemp(prefix="mpwdisk-hfs.")
    env = dict(os.environ, HOME=home)
    stage = tempfile.mkdtemp(prefix="mpwdisk-stage.")

    # 1. The CD's trees out as MacBinary files; dirs listed in creation order.
    dirs, files = [], []
    run(env, "hmount", toast)

    def walk(hdir):
        dirs.append(hdir)
        for line in run(env, "hls", "-l", "-a", hdir).splitlines():
            m = LS.match(line)
            if not m:
                continue
            path = hdir + ":" + m.group(3)
            if m.group(1) == "d":
                if path not in SKIP:
                    walk(path)
                continue
            local = os.path.join(stage, "%d.bin" % len(files))
            run(env, "hcopy", "-m", path, local)
            files.append((path, local))

    for t in TREES:
        walk(t)
    run(env, "humount")

    # 2. A fresh volume, the same tree on it.
    with open(out, "wb") as f:
        f.truncate(size_mb * 1024 * 1024)
    run(env, "hformat", "-l", "MPW", out)
    run(env, "hmount", out)
    for d in dirs:
        run(env, "hmkdir", d)
    for path, local in files:
        folder, name = path.rsplit(":", 1)
        run(env, "hcopy", "-m", local, folder + ":")
        # hcopy -m names the file from the MacBinary header; nothing to do.
    run(env, "humount")
    subprocess.run(["rm", "-rf", home, stage])
    print("%s: %d folders, %d files" % (out, len(dirs), len(files)))


if __name__ == "__main__":
    main()
