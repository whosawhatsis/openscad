#!/usr/bin/env python3

# Screen-space reflection test.
#
# Renders the same scene twice, changing only whether the reflections feature is
# enabled, and reports what the floor picked up. Relational like
# materialrendertest.py: no blessed image, so it cannot drift with the palette.
#
# The metric is a red-hue count over the whole frame. The object is identical in
# both renders and so contributes equally; the stand-in environment contains no
# red at all and a metallic floor has no diffuse term to tint. Any increase in
# red pixels is therefore the reflection and nothing else.
#
# Usage: <script> <inputfile> --openscad=<executable-path> [<openscad args>] <outputfile>

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngdecode import count_hue  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument("--openscad", required=True, help="Specify OpenSCAD executable.")
args, remaining_args = parser.parse_known_args()
inputfile = remaining_args[0]
reportfile = remaining_args[-1]
remaining_args = remaining_args[1:-1]  # Passed on to the OpenSCAD executable

basename = os.path.splitext(reportfile)[0]

RED = 0


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
    return data


plain = render("off", [])
reflected = render("on", ["--enable=screen-space-reflections"])

red_off = count_hue(plain, RED)
red_on = count_hue(reflected, RED)
print("red pixels: off=%d on=%d" % (red_off, red_on), file=sys.stderr)

report = [
    "reflections change the render: %s" % ("yes" if reflected != plain else "NO"),
    # The floor can only get red from the object. A bare "more than zero" would
    # pass on a few stray edge pixels, so require the reflection to be a real
    # feature of the image rather than a rounding artifact.
    "the floor reflects the object: %s"
    % ("yes" if red_on > red_off * 1.2 + 200 else "NO"),
]

with open(reportfile, "w") as f:
    f.write("\n".join(report) + "\n")
