#!/usr/bin/env python3
"""Architecture-divergence probe for OpenSCAD geometry.

Exports every model in models/ to ASCII STL and prints one canonical hash per
model. Run on two machines of different CPU architectures, from the same commit,
and diff the reports: a differing hash means that model's geometry depends on the
architecture the binary runs on.

The hash is canonical in three deliberate ways, each of which removes a source of
false positives found the hard way:

  * Facet normals are ignored. They are derived with cross products, diverge at
    the 1e-16 level for free, and every consumer recomputes them anyway.
  * Vertices are sorted. Export order is not stable for minkowski/hull, and an
    order-sensitive comparison reports a whole-model difference for geometry that
    is in fact identical.
  * Coordinates are quantised to QUANTUM before hashing, so ordinary
    floating-point noise (~1e-15) does not register. Real divergence in this
    class flips an integer segment count and moves vertices by ~1e-1, which is
    eight orders of magnitude clear of the threshold.

Usage:  run-probe.py <openscad-binary> [report-path]
"""
import hashlib
import pathlib
import platform
import subprocess
import sys
import tempfile

QUANTUM = 1e-6  # see module docstring


def canonical_hash(stl_path):
    verts = []
    with open(stl_path) as fh:
        for line in fh:
            parts = line.split()
            if parts and parts[0] == "vertex":
                verts.append(tuple(round(float(x) / QUANTUM) for x in parts[1:4]))
    verts.sort()
    digest = hashlib.sha256()
    for v in verts:
        digest.update(("%d %d %d\n" % v).encode())
    return digest.hexdigest(), len(verts)


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: run-probe.py <openscad-binary> [report-path]")
    binary = sys.argv[1]
    here = pathlib.Path(__file__).resolve().parent
    report_path = pathlib.Path(
        sys.argv[2] if len(sys.argv) > 2 else "arch-probe-%s-%s.txt" % (platform.machine(), platform.system())
    )

    lines = [
        "# arch-probe  machine=%s  os=%s" % (platform.machine(), platform.system()),
        "# binary: %s" % binary,
        "# quantum: %g  (vertices sorted; normals ignored)" % QUANTUM,
    ]
    failed = False
    with tempfile.TemporaryDirectory() as tmp:
        for model in sorted((here / "models").glob("*.scad")):
            out = pathlib.Path(tmp) / (model.stem + ".stl")
            proc = subprocess.run(
                [binary, str(model), "--export-format=asciistl", "-o", str(out)],
                capture_output=True, text=True,
            )
            if proc.returncode != 0 or not out.exists():
                lines.append("ERROR    %s" % model.stem)
                lines.extend("         " + ln for ln in (proc.stderr or "").splitlines())
                failed = True
                continue
            digest, count = canonical_hash(out)
            lines.append("%s  %-26s vertices=%d" % (digest, model.stem, count))

    text = "\n".join(lines) + "\n"
    report_path.write_text(text)
    print(text, end="")
    print("Report written to %s" % report_path, file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
