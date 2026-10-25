#!/bin/bash
# gate_logo.sh - the committed logo files match tools/logo/gen-logo.py, and the
# drawing keeps the rules the user set on 2026-10-25: the two finger rings
# never touch each other and never touch the film. Self-verdicting.
#
#   usage: gate_logo.sh <repo root> <workdir>
#
# Needs python3, rsvg-convert (librsvg2-bin) and magick (ImageMagick 7);
# exits 77 (SKIP) when one is missing.
set -u
ROOT=${1:?usage: $0 <repo root> <workdir>}
W=${2:?usage: $0 <repo root> <workdir>}
for t in python3 rsvg-convert magick; do
  command -v "$t" >/dev/null || { echo "SKIP: $t not installed"; exit 77; }
done
ROOT=$(readlink -f "$ROOT"); mkdir -p "$W" && cd "$W" || exit 1
GEN="$ROOT/tools/logo/gen-logo.py"; TUX="$ROOT/ui/pixmaps/Tux.svg"
SIZES="16 22 24 32 48 64 128 256 512"
PASS=0; FAIL=0
# qt_matches_rsvg: pixels (of 512x512) whose Qt and librsvg colour differ by
# more than 10 %. Measured 2026-10-25 (Qt 6.11.2, librsvg 2.63.3): 5.26 / 5.45 /
# 6.01 / 6.12 for large / small-32 / -22 / -16 -> limit = 1.5 x max. Regressions
# measured against it: Tux.svg clip-paths kept 80.6, feComponentTransfer shadow
# 476.8, nested <svg> + both (commit 80b1029c) 1702. RMSE did not separate the
# clip-path case (0.0134 vs 0.0089), the size-native renders (16-32 px) are
# dominated by anti-aliasing noise - hence one large render for every file.
QT_SIZE=512
QT_FUZZ=10%
QT_AE_MAX=9.2
ok()  { echo "PASS: $1"; PASS=$((PASS+1)); }
bad() { echo "FAIL: $1"; FAIL=$((FAIL+1)); }

# white pixels of a render, as a mask; extra args go to magick (e.g. a dilation)
mask() { local in=$1 out=$2; shift 2; magick "$in" -alpha extract -threshold 50% "$@" "$out"; }
# number of white pixels two masks share
common() { magick "$1" "$2" -compose Multiply -composite -format '%[fx:round(mean*w*h)]' info:; }
gen() { python3 -I "$GEN" "$@" --tux "$TUX"; }

# ---- assets_current: render-icons.sh reproduces every committed file ----
# SVGs come from Python alone and are compared byte for byte. PNGs are compared
# pixel for pixel (decoded, alpha included, no tolerance): a librsvg or
# ImageMagick update may pack the same picture into different bytes - a real
# change of the drawing (anti-aliasing included) still fails.
px_equal() { [ "$(magick compare -metric AE "$1" "$2" null: 2>&1 | cut -d' ' -f1)" = 0 ]; }
rm -rf out && "$ROOT/tools/logo/render-icons.sh" "$W/out" >render.log 2>&1 \
  || { bad "assets_current: render-icons.sh failed ($(tail -1 render.log))"; }
stale=""
for f in ttcut-ng.svg ttcut-ng-small-16.svg ttcut-ng-small-22.svg ttcut-ng-small-32.svg; do
  cmp -s "out/$f" "$ROOT/ui/pixmaps/$f" || stale="$stale $f"
done
for n in $SIZES; do
  px_equal "out/icons/$n/ttcut-ng.png" "$ROOT/ui/pixmaps/icons/$n/ttcut-ng.png" || stale="$stale icons/$n"
done
[ -z "$stale" ] && ok "assets_current" || bad "assets_current: differ from the generator:$stale"

# ---- png_sizes ----
wrong=""
for n in $SIZES; do
  got=$(magick identify -format '%wx%h' "$ROOT/ui/pixmaps/icons/$n/ttcut-ng.png" 2>/dev/null)
  [ "$got" = "${n}x$n" ] || wrong="$wrong $n:${got:-missing}"
