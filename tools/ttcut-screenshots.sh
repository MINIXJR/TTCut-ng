#!/bin/bash
#-----------------------------------------------------------------------------
# Generate screenshots for TTCut-ng Wiki documentation
#
# Usage: tools/ttcut-screenshots.sh [output-dir]
#
# Default output: /usr/local/src/TTCut-ng.wiki/images
#
# Prerequisites: ffmpeg, built ttcut-ng binary in project root
#-----------------------------------------------------------------------------

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TESTDATA_DIR="$SCRIPT_DIR/testdata"
OUTPUT_DIR="${1:-/usr/local/src/TTCut-ng.wiki/images}"

# Paths for generated test media
VIDEO_FILE="$TESTDATA_DIR/tux_test.264"
AUDIO_FILE="$TESTDATA_DIR/tux_test.ac3"
PRJ_FILE="$TESTDATA_DIR/tux_test.prj"
SVG_FILE="$PROJECT_DIR/ui/pixmaps/Tux.svg"
TEMPLATE_FILE="$SCRIPT_DIR/ttcut-test.prj"
BINARY="$PROJECT_DIR/ttcut-ng"

#-----------------------------------------------------------------------------
# Preflight checks
#-----------------------------------------------------------------------------
if [[ ! -x "$BINARY" ]]; then
    echo "ERROR: ttcut-ng binary not found. Run 'make' first."
    exit 1
fi

if ! command -v ffmpeg &>/dev/null; then
    echo "ERROR: ffmpeg not found."
    exit 1
fi

if [[ ! -f "$SVG_FILE" ]]; then
    echo "ERROR: Tux SVG not found: $SVG_FILE"
    exit 1
fi

if [[ ! -f "$TEMPLATE_FILE" ]]; then
    echo "ERROR: Project template not found: $TEMPLATE_FILE"
    exit 1
fi

#-----------------------------------------------------------------------------
# Create testdata directory
#-----------------------------------------------------------------------------
mkdir -p "$TESTDATA_DIR"
mkdir -p "$OUTPUT_DIR"

