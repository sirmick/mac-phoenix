#!/usr/bin/env python3
"""Compare two analyzed runs' hwunique.tsv: accesses (pc, rw, address) only
one machine makes, and those whose first value differs.
  hwdiff.py DIR_A DIR_B [--until SECONDS]"""
import sys
def load(d, until):
    out = {}
    for line in open(d + "/hwunique.tsv"):
        if line.startswith("#"):
            continue
        t, pc, rt, rw, addr, reg, v0, v1, n = line.rstrip("\n").split("\t")
        if float(t) > until:
            continue
        out[(pc, rw, addr)] = (float(t), rt, reg, v0, v1, n)
    return out
a, b = sys.argv[1], sys.argv[2]
until = float(sys.argv[sys.argv.index("--until") + 1]) if "--until" in sys.argv else 1e9
A, B = load(a, until), load(b, until)
rows = []
for k in set(A) | set(B):
    x, y = A.get(k), B.get(k)
    if x and y:
        if x[3] != y[3]:
            rows.append((min(x[0], y[0]), "DIFF", k, x, y))
    else:
        rows.append(((x or y)[0], "only " + (a if x else b), k, x, y))
for t, kind, (pc, rw, addr), x, y in sorted(rows):
    e = x or y
    print("%.6f\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s" % (t, kind, pc, e[1], rw, addr, e[2],
          "%s/%s n=%s" % (x[3], x[4], x[5]) if x else "-", "%s/%s n=%s" % (y[3], y[4], y[5]) if y else "-"))
