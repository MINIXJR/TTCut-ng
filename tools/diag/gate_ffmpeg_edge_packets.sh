#!/usr/bin/env bash
# Gate: a partial audio packet at the recording edge is not warned about.
#
# A VDR recording ends in the middle of a PES packet, so ffmpeg's timestamp
# repair pass reports the trailing packet on essentially every recording:
#
#   PES packet size mismatch
#   Packet corrupt (stream = 1, dts = 5948676626).
#   corrupt input packet in stream 1
#
# Measured on nine fresh demuxes: seven carried these lines, all of them
# 0.3-0.8 s before the end of the recording; the two without ended on a packet
# boundary by chance. Same harmless edge as the partial audio frame, one layer
# up - see gate_audiofix_edge.sh.
#
# Only the middle line carries a position, so the decision is all-or-nothing:
# the group is silenced only when EVERY audio packet-corrupt line sits within
# the outer EDGE_WINDOW seconds. One line further in and everything is warned
# about as before, because the two context-less lines cannot be attributed.
#
# Verdicts (all against ffmpeg_audio_corruption_is_edge_only, sourced from
# ttcut-demux so the gate tests the shipping code, not a copy):
#   EDGE_END    a packet at the end of the recording -> edge only
#   EDGE_START  a packet at the start -> edge only
#   MIDDLE      a packet in the middle -> NOT edge only
#   MIXED       edge packets plus one in the middle -> NOT edge only
#   VIDEO       stream 0 is ignored here (it has corrupt_frame_ranges)
#   NONE        no packet-corrupt lines at all -> nothing to silence
#
# Usage: gate_ffmpeg_edge_packets.sh <path-to-ttcut-demux>
set -euo pipefail
DEMUX="${1:?usage: gate_ffmpeg_edge_packets.sh <path-to-ttcut-demux>}"

WORK=/usr/local/src/CLAUDE_TMP/TTCut-ng/ffmpeg_edge; mkdir -p "$WORK"
RC_ALL=0

# Pull the function under test out of the script, together with the constants
# it reads. Without them bash leaves the window empty, every case comes out
# "noedge", and the three cases that expect exactly that would pass for the
# wrong reason - so the presence of each is asserted rather than assumed.
grep -E '^readonly FFMPEG_(EDGE_WINDOW_S|PACKET_CORRUPT_PATTERN)=' "$DEMUX" > "$WORK/fn.sh"
sed -n '/^ffmpeg_audio_corruption_is_edge_only()/,/^}/p' "$DEMUX" >> "$WORK/fn.sh"
for need in FFMPEG_EDGE_WINDOW_S FFMPEG_PACKET_CORRUPT_PATTERN ffmpeg_audio_corruption_is_edge_only; do
  grep -q "$need" "$WORK/fn.sh" || { echo "VERDICT: FAIL ($need not found in $DEMUX)"; exit 1; }
done
# shellcheck disable=SC1090
. "$WORK/fn.sh"
[ -n "${FFMPEG_EDGE_WINDOW_S:-}" ] || { echo "VERDICT: FAIL (edge window empty after sourcing)"; exit 1; }

START=63027.837511          # a real recording's start_pts
DURATION=2100.098467        # and its container duration
tick() { python3 -c "print(int(($START + $1) * 90000))"; }

mklog() {  # mklog <file> <stream>:<offset-seconds> ...
  : > "$1"; local f=$1; shift
  for spec in "$@"; do
    local st=${spec%%:*} off=${spec#*:}
    { echo "[in#0/mpegts @ 0x1] PES packet size mismatch"
      echo "[in#0/mpegts @ 0x1] Packet corrupt (stream = $st, dts = $(tick "$off"))."
      echo "[in#0/mpegts @ 0x2] corrupt input packet in stream $st"; } >> "$f"
  done
}

check() {  # check <name> <expected: edge|noedge> <log>
  local name=$1 want=$2 log=$3
  set +e; ffmpeg_audio_corruption_is_edge_only "$log" "$START" "$DURATION"; local rc=$?; set -e
  local got=noedge; [ $rc -eq 0 ] && got=edge
  if [ "$got" = "$want" ]; then
    echo "$name: PASS ($got)"
  else
    echo "$name: FAIL (expected $want, got $got)"; RC_ALL=1
  fi
}

mklog "$WORK/end.log"    1:2099.6                    ; check EDGE_END   edge   "$WORK/end.log"
mklog "$WORK/start.log"  1:0.4                       ; check EDGE_START edge   "$WORK/start.log"
mklog "$WORK/mid.log"    1:1050.0                    ; check MIDDLE     noedge "$WORK/mid.log"
mklog "$WORK/mixed.log"  1:0.4 1:1050.0 1:2099.6     ; check MIXED      noedge "$WORK/mixed.log"
mklog "$WORK/video.log"  0:1050.0                    ; check VIDEO      noedge "$WORK/video.log"
: > "$WORK/none.log"; echo "frame= 100 size= 2048kB" >> "$WORK/none.log"
                                                       check NONE       noedge "$WORK/none.log"

[ $RC_ALL -eq 0 ] && echo "VERDICT: PASS" || echo "VERDICT: FAIL"
exit $RC_ALL
