#!/bin/bash
# Record how ttcut-demux's progress percentage moves over a run.
#
# ttcut-demux writes a single 0..100 for the running recording to
# "$OUTDIR/.$NAME.progress" for a supervising wrapper to poll. This samples
# that file once a second alongside the last log line, so a run can be read
# back phase by phase: where the bar moves, and where it stands still.
#
# That is what the fixed marks in ttcut-demux are spaced by - see the band
# model comment there. Re-run this after touching them.
#
# MEASURE ON AN IDLE MACHINE. A parallel transcode stretched a 63 s run to
# 242 s and shifted the phases against each other by up to a factor of eight,
# which is enough to derive the marks wrongly. Check with
# `ps aux --sort=-%cpu | head` before starting.
#
# Usage: measure_demux_progress.sh <first-ts-file> [outdir]
#   Writes progress_trace.csv (seconds;percent;log line) into the CURRENT
#   directory and removes the demuxed output again - only the trace is kept.

set -u
SRC=${1:?usage: measure_demux_progress.sh <first-ts-file> [outdir]}
OUT=${2:-/tmp/measure_demux_progress}
NAME=MessLauf
TRACE=$PWD/progress_trace.csv
DEMUX=$(command -v ttcut-demux || echo "$(dirname "$0")/../ttcut-demux/ttcut-demux")

mkdir -p "$OUT"
: > "$TRACE"
T0=$(date +%s)
"$DEMUX" --no-subs -n "$NAME" "$SRC" "$OUT" > "$OUT/measure_demux.log" 2>&1 &
PID=$!
while kill -0 $PID 2>/dev/null; do
    P=$(cat "$OUT/.$NAME.progress" 2>/dev/null || echo "-")
    L=$(tail -1 "$OUT/measure_demux.log" 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | cut -c1-70)
    echo "$(( $(date +%s) - T0 ));$P;$L" >> "$TRACE"
    sleep 1
done
wait $PID; RC=$?
echo "ENDE nach $(( $(date +%s) - T0 ))s, rc=$RC" >> "$TRACE"
rm -rf "$OUT"

# Phase view: one line per change of the percentage.
# NF>1 skips the trailing ENDE line, which carries no semicolons.
awk -F';' 'NF>1 && $2!=last {print $1"s -> "$2"%  | "$3; last=$2}' "$TRACE"
tail -1 "$TRACE"
