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

report = [
    "material() with no attributes matches plain color(): %s" % ("yes" if bare == plain else "NO"),
    "roughness changes the render: %s" % ("yes" if smooth != rough else "NO"),
    "roughness differs from the default: %s" % ("yes" if smooth != bare else "NO"),
    "metallic changes the render: %s" % ("yes" if metal != bare else "NO"),
]

with open(reportfile, "w") as f:
    f.write("\n".join(report) + "\n")
