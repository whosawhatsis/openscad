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
import collections
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
# As a fraction of the model rather than an absolute count, so the assertion
# does not silently weaken if the image size or the camera changes.
background = collections.Counter(lit).most_common(1)[0][0]
model = sum(1 for p in lit if p != background)
print("darker=%d brighter=%d model=%d (%.1f%%)"
      % (darker, brighter, model, 100.0 * darker / max(model, 1)), file=sys.stderr)

report = [
    "shadows change the render: %s" % ("yes" if shadowed != lit else "NO"),
    # A wall this size throws a shadow across a good part of the plate, so
    # require a real proportion of the model to darken. An earlier version of
    # this asked only for 800 pixels and passed against an implementation whose
    # shadow map was unreadable - the few pixels it did darken were an artifact,
    # not a shadow. Measured: 1.3% of the model in that broken state against 15%
    # when it works.
    "a region of the model is shadowed: %s"
    % ("yes" if darker > 0.05 * model else "NO"),
    # Shadows subtract light. Anything that gets brighter is not a shadow, so
    # the darkened area must dominate by a wide margin.
    "shadowing only removes light: %s"
    % ("yes" if darker > brighter * 10 else "NO"),
]

with open(reportfile, "w") as f:
    f.write("\n".join(report) + "\n")
