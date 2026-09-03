#!/bin/bash
# Build deterministic damaged-TS fixtures for the gap-repair tests.
set -euo pipefail
FIX=/usr/local/src/CLAUDE_TMP/TTCut-ng/demuxrepair/fixtures
mkdir -p "$FIX"
HERE="$(cd "$(dirname "$0")" && pwd)"

# Initialize MANIFEST with header and timestamp
{
    echo "# Measured gap manifest for TTCut-ng demux repair test fixtures"
    echo "# Generated: $(date)"
    echo "# Format: <file> <stream> <gap_description>"
} > "$FIX/MANIFEST.txt"

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

# PID of the first stream matching the selector. ffprobe 8.x prints duplicate
# id lines + trailing commas for mpegts — sanitize.
probe_stream_id() {  # $1=selector $2=ts
    ffprobe -v error -select_streams "$1" -show_entries stream=id -of csv=p=0 "$2" | head -1 | tr -d ','
}
pids() {  # $1=ts -> setzt VPID APID APID2 (Video, erste Audio, zweite Audio)
    VPID=$(probe_stream_id v:0 "$1")
    APID=$(probe_stream_id a:0 "$1")
    APID2=$(probe_stream_id a:1 "$1")
}
for base in clean_h264 clean_mpeg2; do
    pids "$FIX/$base.ts"
    # Video-Loch 400ms @10s; kombiniertes Loch @30s (Video-Anteil)
    python3 "$HERE/damage_ts.py" "$FIX/$base.ts" "$FIX/.tmp1.ts" \
        --pid "$VPID" --drop-at 10 --drop-ms 400 --drop-at 30 --drop-ms 300
    # Audio-Loch 200ms @20s; kombiniertes Loch @30s (Audio-Anteil, 500ms > Video 300ms)
    python3 "$HERE/damage_ts.py" "$FIX/.tmp1.ts" "$FIX/.tmp2.ts" \
        --pid "$APID" --drop-at 20 --drop-ms 200 --drop-at 30 --drop-ms 500
    # Damage second audio track with same pattern
    python3 "$HERE/damage_ts.py" "$FIX/.tmp2.ts" "$FIX/${base/clean/dmg}.ts" \
        --pid "$APID2" --drop-at 20 --drop-ms 200 --drop-at 30 --drop-ms 500
    rm -f "$FIX/.tmp1.ts" "$FIX/.tmp2.ts"
done
# Measure and record gaps in MANIFEST
check_gaps() {  # $1=fixture $2=stream_spec
    local fixture=$1 stream_spec=$2 fixture_base
    fixture_base=$(basename "$fixture" .ts)
    ffprobe -v error -select_streams "$stream_spec" -show_entries "packet=pts_time,duration" \
        -of csv=p=0 "$fixture" | sort -n | awk -F, -v fb="$fixture_base" -v ss="$stream_spec" '
        BEGIN { prev_end = -1; found_gap = 0 }
        NF >= 2 {
            pts = $1 + 0
            dur = ($2 + 0) / 90000  # Convert from 90kHz timebase to seconds
            if (dur == 0 || dur == "") dur = 0.02
            end = pts + dur
            if (prev_end > 0) {
                gap = pts - prev_end
                if (gap > 0.06) {
                    printf "%s %s gap @%.2fs %dms\n", fb, ss, pts, int(gap * 1000)
                    found_gap = 1
                }
            }
            prev_end = end
        }
        END { if (found_gap == 0) print fb " " ss " no-gaps" }' >> "$FIX/MANIFEST.txt"
}

# Check all fixtures for gaps
for fixture in "$FIX"/*.ts; do
    pids "$fixture"
    check_gaps "$fixture" "v:0"
    check_gaps "$fixture" "a:0"
    check_gaps "$fixture" "a:1"
done

echo "fixtures ready: $(ls "$FIX")"
echo "manifest: $(cat "$FIX/MANIFEST.txt")"
