#!/usr/bin/env python3

# Minimal PNG reader shared by the shading tests.
#
# Decoded with zlib rather than PIL, which this suite cannot assume is
# installed. Lives here rather than in each test script because there is more
# than one shading test now and a second copy of a filter-reconstruction loop is
# the kind of thing that silently drifts.

import struct
import zlib


def pixels(png):
    """[(r, g, b), ...] in scanline order. Raises ValueError on anything that is
    not 8-bit RGB or RGBA, rather than quietly producing nonsense."""
    pos, idat, width, bpp = 8, b"", 0, 0
    while pos < len(png):
        length, = struct.unpack(">I", png[pos:pos + 4])
        ctype = png[pos + 4:pos + 8]
        data = png[pos + 8:pos + 8 + length]
        if ctype == b"IHDR":
            width, _h, depth, color = struct.unpack(">IIBB", data[:10])
            if depth != 8 or color not in (2, 6):
                raise ValueError("expected 8-bit RGB/RGBA png, got depth %d color %d" % (depth, color))
            bpp = 3 if color == 2 else 4
        elif ctype == b"IDAT":
            idat += data
        pos += length + 12

    raw = zlib.decompress(idat)
    stride = width * bpp
    prev = bytearray(stride)
    out = []
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
            out.append((line[i], line[i + 1], line[i + 2]))
        prev = line
    return out


def channels(png, pick=min):
    """Per-pixel reduction over the color channels, in scanline order. `min` is
    how close each pixel gets to white, which is what a saturating highlight
    does; `max` is how bright it gets in any one channel, which is what a
    colored emission does - an emissive red part glows red, so it never moves
    the min."""
    return [pick(r, g, b) for r, g, b in pixels(png)]


def gain(a, b, pick=min):
    """How much more than b the most-gaining pixel of a gets, under `pick`. The
    two renders share a camera and a background, so the background cancels and
    only the material's own response is measured."""
    return max(x - y for x, y in zip(channels(a, pick), channels(b, pick)))


def count_hue(png, channel, margin=40):
    """Pixels where one channel leads both others by `margin`. A crude hue test,
    but it is measuring whether a strongly saturated color appears somewhere it
    otherwise could not, which does not need better than crude."""
    n = 0
    for px in pixels(png):
        lead = px[channel]
        if all(lead > px[other] + margin for other in range(3) if other != channel):
            n += 1
    return n
