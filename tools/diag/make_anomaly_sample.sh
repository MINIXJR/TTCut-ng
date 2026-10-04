#!/bin/bash
# Synthetic 5.1 AC3 with an injected anomaly at t=30 s, 1.2 s long by default:
# LFE silent everywhere except a 60 Hz pulse, C carries a square burst.
# Mirrors the ProSieben 02x06 real case (see corpus BESCHREIBUNG.md).
#
# Total duration is 150s (not 60s): TTAudioAnomalyScanTask::evaluate()'s
# Vorbedingung requires the LFE to be "null" (below anomalyLfeRmsDb) in
# >= anomalyLfeNullPercent (99.0) of 5.1 frames, or the whole track is
# reported unsuitable with no finding at all (Task 6, deliberate hard
# gate, no fallback). The 1.2s burst is only 98.05% null at 60s total
# (58.8/60) - below the 99% gate, so the scanner never fires. At 150s
# total it is 99.2% null (148.8/150), clearing the gate with margin.
#
#   usage: make_anomaly_sample.sh [out.ac3] [burst seconds]
# A burst of 1.2 s is longer than the scan's limit (no finding); 0.2 s gives one.
set -e
OUT="${1:-/usr/local/src/CLAUDE_TMP/TTCut-ng/anomaly_sample.ac3}"
BURST="${2:-1.2}"                       # seconds; the scan reports only bursts up to 0.48 s
END=$(awk "BEGIN { printf \"%.3f\", 30 + $BURST }")
mkdir -p "$(dirname "$OUT")"
ffmpeg -y -v error -f lavfi -i "aevalsrc=exprs=\
0.3*sin(2*PI*440*t)|\
0.3*sin(2*PI*550*t)|\
0.4*sin(2*PI*330*t)+0.6*sgn(sin(2*PI*900*t))*between(t\,30\,$END)|\
0.5*sin(2*PI*60*t)*between(t\,30\,$END)|\
0.1*sin(2*PI*660*t)|\
0.1*sin(2*PI*770*t):channel_layout=5.1(side):sample_rate=48000:duration=150" \
  -c:a ac3 -b:a 384k "$OUT"
echo "$OUT"
