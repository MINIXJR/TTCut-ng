#!/bin/bash
# gate_au_without_slice.sh - a picture whose slice start code is damaged must
# not make a recording uncuttable. Such a picture is left as an AUD (plus SEI)
# without a slice NAL; TTNaluParser counted it as an access unit while libav
# puts its bytes in front of the next packet, so the display-order map had one
# entry less than the parser had pictures and every cut failed ("display-order
# map unavailable or misaligned", measured 2026-10-13 on a DVB recording).
# On a generated recording (no material needed): encode a TS with AUDs, damage
# the slice start code of one non-key picture, demux it, then check
#   1  the .info names the place in es_missing_ranges (one picture lost)
#   2  parser and indexer count the same number of pictures
#   3  --auto-cut over the place ends with 0 and writes every picture of the cut
#   usage: tools/diag/gate_au_without_slice.sh h264|hevc
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
D="$ROOT/tools/diag"
DEMUX="$ROOT/tools/ttcut-demux/ttcut-demux"
# shellcheck source=tools/diag/ttcut-project.sh
. "$D/ttcut-project.sh"
CODEC=${1:-}
case $CODEC in
  h264) ENC=(-c:v libx264 -preset ultrafast -g 50 -bf 2 -x264-params aud=1); EXT=264 ;;
  hevc) ENC=(-c:v libx265 -preset ultrafast -g 50 -x265-params aud=1:log-level=error); EXT=265 ;;
  *) echo "usage: $(basename "$0") h264|hevc" >&2; exit 2 ;;
esac
# A subdirectory of its own: under run-gates.sh $W also holds the runner's
# HOME and XDG directories.
WORK="${W:-/usr/local/src/CLAUDE_TMP/TTCut-ng/au-without-slice}/$CODEC"
rm -rf "$WORK"; mkdir -p "$WORK/es" "$WORK/out"; cd "$WORK" || exit 1
export XDG_CACHE_HOME="${XDG_CACHE_HOME:-$WORK/xdg-cache}" XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$WORK/xdg-config}"
export QT_QPA_PLATFORM=offscreen
PASS=0; FAIL=0
ok()  { echo "PASS: $1"; PASS=$((PASS+1)); }
bad() { echo "FAIL: $1"; FAIL=$((FAIL+1)); }
for b in "$D/test_au_count_match" "$ROOT/build/ttcut-ng" "$ROOT/tools/ttcut-audiofix/ttcut-audiofix"; do
  [ -x "$b" ] || { echo "FAIL: not built: $b"; exit 1; }
done

PICTURES=500
ffmpeg -v error -y -f lavfi -i "testsrc2=size=320x180:rate=50" -f lavfi -i "sine=frequency=440:sample_rate=48000" \
    -t 10 "${ENC[@]}" -c:a mp2 -b:a 192k -ac 2 -f mpegts clean.ts \
    || { echo "FAIL: ffmpeg could not build the recording"; exit 1; }

# The first picture from number 250 on whose AUD is followed directly by its
# slice in the same TS packet (a key picture carries parameter sets there).
python3 - "$CODEC" clean.ts damaged.ts <<'PY' || { echo "FAIL: no picture to damage found"; exit 1; }
import sys
codec, src, dst = sys.argv[1:]
d = bytearray(open(src, 'rb').read())
aud = b'\x00\x00\x01\x09' if codec == 'h264' else b'\x00\x00\x01\x46'
def is_slice(b):
    return (b & 0x1F) == 1 if codec == 'h264' else ((b >> 1) & 0x3F) <= 9
p = -1
n = -1
while True:
    p = d.find(aud, p + 1)
    if p < 0:
        sys.exit(1)
    n += 1
    if n < 250:
        continue
    q = d.find(b'\x00\x00\x01', p + 4)
    if q >= 0 and q // 188 == p // 188 and is_slice(d[q + 3]):
        break
d[q + 1] = 0x04                       # 00 00 01 -> 00 04 01: no start code any more
open(dst, 'wb').write(d)
print(f"damaged the slice start code of picture {n} (decode order)")
PY

PATH="$ROOT/tools/ttcut-audiofix:$ROOT/tools/ttcut-ac3fix:$PATH" \
    "$DEMUX" -e --no-subs -n rec damaged.ts "$WORK/es" > demux.log 2>&1 \
    || { echo "FAIL: ttcut-demux failed"; tail -n 5 demux.log; exit 1; }
ES="$WORK/es/rec.$EXT"
AUDIO=$(ls "$WORK"/es/rec_*.mp2 2>/dev/null | head -1)
[ -s "$ES" ] && [ -n "$AUDIO" ] || { echo "FAIL: demux wrote no video or no audio"; exit 1; }

# 1 - the demuxer reports the place
RANGES=$(sed -n 's/^es_missing_ranges=//p' "$WORK/es/rec.info")
LOST=$(sed -n 's/^es_lost_ms=//p' "$WORK/es/rec.info")
if [[ "$RANGES" =~ ^[0-9]+-[0-9]+:20$ ]] && [ "$LOST" = 20 ]; then
  ok ".info reports one lost picture: es_missing_ranges=$RANGES"
else
  bad ".info does not report one lost picture: es_missing_ranges='$RANGES' es_lost_ms='$LOST'"
fi

# 2 - both splitters count the same pictures
if "$D/test_au_count_match" "$ES" $((PICTURES - 1)) > count.log 2>&1; then
  ok "$(tail -n 1 count.log | sed 's/^PASS: //')"
else
  bad "$(grep -v '^FAIL' count.log | tail -n 1)"
fi

# 3 - a cut over the place
ttcut_project_xml "$ES" "$AUDIO" - 100:399 > cut.ttcut
LC_ALL=C.UTF-8 "$ROOT/build/ttcut-ng" --project cut.ttcut --auto-cut "$WORK/out/cut.mkv" > cut.stdout 2>&1; RC=$?
N=$(ffprobe -v error -select_streams v:0 -count_packets -show_entries stream=nb_read_packets -of csv=p=0 "$WORK/out/cut.mkv" 2>/dev/null)
if [ "$RC" -eq 0 ] && [ "$N" = 300 ]; then
  ok "cut 100-399 over the place: exit 0, 300 pictures in the MKV"
else
  bad "cut 100-399 over the place: exit $RC, '$N' pictures in the MKV (want 0 and 300)"
  grep -F "Auto-cut: cut failed" "$XDG_CACHE_HOME/ttcut-ng/logfile.log" 2>/dev/null | tail -1
fi

echo "$CODEC: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
