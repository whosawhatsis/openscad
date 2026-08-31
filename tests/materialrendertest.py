#!/usr/bin/env python3

# material() shading test.
#
# Renders one model several times in shaded mode, changing only the material
# attributes, and reports which renders differ from which. The assertions are
# relationships between renders rather than a blessed image, so this does not
# need an expected PNG and cannot drift with the renderer's palette.
#
# Two things are pinned:
#   - roughness and metallic visibly change the render,
#   - a material() with no attributes renders pixel-identically to a plain
#     color(), which is the guarantee that extending the shared shader did not
#     disturb anything that does not use the new attributes.
#
# Usage: <script> <inputfile> --openscad=<executable-path> [<openscad args>] <outputfile>

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngdecode import gain  # noqa: E402

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


plain = render(0)          # color("red")
bare = render(1)           # material() with no attributes
smooth = render(2)         # roughness 0.05
rough = render(3)          # roughness 0.9
metal = render(4)          # metallic 1
mirror = render(5)         # roughness 0
emissive = render(6)       # emission 0.5
dense = render(7)          # ior 2.5
matte = render(8)          # specular 0

report = [
    "material() with no attributes matches plain color(): %s" % ("yes" if bare == plain else "NO"),
    "roughness changes the render: %s" % ("yes" if smooth != rough else "NO"),
    "roughness differs from the default: %s" % ("yes" if smooth != bare else "NO"),
    "metallic changes the render: %s" % ("yes" if metal != bare else "NO"),
    # A normalized microfacet lobe concentrates a smooth surface's reflection
    # into a highlight core that saturates to white, where Blinn-Phong's fixed
    # 0.35 highlight weight cannot exceed 89/255 whatever the exponent. Pins the
    # BRDF, not merely that roughness changes something.
    "a smooth surface has a white highlight core: %s"
    % ("yes" if gain(smooth, rough) > 150 else "NO"),
    # Zero is a meaningful roughness - a mirror - so it must not be read as "the model
    # set nothing". It was, because zero doubled as the not-set sentinel all the way
    # from the node to the shader, and a mirror silently rendered as the default finish.
    "roughness = 0 is a mirror rather than the default: %s"
    % ("yes" if mirror != bare else "NO"),
    # specular, ior and emission were parsed, advertised in the call tips and
    # handed to the USD and POV exporters, but the viewport shader read only
    # roughness and metallic - so all three were accepted and silently ignored
    # on screen. Emission is checked for direction as well as difference: it is
    # light the surface adds, so it can only brighten.
    "emission brightens the render: %s"
    % ("yes" if emissive != bare and gain(emissive, bare, max) > 20 else "NO"),
    "ior changes the render: %s" % ("yes" if dense != bare else "NO"),
    "specular changes the render: %s" % ("yes" if matte != bare else "NO"),
]

with open(reportfile, "w") as f:
    f.write("\n".join(report) + "\n")
