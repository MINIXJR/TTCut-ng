#!/usr/bin/env bash
# Gate: ttcut-audiofix separates a cut-off edge frame from real damage.
#
# A VDR recording starts and ends in the middle of an audio frame - the
# partial frame at either end is not a valid frame, so the sanitizer used to
# list it in junk_regions like any other damage. ttcut-demux turned that into
# audio_N_corrupt_ranges, and TTCut-ng showed it as a "Tonstoerungen:" marker
# on essentially every recording, always a few frames before the end (the
# audio ES ends earlier than the video by the padding amount).
#
# Measured on nine recordings: the junk was always exactly two regions -
# frame_idx 0 and frame_idx == total_frames - each smaller than one audio
# frame, with the bytes in between dividing evenly by the frame size. Never
# anything in the middle.
#
# Verdicts, each built from a clean source by cutting mid-frame the way VDR
# does:
#   EDGE     both edges reported as edge_junk_bytes, junk_regions empty,
#            exit code still 1 so ttcut-demux runs the fix
#   MIDDLE   damage inside the stream still lands in junk_regions
#   OVERSIZE trailing garbage larger than one frame is real damage, not an
#            edge - it stays in junk_regions
#
# Usage: gate_audiofix_edge.sh <ttcut-audiofix-bin> <clean-audio-file> <frame-size>
#   e.g. ... TEST_deu.ac3 1536   |   ... something.mp2 576
set -euo pipefail
BIN="${1:?usage: gate_audiofix_edge.sh <bin> <clean-audio> <frame-size>}"
SRC="${2:?}"
FS="${3:?}"

WORK=/usr/local/src/CLAUDE_TMP/TTCut-ng/audiofix_edge; mkdir -p "$WORK"
RC_ALL=0

field() { echo "$1" | grep -oP "^$2=\K.*" || true; }

# The source must exist and be clean, otherwise every verdict below is
# meaningless. Checking the exit code matters: a missing file also produces
# empty junk_regions, which would otherwise read as "clean".
[ -r "$SRC" ] || { echo "VERDICT: FAIL (source $SRC not readable)"; exit 1; }
set +e
BASE=$("$BIN" -a "$SRC" 2>/dev/null); BASE_RC=$?
set -e
if [ "$BASE_RC" -ne 0 ] || [ -n "$(field "$BASE" junk_regions)" ]; then
  echo "VERDICT: FAIL (source $SRC not clean: rc=$BASE_RC junk=$(field "$BASE" junk_regions))"
  exit 1
fi
echo "source clean: $SRC (frame size $FS)"

# --- EDGE: start 500 bytes into a frame, end 700 bytes into another --------
dd if="$SRC" of="$WORK/edge.bin" bs=1 skip=500 count=$((200*FS+700)) status=none
OUT=$("$BIN" -a "$WORK/edge.bin" 2>/dev/null) || true
ERC=$?; set +e; "$BIN" -a "$WORK/edge.bin" >/dev/null 2>&1; ERC=$?; set -e
JUNK=$(field "$OUT" junk_regions); EDGE=$(field "$OUT" edge_junk_bytes)
if [ -z "$JUNK" ] && [ -n "$EDGE" ] && [ "$EDGE" -gt 0 ] && [ "$ERC" -eq 1 ]; then
  echo "EDGE: PASS (edge_junk_bytes=$EDGE, junk_regions empty, rc=$ERC)"
else
  echo "EDGE: FAIL (junk_regions='$JUNK' edge_junk_bytes='$EDGE' rc=$ERC)"
  RC_ALL=1
fi

# --- MIDDLE: same file, one frame overwritten well inside the stream -------
cp "$WORK/edge.bin" "$WORK/middle.bin"
dd if=/dev/urandom of="$WORK/middle.bin" bs=1 seek=$((100*FS)) count="$FS" \
   conv=notrunc status=none
OUT=$("$BIN" -a "$WORK/middle.bin" 2>/dev/null) || true
JUNK=$(field "$OUT" junk_regions)
if [ -n "$JUNK" ]; then
  echo "MIDDLE: PASS (junk_regions=$JUNK)"
else
  echo "MIDDLE: FAIL (damage inside the stream was not reported)"
  RC_ALL=1
fi

# --- OVERSIZE: trailing garbage larger than one frame ----------------------
head -c $((200*FS)) "$SRC" > "$WORK/oversize.bin"   # frame-aligned, clean
head -c $((2*FS)) /dev/urandom >> "$WORK/oversize.bin"
OUT=$("$BIN" -a "$WORK/oversize.bin" 2>/dev/null) || true
JUNK=$(field "$OUT" junk_regions)
if [ -n "$JUNK" ]; then
  echo "OVERSIZE: PASS (junk_regions=$JUNK)"
else
  echo "OVERSIZE: FAIL (trailing garbage of $((2*FS)) bytes was treated as an edge)"
  RC_ALL=1
fi

[ $RC_ALL -eq 0 ] && echo "VERDICT: PASS" || echo "VERDICT: FAIL"
exit $RC_ALL
