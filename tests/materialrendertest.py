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


def brightest_white(png):
    """Max over pixels of the smallest channel: how close the whitest pixel gets
    to white. Decoded with zlib rather than PIL, which this suite cannot assume."""
    import struct
    import zlib

    pos, idat, width, bpp = 8, b"", 0, 0
    while pos < len(png):
        length, = struct.unpack(">I", png[pos:pos + 4])
        ctype = png[pos + 4:pos + 8]
        data = png[pos + 8:pos + 8 + length]
        if ctype == b"IHDR":
            width, _h, depth, color = struct.unpack(">IIBB", data[:10])
            if depth != 8 or color not in (2, 6):
                failquit("expected 8-bit RGB/RGBA png, got depth %d color %d" % (depth, color))
            bpp = 3 if color == 2 else 4
        elif ctype == b"IDAT":
            idat += data
        pos += length + 12

    raw = zlib.decompress(idat)
    stride = width * bpp
    prev = bytearray(stride)
    best = 0
    for row in range(0, len(raw), stride + 1):
        f = raw[row]
        line = bytearray(raw[row + 1:row + 1 + stride])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 0xFF
            elif f == 2: line[i] = (line[i] + b) & 0xFF
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        for i in range(0, stride, bpp):
            best = max(best, min(line[i], line[i + 1], line[i + 2]))
        prev = line
    return best


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
    # A normalized microfacet lobe concentrates a smooth surface's reflection
    # into a highlight core that saturates to white. Blinn-Phong's fixed 0.35
    # highlight weight cannot reach this, so it pins the BRDF, not just that
    # roughness does something.
    "a smooth surface has a white highlight core: %s"
    % ("yes" if brightest_white(smooth) > 200 else "NO"),
]

with open(reportfile, "w") as f:
    f.write("\n".join(report) + "\n")
