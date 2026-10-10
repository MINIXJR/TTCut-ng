#!/bin/bash
#-----------------------------------------------------------------------------
# Generate screenshots for TTCut-ng Wiki documentation
#
# Usage: tools/ttcut-screenshots.sh [--repair-only] [output-dir]
#
# Default output: /usr/local/src/TTCut-ng.wiki/images
#
# Two runs of the application: the usual pictures with the Tux test project,
# then the audio repair dialog (one picture per view) with a second project
# whose sound carries what the dialog repairs. --repair-only does only the
# second run.
#
# Prerequisites: ffmpeg, built ttcut-ng binary in build/
#
# TTCUT_BINARY overrides the program to run (the gate uses that to check
# what happens when the run fails). TTCUT_QPA_PLATFORM overrides the Qt
# platform (default xcb; "offscreen" runs without a window).
#-----------------------------------------------------------------------------

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TESTDATA_DIR="$SCRIPT_DIR/testdata"
REPAIR_ONLY=0
if [[ "${1:-}" == "--repair-only" ]]; then
    REPAIR_ONLY=1
    shift
fi
OUTPUT_DIR="${1:-/usr/local/src/TTCut-ng.wiki/images}"

# Paths for generated test media
VIDEO_FILE="$TESTDATA_DIR/tux_test.264"
AUDIO_FILE="$TESTDATA_DIR/tux_test.ac3"
PROJECT_FILE="$TESTDATA_DIR/tux_test.ttcut"
SVG_FILE="$PROJECT_DIR/ui/pixmaps/Tux.svg"
TEMPLATE_FILE="$SCRIPT_DIR/ttcut-test.ttcut"
BINARY="${TTCUT_BINARY:-$PROJECT_DIR/build/ttcut-ng}"
PLATFORM="${TTCUT_QPA_PLATFORM:-xcb}"

# Paths for the repair dialog's pictures
REPAIR_AUDIO="$TESTDATA_DIR/tux_repair.ac3"
REPAIR_DONOR="$TESTDATA_DIR/tux_repair.mp2"
REPAIR_PROJECT="$TESTDATA_DIR/tux_repair.ttcut"
REPAIR_TEMPLATE="$SCRIPT_DIR/ttcut-repair-test.ttcut"

#-----------------------------------------------------------------------------
# Preflight checks
#-----------------------------------------------------------------------------
if [[ ! -x "$BINARY" ]]; then
    echo "ERROR: ttcut-ng binary not found. Run 'cmake --build build' first."
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

if [[ ! -f "$REPAIR_TEMPLATE" ]]; then
    echo "ERROR: Project template not found: $REPAIR_TEMPLATE"
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

    # gen_ac3_segment <out> <seconds> <stereo|5.1> <tone|silence> [frequency]
    gen_ac3_segment() {
        local out=$1 secs=$2 layout=$3 kind=$4 freq=${5:-440}
        local -a src enc
        if [ "$kind" = tone ]; then
            src=(-f lavfi -i "sine=frequency=$freq:duration=$secs:sample_rate=48000")
        else
            src=(-f lavfi -i "anullsrc=r=48000:cl=$layout" -t "$secs")
        fi
        if [ "$layout" = 5.1 ]; then
            enc=(-c:a ac3 -b:a 384k)
            [ "$kind" = tone ] && enc=(-af "pan=5.1|FL=c0|FR=c0|FC=c0|LFE=c0|BL=c0|BR=c0" "${enc[@]}")
        else
            enc=(-c:a ac3 -b:a 192k)
            [ "$kind" = tone ] && enc+=(-ac 2)
        fi
        ffmpeg -y -hide_banner -loglevel warning "${src[@]}" "${enc[@]}" "$out"
    }

    # Timeline: tone / silence alternating, stereo and 5.1 (acmod changes)
    gen_ac3_segment "$TMP_AC3_DIR/seg1.ac3" 25 stereo tone 440     # 0-25s
    gen_ac3_segment "$TMP_AC3_DIR/seg2.ac3"  5 stereo silence      # 25-30s
    gen_ac3_segment "$TMP_AC3_DIR/seg3.ac3" 25 5.1    tone 330     # 30-55s
    gen_ac3_segment "$TMP_AC3_DIR/seg4.ac3"  5 5.1    silence      # 55-60s
    gen_ac3_segment "$TMP_AC3_DIR/seg5.ac3" 30 stereo tone 440     # 60-90s
    gen_ac3_segment "$TMP_AC3_DIR/seg6.ac3"  5 stereo silence      # 90-95s
    gen_ac3_segment "$TMP_AC3_DIR/seg7.ac3" 25 5.1    tone 330     # 95-120s

    # Concatenate all segments (AC3 frames are self-contained)
    cat "$TMP_AC3_DIR"/seg{1,2,3,4,5,6,7}.ac3 > "$AUDIO_FILE"
    rm -rf "$TMP_AC3_DIR"

    rm -f "$TMP_PNG"
    echo "Test media generated: $VIDEO_FILE, $AUDIO_FILE"
