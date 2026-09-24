#!/bin/bash
# gate_mpeg2_framerate.sh — MPEG-2 frame rates beyond 24/25/30 fps.
#
# Makes a one-second MPEG-2 ES for each of the eight frame_rate_codes of
# ISO 13818-2 table 6-4 with ffmpeg and checks the rate TTCut-ng reads
# (test_mpeg2_framerate) against the exact value (at most 0.001 fps off).
# Before 2026-09-24 codes 1, 4, 6, 7 and 8 came back as 25 fps.
#
#   tools/diag/gate_mpeg2_framerate.sh <workdir>
set -u
D="$(cd "$(dirname "$0")" && pwd)"
W=${1:?usage: $0 <workdir>}
BIN="$D/test_mpeg2_framerate"
[ -x "$BIN" ] || { echo "SKIP: not built: $BIN"; exit 77; }
command -v ffmpeg >/dev/null || { echo "SKIP: ffmpeg missing"; exit 77; }
mkdir -p "$W"
export LC_ALL=C

ok=1
#     code  ffmpeg rate   exact value
for spec in "1 24000/1001 23.976024" "2 24 24" "3 25 25" "4 30000/1001 29.970030" \
            "5 30 30" "6 50 50" "7 60000/1001 59.940060" "8 60 60"; do
  set -- $spec
  code=$1 rate=$2 want=$3
  es="$W/code$code.m2v"
  ffmpeg -y -v error -f lavfi -i "testsrc2=size=320x240:rate=$rate" -t 1 \
      -c:v mpeg2video -f mpeg2video "$es" \
      || { echo "FAIL: code $code: ffmpeg could not make the ES"; ok=0; continue; }
  got=$("$BIN" "$es" 2>"$W/code$code.log" | sed -n 's/^frameRate=//p')
  verdict=$(awk -v got="$got" -v want="$want" 'BEGIN {
      d = got - want; if (d < 0) d = -d
      print (got != "" && d <= 0.001) ? "PASS" : "FAIL" }')
  echo "frame_rate_code $code: read ${got:-nothing}, want $want - $verdict"
  [ "$verdict" = PASS ] || ok=0
done

if [ $ok = 1 ]; then echo "PASS: all eight MPEG-2 frame rates read correctly"; exit 0; fi
echo "FAIL: MPEG-2 frame rates read wrongly"
exit 1