done
[ -z "$wrong" ] && ok "png_sizes" || bad "png_sizes:$wrong"

# ---- metadata: Tux attribution in every SVG ----
miss=""
for f in ttcut-ng.svg ttcut-ng-small-16.svg ttcut-ng-small-22.svg ttcut-ng-small-32.svg; do
  for s in 'lewing@isc.tamu.edu' 'The GIMP' 'Simon Budig' 'Garrett LeSage'; do
    grep -qF "$s" "$ROOT/ui/pixmaps/$f" 2>/dev/null || miss="$miss $f:'$s'"
  done
done
[ -z "$miss" ] && ok "metadata" || bad "metadata: missing$miss"

# ---- rings_apart / rings_off_film ----
for v in "large" "small 16" "small 22" "small 32"; do
  set -- $v; name=${1}${2:-}; args="--profile $1${2:+ --size $2}"
  gen $args --part rings --out "r-$name.svg" && gen $args --part film --out "f-$name.svg" \
    || { bad "rings ($name): generator failed"; continue; }
  rsvg-convert -w 512 -h 512 "r-$name.svg" -o "r-$name.png"
  rsvg-convert -w 512 -h 512 "f-$name.svg" -o "f-$name.png"
  # two separate white objects = the rings do not touch
  objs=$(mask "r-$name.png" - | magick - -define connected-components:verbose=true \
         -connected-components 8 null: 2>/dev/null | grep -c 'gray(255)\|srgb(255,255,255)')
  [ "$objs" = 2 ] && ok "rings_apart ($name)" || bad "rings_apart ($name): $objs white objects, expected 2"
  # large: still apart with both masks grown by 10 px (spike: gap 22-24 px);
  # small: not measured in the spike - the rule is "no contact", so no growth
  if [ "$1" = large ]; then grow=(-morphology Dilate Disk:10); else grow=(); fi
  mask "r-$name.png" "rm-$name.png" "${grow[@]}"; mask "f-$name.png" "fm-$name.png" "${grow[@]}"
  n=$(common "rm-$name.png" "fm-$name.png")
  [ "$n" = 0 ] && ok "rings_off_film ($name)" || bad "rings_off_film ($name): $n shared pixels"
done

# ---- qt_matches_rsvg: Qt (svg icon engine, as Plasma uses it) draws what librsvg draws ----
QTR="$ROOT/tools/diag/test_logo_render"
if [ ! -x "$QTR" ]; then
  bad "qt_matches_rsvg: $QTR not built (cmake --build build --target test_logo_render)"
else
  for f in ttcut-ng.svg ttcut-ng-small-32.svg ttcut-ng-small-22.svg ttcut-ng-small-16.svg; do
    svg="$ROOT/ui/pixmaps/$f"; b=${f%.svg}
    rsvg-convert -w $QT_SIZE -h $QT_SIZE "$svg" -o "rsvg-$b.png"
    QT_QPA_PLATFORM=offscreen "$QTR" "$svg" $QT_SIZE "qt-$b.png" >"qt-$b.log" 2>&1 \
      || { bad "qt_matches_rsvg ($b): test_logo_render failed ($(tail -1 "qt-$b.log"))"; continue; }
    ae=$(magick compare -fuzz $QT_FUZZ -metric AE "qt-$b.png" "rsvg-$b.png" null: 2>&1 | cut -d' ' -f1)
    awk -v n="$ae" -v m="$QT_AE_MAX" 'BEGIN { exit !(n ~ /^[0-9.e+-]+$/ && n+0 <= m+0) }' \
      && ok "qt_matches_rsvg ($b): $ae px" || bad "qt_matches_rsvg ($b): ${ae:-?} px > $QT_AE_MAX"
  done
fi

echo "gate_logo: $PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
