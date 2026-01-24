#!/bin/bash
#
# ts_demux_normalize.sh - Demux and normalize TS files for TTCut
# Similar to ProjectX for MPEG-2, but for H.264/H.265 (AVC/HEVC)
#
# Supported video codecs: H.264 (AVC), H.265 (HEVC)
# Supported audio codecs: MP2, AC3, AAC, MP3
#
# Usage: ts_demux_normalize.sh <input.ts> [output_dir]
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

info() { echo -e "${GREEN}[INFO]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

# Check arguments
if [ -z "$1" ]; then
    echo "Usage: $0 <input.ts> [output_dir]"
    echo ""
    echo "Demux and normalize H.264/H.265 TS files for TTCut."
    echo "Similar to ProjectX for MPEG-2."
    echo ""
    echo "Output:"
    echo "  <basename>_normalized.ts  - Video + first audio, normalized timestamps"
    echo "  <basename>_audio_N.mp2/ac3 - Additional audio tracks (if any)"
    echo "  <basename>.srt            - Subtitles (if DVB subtitles present)"
    exit 1
fi

INPUT="$1"
BASENAME=$(basename "$INPUT" .ts)
OUTDIR="${2:-$(dirname "$INPUT")}"

# Check input file exists
[ -f "$INPUT" ] || error "Input file not found: $INPUT"

# Create output directory if needed
mkdir -p "$OUTDIR"

info "Analyzing: $INPUT"
echo ""

# Get stream information with language tags
VIDEO_STREAM=""
AUDIO_STREAMS=()
SUBTITLE_STREAMS=()
VIDEO_CODEC=""

# Get video stream
VIDEO_INFO=$(ffprobe -v error -select_streams v:0 -show_entries stream=index,codec_name,width,height -of csv "$INPUT" 2>&1 | grep "^stream," | head -1)
if [ -n "$VIDEO_INFO" ]; then
    IFS=',' read -r _ idx codec_name width height <<< "$VIDEO_INFO"
    VIDEO_STREAM="$idx"
    VIDEO_CODEC="$codec_name"
    info "Video stream $idx: $codec_name ${width}x${height}"
fi

# Get audio streams with language (deduplicate by index, prefer entries with language)
declare -A AUDIO_MAP
while IFS=',' read -r _ idx codec_name lang; do
    [ -z "$idx" ] && continue
    # If we already have this index with a language, skip
    if [ -n "${AUDIO_MAP[$idx]}" ]; then
        # Only replace if current has no language and new one does
        old_lang="${AUDIO_MAP[$idx]##*:}"
        if [ "$old_lang" = "und" ] && [ -n "$lang" ]; then
            AUDIO_MAP[$idx]="$idx:$codec_name:$lang"
        fi
    else
        lang="${lang:-und}"
        AUDIO_MAP[$idx]="$idx:$codec_name:$lang"
    fi
done < <(ffprobe -v error -select_streams a -show_entries stream=index,codec_name:stream_tags=language -of csv "$INPUT" 2>&1 | grep "^stream,")

# Convert map to array, sorted by index
for idx in $(echo "${!AUDIO_MAP[@]}" | tr ' ' '\n' | sort -n); do
    AUDIO_STREAMS+=("${AUDIO_MAP[$idx]}")
    IFS=':' read -r _ codec lang <<< "${AUDIO_MAP[$idx]}"
    info "Audio stream $idx: $codec [$lang]"
done

# Get subtitle streams with language (deduplicate by index, prefer entries with language)
declare -A SUB_MAP
while IFS=',' read -r _ idx codec_name lang; do
    [ -z "$idx" ] && continue
    if [ -n "${SUB_MAP[$idx]}" ]; then
        old_lang="${SUB_MAP[$idx]##*:}"
        if [ "$old_lang" = "und" ] && [ -n "$lang" ]; then
            SUB_MAP[$idx]="$idx:$codec_name:$lang"
        fi
    else
        lang="${lang:-und}"
        SUB_MAP[$idx]="$idx:$codec_name:$lang"
    fi
done < <(ffprobe -v error -select_streams s -show_entries stream=index,codec_name:stream_tags=language -of csv "$INPUT" 2>&1 | grep "^stream,")

for idx in $(echo "${!SUB_MAP[@]}" | tr ' ' '\n' | sort -n); do
    SUBTITLE_STREAMS+=("${SUB_MAP[$idx]}")
    IFS=':' read -r _ codec lang <<< "${SUB_MAP[$idx]}"
    info "Subtitle stream $idx: $codec [$lang]"
done

echo ""

# Check we have video
[ -n "$VIDEO_STREAM" ] || error "No video stream found"

# Check video codec is H.264 or H.265
case "$VIDEO_CODEC" in
    h264|hevc|h265)
        info "Video codec $VIDEO_CODEC supported"
        ;;
    *)
        warn "Video codec $VIDEO_CODEC may not be fully supported"
        ;;
