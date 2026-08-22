#!/usr/bin/env python3

import hashlib
import itertools
import subprocess
import sys
import tempfile
from pathlib import Path


def render(openscad: str, source: str, output: Path) -> bytes:
    subprocess.run(
        [
            openscad,
            "--imgsize=512,512",
            "--autocenter",
            "--viewall",
            "-o",
            str(output),
            source,
        ],
        check=True,
    )
    return output.read_bytes()


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} OPENSCAD MODEL_SCAD", file=sys.stderr)
        return 2

    statements = [line for line in Path(sys.argv[2]).read_text().splitlines() if line.strip()]
    if len(statements) != 3:
        print(f"expected exactly 3 non-empty statements, got {len(statements)}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        results = {}
        for index, permutation in enumerate(itertools.permutations(statements)):
            source = tmpdir / f"order-{index}.scad"
            source.write_text("\n".join(permutation) + "\n")
            image = render(sys.argv[1], str(source), tmpdir / f"order-{index}.png")
            results[index] = hashlib.sha256(image).hexdigest()

    hashes = set(results.values())
    if len(hashes) == 1:
        return 0

    print(f"transparent product rendering produced {len(hashes)} images for 6 source orders:", file=sys.stderr)
    for index, digest in results.items():
        print(f"  order-{index}: {digest}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
