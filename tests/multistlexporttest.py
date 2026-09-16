#!/usr/bin/env python3

# Multi-file STL export test.
#
# Exports one model with --multi-stl and reports, in its output text file, the files it
# produced: their names in the order the exporter wrote them, and the
# world-space bounding box of each. Bodies are never recentered, so the boxes
# double as the check that every file stays on the model's common origin.
#
# Then re-exports over those files twice, to show that the collision preflight
# refuses the whole batch without --overwrite and accepts it with.
#
# Usage: <script> <inputfile> --openscad=<executable-path> [<openscad args>] <outputfile>

import argparse
import glob
import os
import re
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--openscad", required=True, help="Specify OpenSCAD executable.")
args, remaining_args = parser.parse_known_args()
inputfile = remaining_args[0]
# The harness hands the tool the full path of the file its output is compared
# against; the STL files are written alongside it under the same stem.
reportfile = remaining_args[-1]
remaining_args = remaining_args[1:-1]  # Passed on to the OpenSCAD executable

basename = os.path.splitext(reportfile)[0]
stlfile = basename + ".stl"
pattern = basename + "*.stl"
report = []


def emit(*fields):
    report.append(" ".join(str(f) for f in fields))


def failquit(message):
    print(message, file=sys.stderr)
    sys.exit(1)


if not os.path.exists(inputfile):
    failquit("cant find input file named: " + inputfile)
if not os.path.exists(args.openscad):
    failquit("cant find openscad executable named: " + args.openscad)

for stale in glob.glob(pattern):
    os.unlink(stale)


def export(extra):
    cmd = [args.openscad, inputfile, "-o", stlfile, "--multi-stl"] + extra + remaining_args
    print("Running OpenSCAD:", file=sys.stderr)
    print(" ".join(cmd), file=sys.stderr)
    sys.stderr.flush()
    return subprocess.call(cmd, stdout=sys.stderr)


VERTEX = re.compile(r"\s*vertex\s+(\S+)\s+(\S+)\s+(\S+)")


def bounds(path):
    lo = [None] * 3
    hi = [None] * 3
    with open(path) as f:
        for line in f:
            m = VERTEX.match(line)
            if not m:
                continue
            for i in range(3):
                v = float(m.group(i + 1))
                lo[i] = v if lo[i] is None else min(lo[i], v)
                hi[i] = v if hi[i] is None else max(hi[i], v)
    if lo[0] is None:
        failquit("no vertices in " + path)
    fmt = lambda vs: "[" + ", ".join("%g" % round(v, 4) for v in vs) + "]"
    return fmt(lo) + " " + fmt(hi)


if export([]) != 0:
    failquit("multi-STL export failed")

# Listed alphabetically rather than in write order: the numeric discriminator is
# assigned in source order, so pairing each name with its bounding box pins the
# order down anyway, and does not depend on directory iteration.
produced = sorted(glob.glob(pattern))
stem = os.path.basename(basename)
for path in produced:
    # Only the generated part of the name is reported; the stem is a temporary
    # path chosen by the test harness.
    emit(os.path.basename(path)[len(stem):], bounds(path))

emit("files:", len(produced))

# The whole batch must be refused while any target exists, and nothing may be
# left half-written.
before = {p: os.path.getmtime(p) for p in produced}
emit("rerun without --overwrite:", "refused" if export([]) != 0 else "OVERWROTE")
if {p: os.path.getmtime(p) for p in glob.glob(pattern)} != before:
    failquit("refused export still modified its targets")
emit("rerun with --overwrite:", "accepted" if export(["--overwrite"]) == 0 else "REFUSED")
if sorted(glob.glob(pattern)) != produced:
    failquit("--overwrite changed the set of output files")

for path in glob.glob(pattern):
    os.unlink(path)

with open(reportfile, "w") as f:
    f.write("\n".join(report) + "\n")
