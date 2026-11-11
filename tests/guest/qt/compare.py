#!/usr/bin/env python3
"""compare.py — QTTest's results against a reference, the movies' own
pixels and the sound the Mac played.

    compare.py --results R --expected E [--manifest M] [--audio A] [--known K] [--record]

* --expected: the reference (tests/guest/qt/expected_*.txt, a ROM
  machine's run): every result line, notes ("#") and the totals left out.
  A line that differs, is missing or is new is a failure. --record writes
  the results as the reference instead.
* --manifest: tests/lib/qtmovie.py's frame CRCs. The frames of the movies
  it wrote ('raw ' and 'rle ', exact codecs) and of QuickTime's own
  flattened copy must be those pixels, whatever the reference says.
* --known: result names or wildcards (one per line, "#" comments) known to differ
  here, left out of the comparison with the reference (not of the
  manifest's pixel check). A known result the reference passes must still
  pass here, its detail may differ; "NAME may-fail" lets it fail too.
* --same-frames A=B: QTTest --disk's movie A must draw what B draws (the
  same movie, A's header compressed: QuickTime 3's 'cmov'); each
  disk.A.NN line's detail against disk.B.NN's.
* --audio: the emulator's --audio-dump (S16LE, 48 kHz, stereo). QTTest
  plays a 1000 Hz tone for 2 s, a 440 Hz one for 1 s, then av.mov's
  1000 Hz for 2 s: both tones must be in it, about that long.
* --audio-heard A SECS: the same dump of QTTest --disk, which plays each
  movie for 2 s from its start: at least SECS seconds of it not silent.
"""

import argparse
import fnmatch
import struct
import sys

FRAME_MOVIES = {"raw32": "raw32.mov", "raw16": "raw16.mov", "rle32": "rle32.mov",
                "rle16": "rle16.mov", "av": "av.mov", "flat-raw32": "raw32.mov"}


def result_lines(path, encoding="mac_roman"):
    """The guest's file is MacRoman with CRs; a reference is UTF-8."""
    text = open(path, "rb").read().decode(encoding).replace("\r", "\n")
    lines = []
    for line in text.split("\n"):
        line = line.rstrip()
        if line == "---":
            break
        if line and not line.startswith("#"):
            lines.append(line)
    return lines


def by_name(lines):
    out = {}
    for line in lines:
        parts = line.split(" ", 2)
        out[parts[1] if len(parts) > 1 else line] = line
    return out


def compare(results, expected):
    errors = []
    got, want = by_name(results), by_name(expected)
    for name, line in want.items():
        if name not in got:
            errors.append("missing:  %s" % line)
        elif got[name] != line:
            errors.append("expected: %s\n  got:    %s" % (line, got[name]))
    for name, line in got.items():
        if name not in want:
            errors.append("new:      %s" % line)
    return errors


def check_manifest(results, manifest):
    crcs = {}
    for line in open(manifest):
        f, n, crc = line.split()
        crcs[(f, int(n))] = crc
    errors, checked = [], 0
    for line in results:
        parts = line.split(" ", 2)
        if len(parts) < 3 or not parts[1].startswith("frame.") or not parts[2].startswith("crc="):
            continue
        _, movie, n = parts[1].split(".")
        if movie not in FRAME_MOVIES:
            continue
        want = crcs.get((FRAME_MOVIES[movie], int(n)))
        checked += 1
        if parts[2][4:] != want:
            errors.append("pixels:   %s, the file's frame is crc=%s" % (line, want))
    return errors, checked


def check_same(results, pairs):
    got = by_name(results)
    errors, checked = [], 0
    for pair in pairs:
        a, b = pair.split("=")
        for name, line in got.items():
            if not name.startswith("disk.%s." % a) or name.endswith((".play", ".whole")):
                continue
            other = got.get("disk.%s.%s" % (b, name.split(".", 2)[2]))
            checked += 1
            if other is None or other.split(" ", 2)[2:] != line.split(" ", 2)[2:]:
                errors.append("same:     %s, but %s" % (line, other))
    return errors, checked


def tone_segments(path, rate=48000):
    """(frequency, seconds) of each stretch of steady tone: 50 ms windows
    with signal, joined while their frequencies (by zero crossings) agree
    within 5%; a stretch's frequency from all its crossings."""
    data = open(path, "rb").read()
    n = len(data) // 4
    left = struct.unpack("<%dh" % (n * 2), data[:n * 4])[0::2]
    win = rate // 20
    out = []                                    # [crossings, windows, last window's frequency]
    prev = None
    for w in range(0, n - win, win):
        s = left[w:w + win]
        if max(s) - min(s) < 2000:
            prev = None
            continue
        z = sum(1 for i in range(1, win) if (s[i - 1] < 0) != (s[i] < 0))
        f = z * rate / 2.0 / win
        if prev is not None and abs(prev - f) < prev * 0.05:
            out[-1][0] += z
            out[-1][1] += 1
        else:
            out.append([z, 1])
        prev = f
    return [(z * rate / 2.0 / (c * win), c / 20.0) for z, c in out if c >= 4]


