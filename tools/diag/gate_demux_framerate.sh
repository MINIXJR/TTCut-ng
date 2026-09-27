#!/bin/bash
# gate_demux_framerate.sh - the .info frame-rate decision of ttcut-demux
# (video_frame_rate): TS r_frame_rate, halved for interlaced material when
# it is twice the TS avg_frame_rate. Cases: the TS values measured on the
# corpus (2026-09-27) plus 29.97i, calculated - the old comparison value
# (the ES avg_frame_rate, always the raw demuxer default 25) would have
# left 29.97i PAFF at 60000/1001.
#   usage: tools/diag/gate_demux_framerate.sh
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEMUX="$ROOT/tools/ttcut-demux/ttcut-demux"
WORK="${W:-/usr/local/src/CLAUDE_TMP/TTCut-ng/demux-framerate-tests}"
mkdir -p "$WORK"
PASS=0; FAIL=0

# Function definitions end with "}" in column 1; sourcing the whole script
# would run it.
extract_fn() {
    awk -v fn="$1" '$0 == fn"() {" {infn=1} infn {print} infn && /^}/ {exit}' "$DEMUX"
}
FN_FILE="$WORK/.fns.sh"
{ extract_fn _parse_fraction
  extract_fn video_frame_rate; } > "$FN_FILE"
grep -q "^video_frame_rate() {" "$FN_FILE" || { echo "FAIL: video_frame_rate not found in $DEMUX"; exit 1; }
# shellcheck source=/dev/null
source "$FN_FILE"

check() {   # R AVG FIELD_ORDER EXPECTED LABEL - result and a silent stderr
    local got err
    got=$(video_frame_rate "$1" "$2" "$3" 2>"$WORK/.err")
    err=$(cat "$WORK/.err")
    if [ "$got" = "$4" ] && [ -z "$err" ]; then echo "PASS: $5: $1 / $2 / $3 -> $got"; PASS=$((PASS+1))
    else echo "FAIL: $5: $1 / $2 / $3 -> $got, want $4${err:+ (stderr: $err)}"; FAIL=$((FAIL+1)); fi
}
check 25/1       25/1       tt          25/1       "ServusTV 1080i25 MBAFF (measured)"
check 50/1       25/1       tt          25/1       "DF1 1080i25 PAFF (measured)"
check 50/1       50/1       progressive 50/1       "Das Erste 720p50 (measured)"
check 25/1       25/1       tt          25/1       "MPEG-2 576i25 (measured)"
check 25/1       25/1       progressive 25/1       "MPEG-2 576p25 (measured)"
check 60000/1001 30000/1001 tt          30000/1001 "29.97i PAFF (calculated)"
check 30000/1001 30000/1001 tt          30000/1001 "29.97i MBAFF (calculated)"
check 50/1       25/1       unknown     50/1       "field order unknown: unchanged"
check 25/1       ""         tt          25/1       "no avg_frame_rate: unchanged"
check 50/1       ""         tt          50/1       "no avg_frame_rate, field rate: unchanged, no awk error"
check 50/1       25/1       ""          50/1       "no field order: unchanged"
echo "---- $PASS PASS, $FAIL FAIL"
[ "$FAIL" -eq 0 ]
