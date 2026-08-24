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
    floating-point noise (~1e-15) does not register.

The hash is only a screen, and a deliberately imperfect one: a coordinate
sitting on a quantisation boundary flips for a 1e-15 perturbation, so a
differing hash with an unchanged vertex count may still be noise. The probe
therefore also writes a full sorted vertex dump next to the report; feed two
dumps to compare-probe.py for a tolerance-based verdict that does not care
about boundaries. Trust the comparison, not the hash.

Usage:  run-probe.py <openscad-binary> [report-path]
"""
import hashlib
import json
import pathlib
import platform
import subprocess
import sys
import tempfile

QUANTUM = 1e-6  # see module docstring


def read_vertices(stl_path):
    verts = []
    with open(stl_path) as fh:
        for line in fh:
            parts = line.split()
            if parts and parts[0] == "vertex":
                verts.append(tuple(float(x) for x in parts[1:4]))
    verts.sort()
    return verts


def canonical_hash(verts):
    digest = hashlib.sha256()
    for v in verts:
        digest.update(("%d %d %d\n" % tuple(round(c / QUANTUM) for c in v)).encode())
    return digest.hexdigest()


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
    dump = {}
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
            verts = read_vertices(out)
            dump[model.stem] = verts
            lines.append("%s  %-26s vertices=%d" % (canonical_hash(verts), model.stem, len(verts)))

    text = "\n".join(lines) + "\n"
    report_path.write_text(text)
    dump_path = report_path.with_suffix(report_path.suffix + ".dump.json")
    dump_path.write_text(json.dumps(dump))
    print(text, end="")
    print("Report written to %s" % report_path, file=sys.stderr)
    print("Vertex dump written to %s (feed two of these to compare-probe.py)" % dump_path,
          file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
