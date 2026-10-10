#!/bin/bash
# gate_donorfill_real.sh - the donor fill on a real recording.
#
# Runs the search of TTAudioRepair::findDonorFill at hole markers of a
# demuxed AC3 file, compares hole, shift, match and gains with given values
# and writes original/repaired excerpts to listen to.
#
#   usage: gate_donorfill_real.sh <file.ac3> <donor> <expected shift> <outdir> <case>...
#     expected shift  donor position minus AC3 position in samples, as the
#                     start offsets of the .info predict (0 when they are equal)
#     case            "<frameFrom>-<frameTo>=<holeStart>,<holeEnd>,<shift>,<match>,<gain>[,<gain>]"
#                     frames: the marker's; tolerances: hole edges 16 samples,
#                     shift 1 sample, match 0.005, gains 0.02
#   example (02x06, demuxed with the slot rule; values measured 2026-10-10
#   with the ffmpeg CLI, spec 2026-10-10 "Evidence"):
#     gate_donorfill_real.sh 02x06_deu.ac3 02x06_deu.mp2 0 out \
#       96868-96870=148791638,148792446,-413,0.999,1.460 \
#       108382-108384=166476750,166477586,-413,0.984,0.980,0.979
#
# Not in the run-gates.sh table: it needs a recording.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="$ROOT/tools/diag/test_donorfill"
[ $# -ge 5 ] || { sed -n '2,20p' "$0"; exit 2; }
[ -x "$BIN" ] || { echo "FAIL: $BIN missing - cmake --build build --target test_donorfill"; exit 1; }
AC3="$1"; DONOR="$2"; EXPECTED="$3"; OUT="$4"; shift 4
[ -f "$AC3" ] || { echo "FAIL: no such file: $AC3"; exit 1; }
[ -f "$DONOR" ] || { echo "FAIL: no such file: $DONOR"; exit 1; }
mkdir -p "$OUT"
RC=0
for CASE in "$@"; do
  RANGE="${CASE%%=*}"; WANT="${CASE#*=}"; FROM="${RANGE%-*}"; TO="${RANGE#*-}"
  RES=$(XDG_CACHE_HOME="$OUT/xdg" LC_ALL=C "$BIN" --probe "$AC3" "$FROM" "$TO" "$DONOR" "$EXPECTED" "$OUT/loch-$FROM" 2>&1)
  GOT=$(echo "$RES" | LC_ALL=C awk '$1=="HOLE"{h=$2","$3} $1=="FIT"{f=$2; for(i=3;i<=NF;i++) f=f","$i} END{print h","f}')
  VERDICT=$(LC_ALL=C awk -v got="$GOT" -v want="$WANT" 'BEGIN {
    n = split(got, g, ","); m = split(want, w, ",")
    if (n != m || n < 5) { print "number of values: " got; exit }
    tol[1] = 16; tol[2] = 16; tol[3] = 1; tol[4] = 0.005; for (i = 5; i <= n; i++) tol[i] = 0.02
    for (i = 1; i <= n; i++) { d = g[i] - w[i]; if (d < 0) d = -d; if (d > tol[i]) { print "value " i ": " g[i] " instead of " w[i]; exit } }
    print "ok" }')
  if [ "$VERDICT" = "ok" ]; then echo "PASS: frames $RANGE: $GOT"; else echo "FAIL: frames $RANGE: $VERDICT (expected $WANT)"; RC=1; fi
  echo "$RES" | sed 's/^/  /'
done
exit $RC
