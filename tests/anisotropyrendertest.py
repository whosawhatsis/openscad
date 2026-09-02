#!/usr/bin/env python3

# Anisotropic roughness shading test (feature 64).
#
# Like materialrendertest.py, the assertions are relationships between renders
# rather than a blessed image, so this cannot drift with the renderer's palette.
#
# The rotation pair renders identical geometry and differs only in whether the
# rotation reaches the anisotropy axis (material() outside vs inside the
# rotate()). The isotropic pair is the control: with no axis to move, those two
# must be pixel-identical, which is what makes a difference in the first pair
# attributable to the axis and nothing else.
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
remaining_args = remaining_args[1:-1]  # Passed on to the OpenSCAD executable

basename = os.path.splitext(reportfile)[0]


def failquit(message):
    print(message, file=sys.stderr)
    sys.exit(1)


if not os.path.exists(inputfile):
    failquit("cant find input file named: " + inputfile)
if not os.path.exists(args.openscad):
    failquit("cant find openscad executable named: " + args.openscad)


def render(mode):
    png = "%s-mode%d.png" % (basename, mode)
    cmd = ([args.openscad, inputfile, "-D", "mode=%d" % mode, "-o", png] + remaining_args)
    print("Running OpenSCAD:", " ".join(cmd), file=sys.stderr)
    sys.stderr.flush()
    if subprocess.call(cmd, stdout=sys.stderr) != 0:
        failquit("render failed for mode %d" % mode)
    with open(png, "rb") as f:
        data = f.read()
    os.unlink(png)
    return data


plain = render(0)            # color("red")
isotropic = render(1)        # roughness only, anisotropy never mentioned
explicit_zero = render(2)    # anisotropy = 0
along = render(3)            # anisotropy = 0.9
across = render(4)           # anisotropy = -0.9
outside = render(5)          # anisotropy 0.9, rotation below material(): axis untouched
inside = render(6)           # anisotropy 0.9, rotation above material(): axis moves
iso_outside = render(7)      # isotropic control for the same pair
iso_inside = render(8)

report = [
    # An explicit anisotropy of 0 is isotropic. If these differ, the anisotropic
    # path is not reducing exactly to the isotropic one and every existing
    # render has shifted.
    "anisotropy = 0 renders identically to no anisotropy: %s"
    % ("yes" if explicit_zero == isotropic else "NO"),
    # The feature does something at all.
    "anisotropy changes the render: %s" % ("yes" if along != isotropic else "NO"),
    # The sign is a real 90-degree rotation of the lobe, not a magnitude: the
    # two must not collapse onto each other.
    "the sign of anisotropy changes the render: %s" % ("yes" if along != across else "NO"),
    # The control. Rotating an isotropic sphere changes nothing, so the geometry
    # and the camera are not what makes the next assertion true.
    "the isotropic control pair is identical: %s"
    % ("yes" if iso_outside == iso_inside else "NO"),
    # The rotation semantics, and the reason the axis is stored rather than
    # derived from a global build direction in the shader. Same sphere, same
    # silhouette, different layer direction.
    "the anisotropy axis rotates with the part: %s"
    % ("yes" if outside != inside else "NO"),
    # Sanity: a plain color() is untouched by any of this.
    "plain color() is unaffected: %s" % ("yes" if plain != along else "NO"),
]

with open(reportfile, "w") as f:
    f.write("\n".join(report) + "\n")
