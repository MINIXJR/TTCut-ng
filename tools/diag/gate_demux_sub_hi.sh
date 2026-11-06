#!/bin/bash
# gate_demux_sub_hi.sh - ttcut-demux passes the hearing-impaired flag of a DVB
# subtitle stream into the .info (sub_N_hearing_impaired=1), for every
# subtitle file made from that stream.
#
# subtitle_hi_indices reads the output of
#   ffprobe -select_streams s -show_entries stream=index:stream_disposition=hearing_impaired -of csv=p=0
# and prints the indices flagged hearing impaired. Measured 2026-11-06 on the
# ZDF Whitney Houston recording: ffprobe prints "5", an empty line and "5,1"
# for its one DVB subtitle - the lines without a comma must be ignored.
# A second check keeps the SUBTITLE_HI array parallel to SUBTITLE_FILES:
# every SUBTITLE_FILES+= site has a SUBTITLE_HI+= next to it.
#   usage: tools/diag/gate_demux_sub_hi.sh
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEMUX="$ROOT/tools/ttcut-demux/ttcut-demux"
WORK="${W:-/usr/local/src/CLAUDE_TMP/TTCut-ng/demux-sub-hi-tests}"
mkdir -p "$WORK"
PASS=0; FAIL=0

# Function definitions end with "}" in column 1; sourcing the whole script
# would run it.
extract_fn() {
    awk -v fn="$1" '$0 == fn"() {" {infn=1} infn {print} infn && /^}/ {exit}' "$DEMUX"
}
FN_FILE="$WORK/.fns.sh"
extract_fn subtitle_hi_indices > "$FN_FILE"
grep -q "^subtitle_hi_indices() {" "$FN_FILE" || { echo "FAIL: subtitle_hi_indices not found in $DEMUX"; exit 1; }
# shellcheck source=/dev/null
source "$FN_FILE"

check() {   # INPUT EXPECTED LABEL
    local got
    got=$(printf '%b' "$1" | subtitle_hi_indices 2>"$WORK/.err")
    if [ "$got" = "$2" ] && [ ! -s "$WORK/.err" ]; then echo "PASS: $3 -> '$got'"; PASS=$((PASS+1))
    else echo "FAIL: $3 -> '$got', want '$2'"; FAIL=$((FAIL+1)); fi
}
check '5\n\n5,1\n'       '5'    "ZDF Whitney Houston (measured)"
check '3,0\n4,1\n'       '4'    "two streams, the second flagged"
check '3,1\n4,1\n'       '3 4'  "two streams, both flagged"
check ''                 ''     "no subtitle stream"
check '3,0\n'            ''     "stream not flagged"

files=$(grep -c 'SUBTITLE_FILES+=(' "$DEMUX")
his=$(grep -c 'SUBTITLE_HI+=(' "$DEMUX")
if [ "$files" -gt 0 ] && [ "$files" = "$his" ]; then
    echo "PASS: $files SUBTITLE_FILES+= sites, each with SUBTITLE_HI+="; PASS=$((PASS+1))
else
    echo "FAIL: $files SUBTITLE_FILES+= sites but $his SUBTITLE_HI+= sites"; FAIL=$((FAIL+1))
fi
# shellcheck disable=SC2016  # the literal text of the .info line in ttcut-demux
if grep -qF 'sub_${i}_hearing_impaired=1' "$DEMUX"; then
    echo "PASS: .info writes sub_N_hearing_impaired=1"; PASS=$((PASS+1))
else
    echo "FAIL: .info line sub_N_hearing_impaired missing"; FAIL=$((FAIL+1))
fi

echo "---- $PASS PASS, $FAIL FAIL"
[ "$FAIL" -eq 0 ]
