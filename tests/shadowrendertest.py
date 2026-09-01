#!/usr/bin/env python3

# Self-shadowing test.
#
# Renders one scene twice, changing only whether shadows are enabled, and checks
# that the difference is a shadow rather than any other change. Relational, so
# it pins no palette, resolution or lighting model.
#
# The second assertion is the one with teeth: a shadow can only ever *remove*
# light. Any implementation that brightens the scene, or that darkens it
# uniformly rather than in a region, is wrong however plausible the picture
# looks.
#
# Usage: <script> <inputfile> --openscad=<executable-path> [<openscad args>] <outputfile>

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngdecode import pixels  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument("--openscad", required=True, help="Specify OpenSCAD executable.")
args, remaining_args = parser.parse_known_args()
inputfile = remaining_args[0]
reportfile = remaining_args[-1]
remaining_args = remaining_args[1:-1]  # Passed on to the OpenSCAD executable

basename = os.path.splitext(reportfile)[0]


def failquit(message):
    print(message, file=sys.stderr)
    sys.exit(1)


if not os.path.exists(inputfile):
    failquit("cant find input file named: " + inputfile)
if not os.path.exists(args.openscad):
    failquit("cant find openscad executable named: " + args.openscad)


def render(label, extra):
    png = "%s-%s.png" % (basename, label)
    cmd = [args.openscad, inputfile, "-o", png] + extra + remaining_args
    print("Running OpenSCAD:", " ".join(cmd), file=sys.stderr)
    sys.stderr.flush()
    if subprocess.call(cmd, stdout=sys.stderr) != 0:
        failquit("render failed for %s" % label)
    with open(png, "rb") as f:
        data = f.read()
    os.unlink(png)
    return pixels(data)


lit = render("lit", [])
shadowed = render("shadowed", ["--enable=shadows"])

darker = sum(1 for a, b in zip(shadowed, lit) if sum(b) - sum(a) > 24)
brighter = sum(1 for a, b in zip(shadowed, lit) if sum(a) - sum(b) > 24)
print("darker=%d brighter=%d" % (darker, brighter), file=sys.stderr)

report = [
    "shadows change the render: %s" % ("yes" if shadowed != lit else "NO"),
    # A region has to go dark, not a few stray pixels.
    "a region of the model is shadowed: %s" % ("yes" if darker > 800 else "NO"),
    # Shadows subtract light. Anything that gets brighter is not a shadow, so
    # the darkened area must dominate by a wide margin.
    "shadowing only removes light: %s"
    % ("yes" if darker > brighter * 10 else "NO"),
]

with open(reportfile, "w") as f:
    f.write("\n".join(report) + "\n")
