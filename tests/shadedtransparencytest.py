#!/usr/bin/env python3

# Shaded-mode transparency test.
#
# Pins one invariant of the shaded viewport: a surface at alpha 0 is still
# visible, because the highlight is reflected light rather than the material's
# own color and must not fade out with it. Phong.frag premultiplies only the
# material contribution and leaves the specular term unattenuated, and
# GLView.cc pairs that with glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break
# either half and a fully transparent object disappears completely.
#
# Relational, not a blessed image, and that is the point. The image this
# replaced was re-blessed by two different feature branches for two different
# legitimate shading changes, so their integration branch carried one branch's
# image with the other's shader and the test failed for a reason that was
# nobody's defect. What the scene is actually for does not depend on the
# lighting model at all.
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


def render(label, scad):
    path = "%s-%s.scad" % (basename, label)
    png = "%s-%s.png" % (basename, label)
    with open(path, "w") as f:
        f.write(scad)
    cmd = [args.openscad, path, "-o", png] + remaining_args
    print("Running OpenSCAD:", " ".join(cmd), file=sys.stderr)
    sys.stderr.flush()
    if subprocess.call(cmd, stdout=sys.stderr) != 0:
        failquit("render failed for %s" % label)
    with open(png, "rb") as f:
        data = f.read()
    os.unlink(png)
    os.unlink(path)
    return pixels(data)


with open(inputfile) as f:
    scene = f.read()

transparent = render("transparent", scene)
# Same camera, no geometry: everything here is background, so it is what the
# transparent render must NOT look like.
empty = render("empty", "// deliberately empty\n")

differing = sum(1 for a, b in zip(transparent, empty) if max(abs(a[i] - b[i]) for i in range(3)) > 8)
# The brightest thing in frame must be on the object, not the background.
peak_transparent = max(sum(p) for p in transparent)
peak_empty = max(sum(p) for p in empty)

print("differing=%d peak=%d empty_peak=%d" % (differing, peak_transparent, peak_empty), file=sys.stderr)

report = [
    # A fully transparent object that renders identically to an empty scene has
    # lost its highlight - which is what dropping the premultiplication
    # convention, or blending with GL_SRC_ALPHA, does.
    "a fully transparent surface is still visible: %s"
    % ("yes" if differing > 500 else "NO"),
    # Visible is not enough: it has to be visible as a *highlight*, brighter
    # than anything the background offers, not as a faint tint.
    "its highlight is the brightest thing in frame: %s"
    % ("yes" if peak_transparent > peak_empty else "NO"),
]

with open(reportfile, "w") as f:
    f.write("\n".join(report) + "\n")
