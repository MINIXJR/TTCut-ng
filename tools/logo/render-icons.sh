#!/bin/bash
# render-icons.sh - writes every logo file from tools/logo/gen-logo.py:
#   ttcut-ng.svg                       large drawing (scalable icon, 48 px and up)
#   ttcut-ng-small-{16,22,32}.svg      small drawing, one outline set per size
#   icons/<n>/ttcut-ng.png             16 22 24 32 from the small drawing
#                                      (24 = the 22 px render with 1 px padding),
#                                      48 64 128 256 512 from the large one
#
#   usage: render-icons.sh [<outdir>]    (default: ui/pixmaps of this repository)
#
# Needs python3, rsvg-convert and magick. tools/diag/gate_logo.sh compares the
# output with the committed files (SVGs by bytes, PNGs by pixels); the PNGs are
# written without time chunks so that a rerun leaves git clean.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT=${1:-"$ROOT/ui/pixmaps"}
GEN="$ROOT/tools/logo/gen-logo.py"; TUX="$ROOT/ui/pixmaps/Tux.svg"
mkdir -p "$OUT"

python3 -I "$GEN" --profile large --tux "$TUX" --out "$OUT/ttcut-ng.svg"
for s in 16 22 32; do
  python3 -I "$GEN" --profile small --size "$s" --tux "$TUX" --out "$OUT/ttcut-ng-small-$s.svg"
done

png() { # <svg> <size> <out>
  mkdir -p "$(dirname "$3")"
  rsvg-convert -w "$2" -h "$2" "$1" | magick - -define png:exclude-chunk=date,time "$3"
}
for s in 16 22 32; do png "$OUT/ttcut-ng-small-$s.svg" "$s" "$OUT/icons/$s/ttcut-ng.png"; done
mkdir -p "$OUT/icons/24"
magick "$OUT/icons/22/ttcut-ng.png" -background none -gravity center -extent 24x24 \
  -define png:exclude-chunk=date,time "$OUT/icons/24/ttcut-ng.png"
for s in 48 64 128 256 512; do png "$OUT/ttcut-ng.svg" "$s" "$OUT/icons/$s/ttcut-ng.png"; done