def check_audio(path):
    segs = tone_segments(path)
    found = "; ".join("%.0f Hz %.2f s" % s for s in segs) or "no tone"

    def has(freq, secs):
        return any(abs(f - freq) < freq * 0.03 and d >= secs for f, d in segs)

    errors = []
    if sum(1 for f, d in segs if abs(f - 1000) < 30 and d >= 1.5) < 2:
        errors.append("sound:    want two 1000 Hz tones of 2 s; heard %s" % found)
    if not has(440, 0.7):
        errors.append("sound:    want a 440 Hz tone of 1 s; heard %s" % found)
    return errors, found


def heard_seconds(path, rate=48000):
    """The 50 ms windows with signal in the dump, in seconds, and the
    stretches of them (start, seconds)."""
    data = open(path, "rb").read()
    n = len(data) // 4
    left = struct.unpack("<%dh" % (n * 2), data[:n * 4])[0::2]
    win = rate // 20
    on = [max(left[w:w + win]) - min(left[w:w + win]) >= 512 for w in range(0, n - win, win)]
    stretches = []
    for i, v in enumerate(on):
        if v and (i == 0 or not on[i - 1]):
            stretches.append([i / 20.0, 0.0])
        if v:
            stretches[-1][1] += 0.05
    return sum(on) / 20.0, stretches


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", required=True)
    ap.add_argument("--expected", required=True)
    ap.add_argument("--manifest")
    ap.add_argument("--audio")
    ap.add_argument("--audio-heard", nargs=2, metavar=("DUMP", "SECS"))
    ap.add_argument("--known")
    ap.add_argument("--same-frames", action="append", default=[])
    ap.add_argument("--record", action="store_true")
    a = ap.parse_args()

    results = result_lines(a.results)
    errors = []
    if a.manifest:
        e, checked = check_manifest(results, a.manifest)
        print("frames against the movies' pixels: %d checked, %d differ" % (checked, len(e)))
        errors += e
    if a.same_frames:
        e, checked = check_same(results, a.same_frames)
        print("frames the same as another movie's: %d checked, %d differ" % (checked, len(e)))
        errors += e
    if a.audio:
        e, found = check_audio(a.audio)
        print("sound: %s" % found)
        errors += e
    if a.audio_heard:
        secs, stretches = heard_seconds(a.audio_heard[0])
        print("sound: %.2f s heard: %s" % (secs, "; ".join("%.2f s at %.2f" % (d, t) for t, d in stretches)))
        if secs < float(a.audio_heard[1]):
            errors.append("sound:    want %s s heard; heard %.2f s" % (a.audio_heard[1], secs))
    if a.record:
        with open(a.expected, "wb") as f:
            f.write(("\n".join(results) + "\n").encode("utf-8"))
        print("recorded %d lines in %s" % (len(results), a.expected))
    else:
        expected = result_lines(a.expected, "utf-8")
        if a.known:
            known = set()
            may_fail = set()
            for line in open(a.known, encoding="utf-8"):
                words = line.split("#", 1)[0].split()
                if words:
                    known.add(words[0])
                    if "may-fail" in words[1:]:
                        may_fail.add(words[0])
            name_of = lambda l: (l.split(" ", 2) + [""])[1]
            matches = lambda name, pats: any(fnmatch.fnmatchcase(name, k) for k in pats)
            skip = lambda lines: [l for l in lines if not matches(name_of(l), known)]
            left = len(results) - len(skip(results))
            print("known to differ here: %d names, %d of these results" % (len(known), left))
            status = {name_of(l): l.split(" ", 1)[0] for l in results}
            for l in expected:
                n = name_of(l)
                if (l.startswith("PASS ") and matches(n, known) and not matches(n, may_fail)
                        and status.get(n, "PASS") != "PASS"):
                    errors.append("known:    %s passes in the reference, %s here" % (n, status[n]))
            results, expected = skip(results), skip(expected)
        e = compare(results, expected)
        print("against %s: %d lines, %d differ" % (a.expected, len(results), len(e)))
        errors += e
    for e in errors:
        print("  " + e)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