esac

# Get original timestamps for info
START_PTS=$(ffprobe -v error -show_entries stream=start_time -of csv "$INPUT" 2>&1 | grep "^stream," | head -1 | cut -d',' -f2)
info "Original start time: ${START_PTS}s"

# 1. Create normalized TS with video + first audio
if [ ${#AUDIO_STREAMS[@]} -gt 0 ]; then
    IFS=':' read -r FIRST_AUDIO FIRST_CODEC FIRST_LANG <<< "${AUDIO_STREAMS[0]}"
    OUTPUT_TS="$OUTDIR/${BASENAME}_normalized.ts"

    info "Creating normalized TS: $OUTPUT_TS"
    info "  Video: stream $VIDEO_STREAM"
    info "  Audio: stream $FIRST_AUDIO [$FIRST_LANG]"

    ffmpeg -y -fflags +genpts \
        -i "$INPUT" \
        -map 0:$VIDEO_STREAM -map 0:$FIRST_AUDIO \
        -c copy \
        -avoid_negative_ts make_zero \
        "$OUTPUT_TS" 2>&1 | grep -E "^(frame=|Output|Stream)" || true

    # Verify output
    NEW_START=$(ffprobe -v error -show_entries stream=start_time -of csv "$OUTPUT_TS" 2>&1 | grep "^stream," | head -1 | cut -d',' -f2)
    info "Normalized start time: ${NEW_START}s"
    echo ""
fi

# 2. Extract all audio tracks as separate files
if [ ${#AUDIO_STREAMS[@]} -gt 0 ]; then
    info "Extracting audio tracks..."

    for i in "${!AUDIO_STREAMS[@]}"; do
        IFS=':' read -r idx codec lang <<< "${AUDIO_STREAMS[$i]}"

        # Determine extension
        case "$codec" in
            mp2|mp3) EXT="mp2" ;;
            ac3) EXT="ac3" ;;
            aac) EXT="aac" ;;
            *) EXT="$codec" ;;
        esac

        # Include language in filename
        OUTPUT_AUDIO="$OUTDIR/${BASENAME}_audio_${lang}.${EXT}"

        # If file already exists (same language, different stream), add index
        if [ -f "$OUTPUT_AUDIO" ]; then
            OUTPUT_AUDIO="$OUTDIR/${BASENAME}_audio_${lang}_${idx}.${EXT}"
        fi

        info "  Audio $idx [$lang] -> $OUTPUT_AUDIO"

        ffmpeg -y -i "$INPUT" \
            -map 0:$idx \
            -c copy \
            "$OUTPUT_AUDIO" 2>&1 | grep -E "^(Output|Stream)" || true
    done
    echo ""
fi

# 3. Try to extract subtitles
if [ ${#SUBTITLE_STREAMS[@]} -gt 0 ]; then
    info "Attempting subtitle extraction..."

    for sub in "${SUBTITLE_STREAMS[@]}"; do
        IFS=':' read -r idx codec lang <<< "$sub"

        case "$codec" in
            dvb_subtitle|dvbsub)
                # DVB subtitles are bitmap-based, need OCR for SRT
                warn "DVB subtitle stream $idx [$lang] - bitmap format, needs OCR conversion"
                warn "Consider using: https://github.com/decltype/BDSup2Sub or similar"
                ;;
            subrip|srt|text)
                OUTPUT_SUB="$OUTDIR/${BASENAME}_sub_${lang}.srt"
                info "  Subtitle $idx [$lang] -> $OUTPUT_SUB"
                ffmpeg -y -i "$INPUT" -map 0:$idx "$OUTPUT_SUB" 2>&1 | grep -E "^(Output|Stream)" || true
                ;;
            *)
                warn "Subtitle stream $idx [$lang]: $codec - extraction not supported"
                ;;
        esac
    done
    echo ""
fi

# Summary
echo ""
info "=== Demux complete ==="
echo ""
echo "Output files:"
ls -lh "$OUTDIR/${BASENAME}"* 2>/dev/null | grep -v "^total" || true
echo ""
info "Main file for TTCut: $OUTPUT_TS"
