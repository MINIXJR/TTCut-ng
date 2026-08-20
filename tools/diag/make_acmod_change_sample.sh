#!/bin/bash
# Synthetic AC3 with a channel-mode (acmod) change in the middle: a 5.1
# section followed by a 2.0 stereo section, concatenated as raw elementary
# streams - exactly the structure a DVB recording shows at an ad break.
#
# Regression fixture for the Task-4 Critical: TTAudioRepair::buildRepairTable()
# must ABORT when a repair range spans such a transition instead of silently
# up/downmixing the changed frames against the range's first-frame layout.
# The case was originally established on a corpus recording (ProSieben 02x06,
# transition at AC3 frame 64057); that file is not part of the repository and
# is gone from the development machine, which left the regression test dead
# (SKIP, but the harness still printed ALL PASS). Hence this fixture.
#
# BOTH sections are encoded at 384 kbit/s on purpose: at 48 kHz that is
# exactly 1536 bytes per frame for either channel mode, so the frame SIZE
# stays constant across the transition. Otherwise buildRepairTable's
# constant-bitrate check would fire first and the test would never reach the
# channel-mode check it is written for.
set -e
OUT="${1:-/usr/local/src/CLAUDE_TMP/TTCut-ng/acmod_change_sample.ac3}"
mkdir -p "$(dirname "$OUT")"
TMPDIR_LOCAL="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_LOCAL"' EXIT

# Part 1: 2 s of 5.1 (acmod 7 + LFE)
ffmpeg -y -v error -f lavfi -i "aevalsrc=exprs=\
0.3*sin(2*PI*440*t)|\
0.3*sin(2*PI*550*t)|\
0.4*sin(2*PI*330*t)|\
0.0|\
0.1*sin(2*PI*660*t)|\
0.1*sin(2*PI*770*t):channel_layout=5.1(side):sample_rate=48000:duration=2" \
  -c:a ac3 -b:a 384k "$TMPDIR_LOCAL/part1.ac3"

# Part 2: 1 s of 2.0 stereo (acmod 2)
ffmpeg -y -v error -f lavfi -i "aevalsrc=exprs=\
0.3*sin(2*PI*440*t)|\
0.3*sin(2*PI*550*t):channel_layout=stereo:sample_rate=48000:duration=1" \
  -c:a ac3 -b:a 384k "$TMPDIR_LOCAL/part2.ac3"

cat "$TMPDIR_LOCAL/part1.ac3" "$TMPDIR_LOCAL/part2.ac3" > "$OUT"
echo "$OUT"