else
    echo "Test media up to date."
fi

#-----------------------------------------------------------------------------
# Create .info metadata file (mimics ttcut-demux output)
#
# The test video is a raw H.264 elementary stream without container timing.
# TTCut-ng takes its frame rate from the SPS timing then (25 fps here); real
# recordings carry a .info file written by ttcut-demux, and the fixture
# mirrors that so the screenshots show the usual setup. Values mirror the
# ffmpeg generation parameters above (720x576, 25fps, libx264).
# See reference_libav_h264_framerate.md.
#-----------------------------------------------------------------------------
INFO_FILE="$TESTDATA_DIR/tux_test.info"
echo "Creating .info file..."
cat > "$INFO_FILE" << EOF
# TTCut Elementary Stream Info File
# Generated: $(date -Iseconds)
# Source: synthetic Tux test video (tools/ttcut-screenshots.sh)

[video]
file=tux_test.264
codec=h264
width=720
height=576
frame_rate=25/1
start_pts=0.000000
filler_stripped=false

[audio]
count=1
audio_0_file=tux_test.ac3
audio_0_codec=ac3
audio_0_lang=deu
audio_0_first_pts=0.000000
audio_0_trimmed_ms=0
EOF

#-----------------------------------------------------------------------------
# Create project file from template (replace placeholders with absolute paths)
#-----------------------------------------------------------------------------
echo "Creating project file..."
sed -e "s|__VIDEO_PATH__|$VIDEO_FILE|g" \
    -e "s|__AUDIO_PATH__|$AUDIO_FILE|g" \
    "$TEMPLATE_FILE" > "$PROJECT_FILE"

#-----------------------------------------------------------------------------
# Run TTCut-ng in screenshot mode (generate to temp dir, then compare)
#-----------------------------------------------------------------------------
# Honour project temp-dir policy (CLAUDE.md): use /usr/local/src/CLAUDE_TMP/<project>
# subdirectory if present, otherwise fall back to /tmp.
if [ -d /usr/local/src/CLAUDE_TMP/TTCut-ng ]; then
    TMP_SCREENSHOTS="/usr/local/src/CLAUDE_TMP/TTCut-ng/ttcut-screenshots-$$"
else
    TMP_SCREENSHOTS="/tmp/ttcut-screenshots-$$"
fi
mkdir -p "$TMP_SCREENSHOTS"

# run_app <what> <project> [further options]: one run of the application.
# The run's own exit status decides, not that of a filter behind it: a run
# that failed used to end as "0 updated, 0 unchanged" with exit code 0.
run_app() {
    local what=$1 project=$2
    shift 2
    local log="$TMP_SCREENSHOTS/run-$what.log" rc=0
    echo "Running TTCut-ng screenshot mode ($what)..."
    echo "  Temp:    $TMP_SCREENSHOTS"
    echo "  Output:  $OUTPUT_DIR"
    echo "  Project: $project"
    QT_QPA_PLATFORM="$PLATFORM" "$BINARY" --screenshots "$TMP_SCREENSHOTS" --project "$project" "$@" \
        > "$log" 2>&1 || rc=$?
    grep -E "Screenshot" "$log" || true
    if [ "$rc" -ne 0 ]; then
        echo "ERROR: screenshot run failed (exit code $rc). Last lines of its output:"
        tail -n 20 "$log"
        rm -rf "$TMP_SCREENSHOTS"
        exit 1
    fi
}

