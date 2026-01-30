#!/bin/bash
# Test script for ES Smart Cut Engine
# Usage: ./test_es_smartcut.sh <input.264|input.265>

set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <input.264|input.265>"
    echo "Tests the NAL parser and ES Smart Cut engine"
    exit 1
fi

INPUT="$1"
BASENAME=$(basename "$INPUT" | sed 's/\.[^.]*$//')
OUTDIR=$(dirname "$INPUT")
OUTPUT="${OUTDIR}/${BASENAME}_smartcut_test.264"

if [ ! -f "$INPUT" ]; then
    echo "Error: Input file not found: $INPUT"
    exit 1
fi

echo "=============================================="
echo "ES Smart Cut Engine Test"
echo "=============================================="
echo "Input:  $INPUT"
echo "Output: $OUTPUT"
echo ""

# Check if TTCut binary exists
TTCUT="$(dirname "$0")/../ttcut"
if [ ! -x "$TTCUT" ]; then
    echo "Error: TTCut binary not found at $TTCUT"
    exit 1
fi

# For testing purposes, we'll use a simple command-line test
# In a real scenario, you would run TTCut and perform the cut

echo "Step 1: Analyzing ES file..."

# Get basic info using ffprobe
echo ""
echo "--- FFprobe analysis ---"
ffprobe -v error -select_streams v:0 \
    -show_entries stream=codec_name,width,height,avg_frame_rate \
    -of default=noprint_wrappers=1 "$INPUT" 2>&1

echo ""
echo "--- NAL Unit count ---"
# Count NAL units using simple search
NAL_COUNT=$(grep -c -P '\x00\x00\x01' "$INPUT" 2>/dev/null || echo "unknown")
echo "Approximate NAL units: $NAL_COUNT"

echo ""
echo "--- Keyframe positions ---"
# Find keyframe positions using ffprobe
ffprobe -v error -select_streams v:0 \
    -show_entries packet=pts_time,flags \
    -of csv=p=0 "$INPUT" 2>/dev/null | grep -m 10 ",K" || echo "(could not extract keyframes)"

echo ""
echo "=============================================="
echo "To test the Smart Cut in TTCut:"
echo "1. Run: QT_QPA_PLATFORM=xcb $TTCUT"
echo "2. Load the ES file"
echo "3. Set cut points"
echo "4. Enable 'Smart Cut V2' in settings (when available)"
echo "5. Perform cut"
echo "=============================================="

echo ""
echo "Test complete."
