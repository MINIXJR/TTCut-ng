#!/bin/bash
# gate_ac3fix.sh — output gate for ttcut-ac3fix across a refactor.
#
# Captures stdout, stderr, exit code and the written AC3 files of a fixed set
# of cases per tagged binary; two tags are then compared byte for byte.
# Established for the process_ac3_file split (code audit 2026-09-03, commit
# 34c300e1); the original lived in CLAUDE_TMP and was moved here so that it
# survives a temp purge.
#
#   tools/diag/gate_ac3fix.sh run <tag> [binary]     capture into WORK/<tag>
#   tools/diag/gate_ac3fix.sh compare <tagA> <tagB>  diff -r, PASS/FAIL
#
# Typical use: `run ref` on the pre-change binary, rebuild, `run new`,
# `compare ref new`.  The binary defaults to the one in the source tree
# (tools/ttcut-ac3fix/ttcut-ac3fix, built by `cmake --build build`).
#
# Fixtures are generated once into WORK/fixtures with ffmpeg and reused by
# every later run, so both tags see identical input bytes:
#   stereo448.ac3  8 s stereo, 448 kbit/s
#   s51.ac3        4 s 5.1,    448 kbit/s
#   mixed.ac3      stereo448 + s51 + stereo448 + the four bytes "junk"
#                  (stereo<->5.1 transitions at constant frame size, plus a
#                  truncated tail so the walker's end-of-file path is hit)
#   empty.ac3      zero bytes
# plus the Tux AC3 track from tools/test-videos/cache.  Delete WORK/fixtures
# to regenerate; do not do that between `run ref` and `run new`.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK=/usr/local/src/CLAUDE_TMP/TTCut-ng/ac3fix-gate
FIX="$WORK/fixtures"
TUX="$ROOT/tools/test-videos/cache/tux_h264_1080p_progressive_test.ac3"

make_fixtures() {
  [ -f "$FIX/mixed.ac3" ] && return 0
  mkdir -p "$FIX"
  ffmpeg -y -v error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=8" \
      -ac 2 -c:a ac3 -b:a 448k "$FIX/stereo448.ac3" || return 1
  ffmpeg -y -v error -f lavfi -i "sine=frequency=330:sample_rate=48000:duration=4" \
      -af "pan=5.1|FL=c0|FR=c0|FC=c0|LFE=c0|BL=c0|BR=c0" \
      -c:a ac3 -b:a 448k "$FIX/s51.ac3" || return 1
  { cat "$FIX/stereo448.ac3" "$FIX/s51.ac3" "$FIX/stereo448.ac3"; printf 'junk'; } \
      > "$FIX/mixed.ac3"
  : > "$FIX/empty.ac3"
}

do_run() {
  local tag=$1 bin=${2:-$ROOT/tools/ttcut-ac3fix/ttcut-ac3fix} D
  [ -x "$bin" ] || { echo "FAIL: binary missing ($bin) — cmake --build build"; exit 1; }
  [ -f "$TUX" ] || { echo "FAIL: Tux AC3 missing ($TUX) — tools/test-videos/make_test_video.sh"; exit 1; }
  make_fixtures || { echo "FAIL: fixture generation"; exit 1; }
  D="$WORK/$tag"; rm -rf "$D"; mkdir -p "$D"
  run() { local name=$1; shift
          "$bin" "$@" > "$D/$name.out" 2> "$D/$name.err"; echo "rc=$?" >> "$D/$name.out"; }
  run analyze_mixed  -a "$FIX/mixed.ac3"
  run analyze_tux    -a "$TUX"
  run fix_mixed      -F -f "$FIX/mixed.ac3" "$D/mixed_fixed.ac3"
  run fix_mixed_sv   -F -s -v -f "$FIX/mixed.ac3" "$D/mixed_fixed_sv.ac3"
  run fix_tux_b192   -F -b 192 -s -f "$TUX" "$D/tux_fixed.ac3"
  run copy_tux       -f "$TUX" "$D/tux_copy.ac3"
  run missing_input  -a "$FIX/does_not_exist.ac3"
  run empty_input    -a "$FIX/empty.ac3"
  echo "captured $(ls "$D" | wc -l) files in $D"
}

do_compare() {
  local a="$WORK/$1" b="$WORK/$2"
  [ -d "$a" ] && [ -d "$b" ] || { echo "FAIL: run both tags first ($a, $b)"; exit 1; }
  # The .out files name the output directory, which differs per tag by design.
  if diff -r -I "^Output: " "$a" "$b"; then echo "PASS: $1 == $2"; else echo "FAIL: $1 != $2"; exit 1; fi
}

case "${1:-}" in
  run)     [ $# -ge 2 ] || { sed -n '2,20p' "$0"; exit 1; }; do_run "$2" "${3:-}" ;;
  compare) [ $# -eq 3 ] || { sed -n '2,20p' "$0"; exit 1; }; do_compare "$2" "$3" ;;
  *)       sed -n '2,20p' "$0"; exit 1 ;;
esac