# no_image_check: a run that left no picture is a failed run.
no_image_check() {
    if ! compgen -G "$TMP_SCREENSHOTS/ttcutng-*.png" > /dev/null; then
        echo "ERROR: the screenshot run produced no image."
        rm -rf "$TMP_SCREENSHOTS"
        exit 1
    fi
}

if [ "$REPAIR_ONLY" -eq 0 ]; then
    run_app usual "$PROJECT_FILE"
    no_image_check
fi

#-----------------------------------------------------------------------------
# Sound for the pictures of the repair dialog
#
# The dialog repairs what the anomaly scan finds, and the Tux tone above has
# none of it. A second 5.1 track - a chord in the centre, quiet tones around
# it, a silent LFE - carries one of each kind:
#    20.000 s  a square burst in the centre with a 60 Hz pulse in the LFE (0.2 s)
#    50.003 s  the sound stops in mid-wave, back after 0.5 s
#    80.000 s  a hole of 17 ms
# An MP2 track with the stereo downmix of the undisturbed sound is the second
# track a hole is filled from. Measured 2026-10-10: the scan reports the three
# as "LFE", "stop" and "hole", the stop search and the fill search find them.
#-----------------------------------------------------------------------------
if [[ ! -f "$REPAIR_AUDIO" || ! -f "$REPAIR_DONOR" || "$0" -nt "$REPAIR_AUDIO" ]]; then
    echo "Generating the sound for the repair dialog's pictures..."
    BED="$TESTDATA_DIR/tux_repair_bed.wav"
    CHORD="0.06*(sin(2*PI*277*t)+sin(2*PI*331*t)+sin(2*PI*419*t)+sin(2*PI*523*t)+sin(2*PI*659*t))"
    ffmpeg -y -hide_banner -loglevel warning -f lavfi \
        -i "aevalsrc=exprs=0.02*sin(2*PI*196*t)|0.02*sin(2*PI*247*t)|$CHORD|0|0.01*sin(2*PI*147*t)|0.01*sin(2*PI*165*t):channel_layout=5.1(side):sample_rate=48000:duration=120" \
        -c:a pcm_f32le "$BED"
    BURST="between(t\\,20\\,20.2)"
    GAPS="(1-between(n\\,2400144\\,2424143)-between(n\\,3840000\\,3840815))"
    ffmpeg -y -hide_banner -loglevel warning -i "$BED" \
        -af "aeval=val(0)*$GAPS|val(1)*$GAPS|val(2)*$GAPS+0.6*sgn(sin(2*PI*900*t))*$BURST|0.5*sin(2*PI*60*t)*$BURST|val(4)*$GAPS|val(5)*$GAPS" \
        -c:a ac3 -b:a 384k "$REPAIR_AUDIO"
    ffmpeg -y -hide_banner -loglevel warning -i "$BED" \
        -af "pan=stereo|c0=c0+0.707*c2|c1=c1+0.707*c2" -c:a mp2 -b:a 192k "$REPAIR_DONOR"
    rm -f "$BED"
fi

sed -e "s|__VIDEO_PATH__|$VIDEO_FILE|g" \
    -e "s|__AUDIO_PATH__|$REPAIR_AUDIO|g" \
    -e "s|__DONOR_PATH__|$REPAIR_DONOR|g" \
    "$REPAIR_TEMPLATE" > "$REPAIR_PROJECT"

run_app repair "$REPAIR_PROJECT" --screenshot-set repair

#-----------------------------------------------------------------------------
# Compare and copy only changed screenshots
#-----------------------------------------------------------------------------
echo ""
UPDATED=0
UNCHANGED=0

for f in "$TMP_SCREENSHOTS"/ttcutng-*.png; do
    [[ -f "$f" ]] || continue
    NAME="$(basename "$f")"
    TARGET="$OUTPUT_DIR/$NAME"

    if [[ -f "$TARGET" ]] && cmp -s "$f" "$TARGET"; then
        UNCHANGED=$((UNCHANGED + 1))
    else
        cp "$f" "$TARGET"
        UPDATED=$((UPDATED + 1))
        echo "  Updated: $NAME"
    fi
done

# Clean up temp dir
rm -rf "$TMP_SCREENSHOTS"

if [ $((UPDATED + UNCHANGED)) -eq 0 ]; then
    echo "ERROR: the screenshot run produced no image."
    exit 1
fi

echo ""
echo "Screenshots: $UPDATED updated, $UNCHANGED unchanged."
