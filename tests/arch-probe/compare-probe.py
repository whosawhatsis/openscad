#!/usr/bin/env python3
"""Compare two arch-probe vertex dumps with a tolerance.

Usage:  compare-probe.py <dump-a.json> <dump-b.json> [tolerance]

Reports, per model, the one-sided Hausdorff distance from A's vertices to B's -
the furthest any vertex in A sits from the nearest vertex in B. That is
independent of export order, which is not stable for minkowski and hull, and
immune to the quantisation-boundary artifacts that make a bare hash unreliable.

Verdicts:
  STRUCTURAL  vertex counts differ - a segment count changed, the whole curve
              was re-sampled. This is the openscad/openscad#6161 signature.
  DIVERGENT   same count but vertices moved by more than the tolerance.
  noise       within tolerance; ordinary floating-point difference.
"""
import json
import math
import sys

DEFAULT_TOL = 1e-9


def hausdorff(a, b, cell=0.5):
    grid = {}
    for p in b:
        grid.setdefault((int(p[0] // cell), int(p[1] // cell), int(p[2] // cell)), []).append(p)
    worst = 0.0
    for p in a:
        key = (int(p[0] // cell), int(p[1] // cell), int(p[2] // cell))
        cand, r = [], 0
        while not cand and r < 64:
            for dx in range(-r, r + 1):
                for dy in range(-r, r + 1):
                    for dz in range(-r, r + 1):
                        if r and max(abs(dx), abs(dy), abs(dz)) != r:
                            continue
                        cand += grid.get((key[0] + dx, key[1] + dy, key[2] + dz), [])
            r += 1
        if cand:
            worst = max(worst, min(math.dist(p, q) for q in cand))
    return worst


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    a = json.load(open(sys.argv[1]))
    b = json.load(open(sys.argv[2]))
    tol = float(sys.argv[3]) if len(sys.argv) > 3 else DEFAULT_TOL

    print("%-26s %8s %8s %-12s %s" % ("model", "A", "B", "deviation", "verdict"))
    diverged = 0
    for name in sorted(set(a) | set(b)):
        if name not in a or name not in b:
            print("%-26s %8s %8s %-12s MISSING from one side" % (name, len(a.get(name, [])), len(b.get(name, [])), "-"))
            diverged += 1
            continue
        va, vb = [tuple(v) for v in a[name]], [tuple(v) for v in b[name]]
        if len(va) != len(vb):
            print("%-26s %8d %8d %-12s STRUCTURAL" % (name, len(va), len(vb), "-"))
            diverged += 1
            continue
        d = max(hausdorff(va, vb), hausdorff(vb, va))
        verdict = "noise" if d <= tol else "DIVERGENT"
        if d > tol:
            diverged += 1
        print("%-26s %8d %8d %-12.3g %s" % (name, len(va), len(vb), d, verdict))

    print("\n%d of %d models diverge (tolerance %g)" % (diverged, len(set(a) | set(b)), tol))
    return 1 if diverged else 0


if __name__ == "__main__":
    sys.exit(main())
