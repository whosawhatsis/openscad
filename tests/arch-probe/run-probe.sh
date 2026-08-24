#!/usr/bin/env bash
# Architecture-divergence probe.
#
# Exports every model in models/ to ASCII STL (3D) at full precision and hashes
# the result. Run on two machines with different CPU architectures and diff the
# two report files: any model whose hash differs produces architecture-dependent
# geometry.
#
# This is a ground-truth check, not a proxy - it catches libm differences and
# anything else arch-specific, not only FMA contraction.
#
# Usage: run-probe.sh <path-to-openscad-binary> [output-report]
set -u

BIN="${1:?usage: run-probe.sh <openscad-binary> [report]}"
REPORT="${2:-arch-probe-$(uname -m)-$(uname -s).txt}"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

if command -v sha256sum >/dev/null; then HASH=sha256sum; else HASH="shasum -a 256"; fi

{
  echo "# arch-probe  machine=$(uname -m)  os=$(uname -s)"
  echo "# binary: $BIN"
} > "$REPORT"

fail=0
for f in "$HERE"/models/*.scad; do
  name="$(basename "$f" .scad)"
  if ! "$BIN" "$f" --export-format=asciistl -o "$OUT/$name.stl" >"$OUT/$name.log" 2>&1; then
    echo "ERROR    $name  (export failed; see log below)" >> "$REPORT"
    sed 's/^/         /' "$OUT/$name.log" >> "$REPORT"
    fail=1
    continue
  fi
  # Hash geometry only: strip the solid name line, which carries no coordinates.
  h=$(grep -v '^solid\|^endsolid' "$OUT/$name.stl" | $HASH | cut -d' ' -f1)
  v=$(grep -c 'vertex' "$OUT/$name.stl")
  printf '%-64s %s  vertices=%s\n' "$h" "$name" "$v" >> "$REPORT"
done

echo
echo "Report written to $REPORT"
cat "$REPORT"
exit $fail
