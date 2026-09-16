#!/usr/bin/env python3

# Angle-based smooth shading test (feature 57).
#
# Renders the same geometry twice in shaded mode, changing only $fa. $fn is fixed,
# so the mesh is byte-identical between runs and any pixel difference comes from the
# smoothing tolerance alone, which is 2 * $fa.
#
#   $fn = 24  ->  15 degree facets
#   $fa = 12  ->  24 degree tolerance  ->  smoothed
#   $fa = 1   ->   2 degree tolerance  ->  left faceted
#
# Asserted as a relationship between renders rather than against a blessed image, so
# it cannot drift with the palette or the lighting model.
#
# Usage: <script> <inputfile> --openscad=<executable-path> [<openscad args>] <outputfile>

import argparse
import os
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--openscad", required=True, help="Specify OpenSCAD executable.")
args, remaining_args = parser.parse_known_args()
inputfile = remaining_args[0]
reportfile = remaining_args[-1]
remaining_args = remaining_args[1:-1]

basename = os.path.splitext(reportfile)[0]


def failquit(message):
    print(message, file=sys.stderr)
    sys.exit(1)


if not os.path.exists(inputfile):
    failquit("cant find input file named: " + inputfile)
if not os.path.exists(args.openscad):
    failquit("cant find openscad executable named: " + args.openscad)


def render(fa):
    png = "%s-fa%s.png" % (basename, fa)
    cmd = [args.openscad, inputfile, "-D", "fa=%s" % fa, "-o", png] + remaining_args
    print("Running OpenSCAD:", " ".join(cmd), file=sys.stderr)
    sys.stderr.flush()
    if subprocess.call(cmd, stdout=sys.stderr) != 0:
        failquit("render failed for $fa = %s" % fa)
    with open(png, "rb") as f:
        data = f.read()
    os.unlink(png)
    return data


smoothed = render(12)
faceted = render(1)

report = [
    "$fa changes the shading of identical geometry: %s"
    % ("yes" if smoothed != faceted else "NO"),
]

with open(reportfile, "w") as f:
    f.write("\n".join(report) + "\n")
