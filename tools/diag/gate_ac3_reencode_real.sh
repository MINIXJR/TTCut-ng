#!/bin/bash
# gate_ac3_reencode_real.sh - re-encoded AC3 frames on a real recording.
#
# ffmpeg's AC3 encoder writes no dynamic range compression words, so the
# generated fixtures of gate ac3_reencode cannot show whether a repaired
# range keeps its level on a player that does not apply compression (mpv's
# default, and with it the application's preview). This runs the repair on
# 32 frames of a DVB recording and compares levels decoded without
# compression, the lag and the header fields.
#
#   usage: gate_ac3_reencode_real.sh <recording.ac3> [first-frame]
#
# Not in the run-gates.sh table: it needs a recording. Exit 77 when the
# chosen range carries no compression (pick another first-frame).
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="$ROOT/tools/diag/test_ac3_reencode"
WORK=/usr/local/src/CLAUDE_TMP/TTCut-ng/ac3-reencode-real
[ $# -ge 1 ] || { sed -n '2,14p' "$0"; exit 2; }
[ -x "$BIN" ] || { echo "FAIL: $BIN missing - cmake --build build --target test_ac3_reencode"; exit 1; }
[ -f "$1" ] || { echo "FAIL: no such file: $1"; exit 1; }
mkdir -p "$WORK"
XDG_CACHE_HOME="$WORK/xdg" "$BIN" --real "$1" "$WORK" ${2:+"$2"}
