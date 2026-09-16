#!/usr/bin/env python3

# Transparent-surface tearing test.
#
# Renders one scene through both renderers - preview (OpenCSG) and rendered mesh
# (PolySet) - and compares how much high-frequency structure each produces
# inside the transparent solid. The preview path is the reference rather than a
# fixed number, so the assertion does not encode a resolution, a palette or a
# tessellation, and does not drift when any of them changes.
#
# Usage: <script> <inputfile> --openscad=<executable-path> [<openscad args>] <outputfile>

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngdecode import count_jumps  # noqa: E402

WIDTH = 400

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


def render(label, mode):
    png = "%s-%s.png" % (basename, label)
    cmd = [args.openscad, inputfile, mode, "-o", png] + remaining_args
    print("Running OpenSCAD:", " ".join(cmd), file=sys.stderr)
    sys.stderr.flush()
    if subprocess.call(cmd, stdout=sys.stderr) != 0:
        failquit("render failed for %s" % label)
    with open(png, "rb") as f:
        data = f.read()
    os.unlink(png)
    return data


preview = count_jumps(render("preview", "--preview"), WIDTH)
rendered = count_jumps(render("render", "--render"), WIDTH)
print("jumps: preview=%d render=%d" % (preview, rendered), file=sys.stderr)

report = [
    # Generous: the two renderers tessellate and shade differently, so an exact
    # match is not the claim. Torn output was measured at 14x the preview's
    # count, so anything near parity is unambiguous.
    "the rendered mesh composites transparency without tearing: %s"
    % ("yes" if rendered <= preview * 3 + 200 else "NO"),
]

with open(reportfile, "w") as f:
    f.write("\n".join(report) + "\n")