#-----------------------------------------------------------------------------
# Generate test video from Tux SVG (if not present or SVG is newer)
#-----------------------------------------------------------------------------
if [[ ! -f "$VIDEO_FILE" || "$SVG_FILE" -nt "$VIDEO_FILE" ]]; then
    echo "Generating test video from Tux SVG..."

    # Step 1: SVG -> PNG (720x576, PAL resolution)
    TMP_PNG="$TESTDATA_DIR/tux_frame.png"
    ffmpeg -y -hide_banner -loglevel warning \
        -i "$SVG_FILE" \
        -vf "scale=720:576:force_original_aspect_ratio=decrease,pad=720:576:(ow-iw)/2:(oh-ih)/2:color=black" \
        -update 1 \
        "$TMP_PNG"

    # Step 2: PNG -> H.264 elementary stream (~120s at 25fps = 3000 frames)
    # Use slow panning/zooming for visual variety across cut points
    ffmpeg -y -hide_banner -loglevel warning \
        -loop 1 -framerate 25 -i "$TMP_PNG" \
        -t 120 \
        -vf "zoompan=z='1+0.0005*on':x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)':d=1:s=720x576:fps=25" \
        -c:v libx264 -preset medium -crf 18 \
        -g 25 -keyint_min 25 \
        -bsf:v h264_mp4toannexb \
        -f h264 \
        "$VIDEO_FILE"

    # Step 3: Generate AC3 audio with silent gaps AND channel format changes
    # Segments: stereo tone, stereo silence, 5.1 tone, 5.1 silence, stereo tone, stereo silence, 5.1 tone
    TMP_AC3_DIR="$TESTDATA_DIR/ac3_parts"
    mkdir -p "$TMP_AC3_DIR"

    # Segment 1: 0-25s stereo tone
    ffmpeg -y -hide_banner -loglevel warning \
        -f lavfi -i "sine=frequency=440:duration=25:sample_rate=48000" \
        -c:a ac3 -b:a 192k -ac 2 "$TMP_AC3_DIR/seg1.ac3"

    # Segment 2: 25-30s stereo silence
    ffmpeg -y -hide_banner -loglevel warning \
        -f lavfi -i "anullsrc=r=48000:cl=stereo" -t 5 \
        -c:a ac3 -b:a 192k "$TMP_AC3_DIR/seg2.ac3"

    # Segment 3: 30-55s 5.1 tone
    ffmpeg -y -hide_banner -loglevel warning \
        -f lavfi -i "sine=frequency=330:duration=25:sample_rate=48000" \
        -af "pan=5.1|FL=c0|FR=c0|FC=c0|LFE=c0|BL=c0|BR=c0" \
        -c:a ac3 -b:a 384k "$TMP_AC3_DIR/seg3.ac3"

    # Segment 4: 55-60s 5.1 silence
    ffmpeg -y -hide_banner -loglevel warning \
        -f lavfi -i "anullsrc=r=48000:cl=5.1" -t 5 \
        -c:a ac3 -b:a 384k "$TMP_AC3_DIR/seg4.ac3"

    # Segment 5: 60-90s stereo tone
    ffmpeg -y -hide_banner -loglevel warning \
        -f lavfi -i "sine=frequency=440:duration=30:sample_rate=48000" \
        -c:a ac3 -b:a 192k -ac 2 "$TMP_AC3_DIR/seg5.ac3"

    # Segment 6: 90-95s stereo silence
    ffmpeg -y -hide_banner -loglevel warning \
        -f lavfi -i "anullsrc=r=48000:cl=stereo" -t 5 \
        -c:a ac3 -b:a 192k "$TMP_AC3_DIR/seg6.ac3"

    # Segment 7: 95-120s 5.1 tone
    ffmpeg -y -hide_banner -loglevel warning \
        -f lavfi -i "sine=frequency=330:duration=25:sample_rate=48000" \
        -af "pan=5.1|FL=c0|FR=c0|FC=c0|LFE=c0|BL=c0|BR=c0" \
        -c:a ac3 -b:a 384k "$TMP_AC3_DIR/seg7.ac3"

    # Concatenate all segments (AC3 frames are self-contained)
    cat "$TMP_AC3_DIR"/seg{1,2,3,4,5,6,7}.ac3 > "$AUDIO_FILE"
    rm -rf "$TMP_AC3_DIR"

    rm -f "$TMP_PNG"
    echo "Test media generated: $VIDEO_FILE, $AUDIO_FILE"
else
    echo "Test media up to date."
fi

#-----------------------------------------------------------------------------
# Create project file from template (replace placeholders with absolute paths)
#-----------------------------------------------------------------------------
echo "Creating project file..."
sed -e "s|__VIDEO_PATH__|$VIDEO_FILE|g" \
    -e "s|__AUDIO_PATH__|$AUDIO_FILE|g" \
    "$TEMPLATE_FILE" > "$PRJ_FILE"

#-----------------------------------------------------------------------------
# Run TTCut-ng in screenshot mode
#-----------------------------------------------------------------------------
echo "Running TTCut-ng screenshot mode..."
echo "  Output: $OUTPUT_DIR"
echo "  Project: $PRJ_FILE"

QT_QPA_PLATFORM=xcb "$BINARY" --screenshots "$OUTPUT_DIR" --project "$PRJ_FILE" 2>&1 | \
    grep -E "Screenshot" || true

#-----------------------------------------------------------------------------
# Report
#-----------------------------------------------------------------------------
echo ""
echo "Screenshots generated:"
ls -1 "$OUTPUT_DIR"/ttcutng-*.png 2>/dev/null | while read -r f; do
    echo "  $(basename "$f")"
done

echo ""
echo "Done. Copy to wiki with: cp $OUTPUT_DIR/ttcutng-*.png /usr/local/src/TTCut-ng.wiki/images/"
