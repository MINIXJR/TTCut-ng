#!/bin/bash
# Synthetic AC3 for the stop search of TTAudioAnomalyScanTask:
#   10.003 s  the sound stops in mid-wave, silence until 13 s      -> finding
#   20.000 s  a hole of 17 ms                                      -> finding
#   30.000 s  decay of 60 dB over 100 ms, silence until 33 s       -> nothing
#   40.000 s  the sound stops, silence up to the end (42 s)        -> nothing
#             (that is what ttcut-demux's end padding looks like)
#   usage: make_stop_sample.sh <out.ac3> <5.1|stereo>
#
# The exponent of the decay is clamped with max(): pow(10, -3*(t-30)/0.1)
# overflows to infinity before t = 19.7 s, and 0 * inf is NaN - the track
# would be silent up to there.
set -e
OUT="${1:?usage: make_stop_sample.sh <out.ac3> <5.1|stereo>}"
MODE="${2:-5.1}"
mkdir -p "$(dirname "$OUT")"
G="(lt(t\,10.003)+gte(t\,13)*lt(t\,20)+gte(t\,20.017)*lt(t\,30)+gte(t\,30)*lt(t\,30.1)*pow(10\,-3*max(t-30\,0)/0.1)+gte(t\,33)*lt(t\,40))"
if [ "$MODE" = "5.1" ]; then
  EXPR="0.3*sin(2*PI*440*t)*$G|0.3*sin(2*PI*550*t)*$G|0.3*sin(2*PI*330*t)*$G|0|0.3*sin(2*PI*660*t)*$G|0.3*sin(2*PI*770*t)*$G"
  LAYOUT="5.1(side)"; RATE=448k
else
  EXPR="0.3*sin(2*PI*440*t)*$G|0.3*sin(2*PI*550*t)*$G"
  LAYOUT="stereo"; RATE=192k
fi
ffmpeg -y -v error -f lavfi -i "aevalsrc=exprs=$EXPR:channel_layout=$LAYOUT:sample_rate=48000:duration=42" -c:a ac3 -b:a $RATE "$OUT"
echo "$OUT"
