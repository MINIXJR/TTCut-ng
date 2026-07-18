#!/bin/bash
# Build deterministic damaged-TS fixtures for the gap-repair tests.
set -euo pipefail
FIX=/usr/local/src/CLAUDE_TMP/TTCut-ng/demuxrepair/fixtures
mkdir -p "$FIX"
HERE="$(cd "$(dirname "$0")" && pwd)"

gen() {  # $1=vcodec $2=out
    ffmpeg -y -v error \
      -f lavfi -i "testsrc2=size=1280x720:rate=50:duration=60" \
      -f lavfi -i "sine=frequency=440:duration=60" \
      -f lavfi -i "sine=frequency=880:duration=60" \
      -map 0:v -map 1:a -map 2:a \
      -c:v "$1" -g 50 -bf 2 \
      -c:a:0 mp2 -b:a:0 192k -ar 48000 -ac 2 \
      -c:a:1 ac3 -b:a:1 448k -ar 48000 -ac 2 \
      -f mpegts "$2"
}
gen libx264    "$FIX/clean_h264.ts"
gen mpeg2video "$FIX/clean_mpeg2.ts"

pids() {  # $1=ts -> setzt VPID APID (erste Audio)
    VPID=$(ffprobe -v error -select_streams v:0 -show_entries stream=id -of csv=p=0 "$1" | head -1 | tr -d ',')
    APID=$(ffprobe -v error -select_streams a:0 -show_entries stream=id -of csv=p=0 "$1" | head -1 | tr -d ',')
}
for base in clean_h264 clean_mpeg2; do
    pids "$FIX/$base.ts"
    # Video-Loch 400ms @10s; kombiniertes Loch @30s (Video-Anteil)
    python3 "$HERE/damage_ts.py" "$FIX/$base.ts" "$FIX/.tmp1.ts" \
        --pid "$VPID" --drop-at 10 --drop-ms 400 --drop-at 30 --drop-ms 300
    # Audio-Loch 200ms @20s; kombiniertes Loch @30s (Audio-Anteil, 500ms > Video 300ms)
    python3 "$HERE/damage_ts.py" "$FIX/.tmp1.ts" "$FIX/${base/clean/dmg}.ts" \
        --pid "$APID" --drop-at 20 --drop-ms 200 --drop-at 30 --drop-ms 500
    rm -f "$FIX/.tmp1.ts"
done
echo "fixtures ready: $(ls "$FIX")"
