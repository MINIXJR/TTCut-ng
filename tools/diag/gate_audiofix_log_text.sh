#!/bin/bash
# gate_audiofix_log_text.sh - what ttcut-demux says about an audio track the
# sanitizer (ttcut-audiofix) reported on.
#
# The log used to print "junk removed (N bytes) at video frame(s) X" for every
# reported range. That was wrong twice for a track whose only finding is a
# frame with a bad checksum: nothing is removed at X (the frame stays as it
# is), and N was the size of the partial frames at the recording edges. The
# reader went looking for a cut that is not there.
#
# Usage: gate_audiofix_log_text.sh <path-to-ttcut-demux>
# Shell options as in ttcut-demux itself (set -e only): its range helper
# ends a pipeline with grep, which finds nothing for an empty list - under
# pipefail that would abort here although the script runs fine.
set -e
DEMUX="${1:?usage: gate_audiofix_log_text.sh <path-to-ttcut-demux>}"

WORK=/usr/local/src/CLAUDE_TMP/TTCut-ng/audiofix_log_text; mkdir -p "$WORK"
RC_ALL=0

# The function under test with everything it calls. warn/info are replaced by
# plain prefixes; each needed piece is asserted, not assumed.
{ grep -E '^RANGES_WRAP_(COLS|INDENT)=' "$DEMUX"
  sed -n '/^audiofix_ranges_to_video_frames()/,/^}/p' "$DEMUX"
  sed -n '/^format_ranges_hms()/,/^}/p' "$DEMUX"
  sed -n '/^audiofix_report()/,/^}/p' "$DEMUX"; } > "$WORK/fn.sh"
for need in RANGES_WRAP_COLS 'audiofix_ranges_to_video_frames()' 'format_ranges_hms()' 'audiofix_report()'; do
  grep -qF "$need" "$WORK/fn.sh" || { echo "VERDICT: FAIL ($need not found in $DEMUX)"; exit 1; }
done
warn() { echo "WARN:$1"; }
info() { echo "INFO:$1"; }
# shellcheck disable=SC1090
. "$WORK/fn.sh"

check() {  # check <name> <condition as a command> ...
  local name=$1; shift
  if "$@"; then echo "$name: PASS"; else echo "$name: FAIL"; RC_ALL=1; fi
}
has()   { grep -qF -- "$1" "$WORK/out.txt"; }
hasnt() { ! grep -qF -- "$1" "$WORK/out.txt"; }
lines() { [ "$(grep -c "^$1:" "$WORK/out.txt" || true)" = "$2" ]; }
run()   { audiofix_report "$@" > "$WORK/out.txt"; sed 's/^/    /' "$WORK/out.txt"; }

echo "-- edge only: no damage"
run a.ac3 "" "" 0 1300 25
check EDGE_ONE_INFO       lines INFO 1
check EDGE_NO_WARN        lines WARN 0
check EDGE_TEXT           has "structure OK (trimmed 1300 bytes of partial frame at the recording edges)"

echo "-- one frame with a bad checksum (frame 189153 at 6052896 ms, 25 fps)"
run a.ac3 "" "189153@6052896" 0 1300 25
check CRC_NAMES_CHECKSUM  has "bad checksum"
check CRC_NAMES_FRAME     has "151322-151322 (~1:40:52)"
check CRC_NO_JUNK_CLAIM   hasnt "junk removed"
check CRC_EDGE_SEPARATE   has "INFO:  a.ac3: trimmed 1300 bytes of partial frame at the recording edges"

echo "-- junk between frames (418 bytes at 160000 ms, 25 fps)"
run a.mp2 "5000@160000:418" "" 418 0 25
check JUNK_TEXT           has "junk removed (418 bytes) at video frame(s) 4000-4000 (~0:02:40)"
check JUNK_NO_CHECKSUM    hasnt "bad checksum"
check JUNK_ONE_LINE       lines WARN 1

echo "-- both, far apart: one line each, the edge bytes not in the junk total"
run a.ac3 "5000@160000:418" "189153@6052896" 418 1300 25
check BOTH_TWO_WARN       lines WARN 2
check BOTH_JUNK_RANGE     has "junk removed (418 bytes) at video frame(s) 4000-4000 (~0:02:40)"
check BOTH_CRC_RANGE      has "151322-151322 (~1:40:52)"

if [ $RC_ALL -eq 0 ]; then echo "VERDICT: PASS"; else echo "VERDICT: FAIL"; fi
exit $RC_ALL
