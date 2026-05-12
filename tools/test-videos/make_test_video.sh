#!/bin/bash
# Generate test videos with known black-frame, scene-change, and logo
# markers for TTCut-ng search verification. Six codec variants share the
# same 120-second Tux timeline:
#
#   0-30s   BLUE  + Tux moves L->R
#   30-31s  BLACK 1 (1s pure black)
#   31-60s  RED   + Tux moves R->L
#   60-61s  BLACK 2
#   61-90s  GREEN + Tux moves L->R + small Tux LOGO top-right
#   90-91s  BLACK 3
#   91-120s checkerboard (testsrc2)
#
# Naming: tux_<codec><resolution>_<gop_pattern>_test.<ext>
#   codec        = hevc | h264 | mpeg2
#   resolution   = 4k | 1080p | 1080i | 576i | 720p
#   gop_pattern  = cra (HEVC open-gop)
#                  progressive (H.264/MPEG-2 progressive)
#                  mbaff | paff (H.264 interlaced variants)
#                  pal (MPEG-2 DVB-SD)
#
# Tux artwork (c) Larry Ewing. Used here for testing only.
#
# Outputs go to the gitignored ./cache/ subdirectory. Existing files are
# reused (skip-if-present); pass --force to regenerate.
#
# Usage:
#   ./make_test_video.sh             # all six files
#   ./make_test_video.sh hevc4k      # only HEVC 4K
#   ./make_test_video.sh h264        # all H.264 variants
#   ./make_test_video.sh mpeg2       # both MPEG-2 variants
#   ./make_test_video.sh paff        # only the slow JM PAFF run
#   ./make_test_video.sh --force ... # regenerate even if files exist

set -euo pipefail

SCRIPT_PATH="$(realpath "${BASH_SOURCE[0]}")"
SCRIPTDIR="$(dirname "$SCRIPT_PATH")"
OUTDIR="$SCRIPTDIR/cache"
mkdir -p "$OUTDIR"
cd "$OUTDIR"

TUX_SVG="/usr/local/src/TTCut-ng/ui/pixmaps/Tux.svg"
JM_LENCOD="/usr/local/src/jm-reference/bin/lencod_static"
JM_CFG_DIR="/usr/local/src/jm-reference/cfg"

FORCE=0
if [[ "${1:-}" == "--force" ]]; then
    FORCE=1
    shift
fi

# Returns 0 (= skip generation) if $1 exists, has non-zero size, is newer
# than the script itself, and FORCE is not set. Editing the script bumps its
# mtime, so encoder-parameter changes invalidate cached outputs automatically.
output_already_present() {
    [[ $FORCE -eq 0 && -s "$1" && "$1" -nt "$SCRIPT_PATH" ]]
}

# ---------- Tux PNG render (cached) ----------
render_tux() {
    if [[ ! -s tux_main.png ]]; then
        rsvg-convert "$TUX_SVG" -h 800 -o tux_main.png
    fi
    if [[ ! -s tux_logo.png ]]; then
        rsvg-convert "$TUX_SVG" -h 250 -o tux_logo.png
    fi
}

# ---------- Audio generators ----------
# Optional second arg: duration in seconds (default 120).
gen_audio_ac3() {
    local out=$1
    local duration=${2:-120}
    ffmpeg -y -hide_banner -loglevel warning \
        -f lavfi -i "sine=frequency=1000:sample_rate=48000:duration=${duration}" \
        -ac 2 -c:a ac3 -b:a 192k \
        "$out"
}

gen_audio_mp2() {
    local out=$1
    local duration=${2:-120}
    ffmpeg -y -hide_banner -loglevel warning \
        -f lavfi -i "sine=frequency=1000:sample_rate=48000:duration=${duration}" \
        -ac 2 -c:a mp2 -b:a 192k \
        "$out"
}

# ---------- Project file writer ----------
write_ttcut_project() {
    local basename=$1 video_ext=$2 audio_ext=$3
    cat > "${basename}.ttcut" <<EOF
<!DOCTYPE TTCut-Projectfile>
<TTCut-Projectfile>
 <Version>1.0</Version>
 <Video>
  <Order>0</Order>
  <Name>${OUTDIR}/${basename}.${video_ext}</Name>
  <Audio>
   <Order>0</Order>
   <Name>${OUTDIR}/${basename}.${audio_ext}</Name>
   <Language>deu</Language>
  </Audio>
 </Video>
</TTCut-Projectfile>
EOF
}

# ---------- Tux timeline filter graph ----------
# Produces a 120-second concatenated [outv] stream for the given resolution.
build_tux_timeline_args() {
    local W=$1 H=$2 R=$3
    cat <<EOF
-f lavfi -i color=c=blue:s=${W}x${H}:r=${R}:d=30
-f lavfi -i color=c=black:s=${W}x${H}:r=${R}:d=1
-f lavfi -i color=c=red:s=${W}x${H}:r=${R}:d=29
-f lavfi -i color=c=black:s=${W}x${H}:r=${R}:d=1
-f lavfi -i color=c=green:s=${W}x${H}:r=${R}:d=29
-f lavfi -i color=c=black:s=${W}x${H}:r=${R}:d=1
-f lavfi -i testsrc2=s=${W}x${H}:r=${R}:d=29
-i tux_main.png
-i tux_logo.png
EOF
}

# Filter graph (constant string).
TUX_FILTERGRAPH="
[0:v][7:v]overlay=x='150+(W-w-300)*t/30':y='(H-h)/2':format=auto[seg_a];
[2:v][7:v]overlay=x='W-w-150-(W-w-300)*t/29':y='(H-h)/2':format=auto[seg_c];
[4:v][7:v]overlay=x='150+(W-w-300)*t/29':y='(H-h)/2':format=auto[seg_e_main];
[seg_e_main][8:v]overlay=x=W-w-80:y=80:format=auto[seg_e];
[seg_a][1:v][seg_c][3:v][seg_e][5:v][6:v]concat=n=7:v=1:a=0[outv]
"

# ---------- Duplicate-segment timeline (30s) ----------
# For Equal-Frame-Search testing (DVB ad-repeat use case).
# Two identical 10-second Tux sequences separated by a 2-second black gap,
# followed by 8 seconds of testsrc2 padding. The lavfi color+overlay filter
# graph is fully deterministic: rendering the same Tux-on-blue sequence
# twice with identical input parameters produces bit-identical YUV pixels.
# After the lossy encoder pass, the two segments remain visually identical
# and easily score below the equal-frame threshold (~10% per pixel).
#
#   0-10s   Tux moves L->R over BLUE        ← Sequence A (instance 1)
#   10-12s  BLACK gap                       ← simulates ad-block
#   12-22s  Tux moves L->R over BLUE        ← Sequence A (instance 2)
#   22-30s  testsrc2 checkerboard           ← non-matching tail
#
# Inputs (5):
#   [0]  blue 10s
#   [1]  black 2s
#   [2]  blue 10s (identical to [0])
#   [3]  testsrc2 8s
#   [4]  tux_main.png (overlay sprite)
build_duplicate_timeline_args() {
    local W=$1 H=$2 R=$3
    cat <<EOF
-f lavfi -i color=c=blue:s=${W}x${H}:r=${R}:d=10
-f lavfi -i color=c=black:s=${W}x${H}:r=${R}:d=2
-f lavfi -i color=c=blue:s=${W}x${H}:r=${R}:d=10
-f lavfi -i testsrc2=s=${W}x${H}:r=${R}:d=8
-i tux_main.png
EOF
}

DUPLICATE_FILTERGRAPH="
[0:v][4:v]overlay=x='150+(W-w-300)*t/10':y='(H-h)/2':format=auto[seg_a1];
[2:v][4:v]overlay=x='150+(W-w-300)*t/10':y='(H-h)/2':format=auto[seg_a2];
[seg_a1][1:v][seg_a2][3:v]concat=n=4:v=1:a=0[outv]
"

# ---------- HEVC 4K CRA-only Open-GOP ----------
generate_hevc4k_cra() {
    local BASE="tux_hevc4k_cra_test"
    if output_already_present "${BASE}.265"; then
        echo "==> HEVC 4K already present: ${BASE}.265 (use --force to regenerate)"
        write_ttcut_project "$BASE" "265" "ac3"
        return 0
    fi
    echo "==> Generating HEVC 4K (Main 10, CRA-only Open-GOP)..."
    render_tux
    gen_audio_ac3 "${BASE}.ac3"
    ffmpeg -y -hide_banner -loglevel warning -stats \
        $(build_tux_timeline_args 3840 2160 50) \
        -filter_complex "$TUX_FILTERGRAPH" \
        -map "[outv]" \
        -c:v libx265 -preset fast -pix_fmt yuv420p10le -profile:v main10 \
        -x265-params "keyint=50:min-keyint=50:bframes=4:b-pyramid=1:open-gop=1:scenecut=0:repeat-headers=1:log-level=error" \
        -an "${BASE}.265"
    write_ttcut_project "$BASE" "265" "ac3"
    echo "    Done: ${BASE}.265 / ${BASE}.ac3 / ${BASE}.ttcut"
}

# ---------- TODO functions for other codecs (filled in subsequent tasks) ----------
generate_h264_1080p_progressive() {
    local BASE="tux_h264_1080p_progressive_test"
    if output_already_present "${BASE}.264"; then
        echo "==> H.264 1080p progressive already present: ${BASE}.264 (use --force to regenerate)"
        write_ttcut_project "$BASE" "264" "ac3"
        return 0
    fi
    echo "==> Generating H.264 1080p progressive 50fps (High Profile)..."
    render_tux
    gen_audio_ac3 "${BASE}.ac3"
    ffmpeg -y -hide_banner -loglevel warning -stats \
        $(build_tux_timeline_args 1920 1080 50) \
        -filter_complex "$TUX_FILTERGRAPH" \
        -map "[outv]" \
        -c:v libx264 -preset fast -pix_fmt yuv420p -profile:v high \
        -x264-params "keyint=50:min-keyint=50:bframes=4:b-pyramid=normal:open-gop=1:scenecut=0:repeat-headers=1:force-cfr=1" \
        -an "${BASE}.264"
    write_ttcut_project "$BASE" "264" "ac3"
    if ffmpeg -v debug -i "${BASE}.264" -c copy -bsf:v trace_headers -t 1 -f null - 2>&1 \
        | grep -qE 'frame_mbs_only_flag.*0|MbInterlace|field_pic_flag.*1'; then
        echo "    WARNING: H.264 1080p output appears interlaced (expected progressive)" >&2
    fi
    echo "    Done: ${BASE}.264 / ${BASE}.ac3 / ${BASE}.ttcut"
}
generate_h264_1080i_mbaff() {
    local BASE="tux_h264_1080i_mbaff_test"
    if output_already_present "${BASE}.264"; then
        echo "==> H.264 1080i MBAFF already present: ${BASE}.264 (use --force to regenerate)"
        write_ttcut_project "$BASE" "264" "ac3"
        return 0
    fi
    echo "==> Generating H.264 1080i MBAFF 25fps (50 fields)..."
    render_tux
    gen_audio_ac3 "${BASE}.ac3"
    ffmpeg -y -hide_banner -loglevel warning -stats \
        $(build_tux_timeline_args 1920 1080 25) \
        -filter_complex "$TUX_FILTERGRAPH" \
        -map "[outv]" \
        -c:v libx264 -preset fast -pix_fmt yuv420p -profile:v high \
        -x264-params "keyint=25:min-keyint=25:bframes=4:b-pyramid=normal:scenecut=0:repeat-headers=1:interlaced=1:tff=1" \
        -an "${BASE}.264"
    write_ttcut_project "$BASE" "264" "ac3"
    if ! ffmpeg -v debug -i "${BASE}.264" -c copy -bsf:v trace_headers -t 1 -f null - 2>&1 \
        | grep -qE 'mb_adaptive_frame_field_flag.*1'; then
        echo "    WARNING: H.264 MBAFF output is missing mb_adaptive_frame_field_flag=1" >&2
    fi
    echo "    Done: ${BASE}.264 / ${BASE}.ac3 / ${BASE}.ttcut"
}
generate_h264_1080i_paff() {
    local BASE="tux_h264_1080i_paff_test"
    if output_already_present "${BASE}.264"; then
        echo "==> H.264 1080i PAFF already present: ${BASE}.264 (use --force to regenerate)"
        write_ttcut_project "$BASE" "264" "ac3"
        return 0
    fi
    echo "==> Generating H.264 1080i PAFF via JM Reference Encoder (slow)..."
    if [[ ! -x "$JM_LENCOD" ]]; then
        echo "    SKIP: JM lencod_static not found at $JM_LENCOD" >&2
        return 0
    fi
    render_tux
    gen_audio_ac3 "${BASE}.ac3"

    local RAW="${BASE}.yuv"
    echo "    Step 1/3: dump raw YUV (~7 GB)..."
    ffmpeg -y -hide_banner -loglevel warning -stats \
        $(build_tux_timeline_args 1920 1080 25) \
        -filter_complex "$TUX_FILTERGRAPH" \
        -map "[outv]" \
        -c:v rawvideo -pix_fmt yuv420p -f rawvideo \
        "$RAW"

    echo "    Step 2/3: JM encode (this is slow — expect 30+ min)..."
    local START=$(date +%s)
    timeout 7200 "$JM_LENCOD" -d "$JM_CFG_DIR/encoder_main.cfg" -f "$SCRIPTDIR/paff.cfg" \
        -p InputFile="$RAW" -p OutputFile="${BASE}.264" \
        > "${BASE}.jm.log" 2>&1 || {
        local rc=$?
        echo "    WARNING: JM encode failed or timed out (rc=$rc)" >&2
        rm -f "$RAW"
        return 1
    }
    local ELAPSED=$(( $(date +%s) - START ))
    echo "    JM encode finished in ${ELAPSED}s"

    echo "    Step 3/3: verify VUI + PAFF flags..."
    rm -f "$RAW"
    local trace
    trace=$(ffmpeg -v debug -i "${BASE}.264" -c copy -bsf:v trace_headers -t 1 -f null - 2>&1)
    if ! grep -qE 'vui_parameters_present_flag.*= 1' <<<"$trace"; then
        echo "    WARNING: JM output has no VUI (paff.cfg EnableVUISupport set?)" >&2
        return 1
    fi
    if ! grep -qE 'field_pic_flag.*= 1' <<<"$trace"; then
        echo "    WARNING: JM output is not PAFF (field_pic_flag missing)" >&2
        return 1
    fi
    write_ttcut_project "$BASE" "264" "ac3"
    echo "    Done: ${BASE}.264 / ${BASE}.ac3 / ${BASE}.ttcut"
}
generate_mpeg2_576i_pal() {
    local BASE="tux_mpeg2_576i_pal_test"
    if output_already_present "${BASE}.m2v"; then
        echo "==> MPEG-2 PAL DVB-SD already present: ${BASE}.m2v (use --force to regenerate)"
        write_ttcut_project "$BASE" "m2v" "mp2"
        return 0
    fi
    echo "==> Generating MPEG-2 PAL DVB-SD 720x576 25fps interlaced..."
    render_tux
    gen_audio_mp2 "${BASE}.mp2"
    ffmpeg -y -hide_banner -loglevel warning -stats \
        $(build_tux_timeline_args 720 576 25) \
        -filter_complex "$TUX_FILTERGRAPH" \
        -map "[outv]" \
        -c:v mpeg2video -pix_fmt yuv420p -aspect 4:3 \
        -flags +ilme+ildct -top 1 \
        -b:v 5M -minrate 5M -maxrate 9M -bufsize 1835008 \
        -force_key_frames "0,30,31,60,61,90,91" \
        -g 12 \
        -an "${BASE}.m2v"
    write_ttcut_project "$BASE" "m2v" "mp2"
    echo "    Done: ${BASE}.m2v / ${BASE}.mp2 / ${BASE}.ttcut"
}
# ---------- MPEG-2 576i field-picture variant ----------
generate_mpeg2_576i_fieldpic() {
    local BASE="tux_mpeg2_576i_fieldpic_test"
    local SRC="tux_mpeg2_576i_pal_test.m2v"
    if output_already_present "${BASE}.m2v"; then
        echo "==> MPEG-2 576i field-picture already present: ${BASE}.m2v (use --force to regenerate)"
        write_ttcut_project "$BASE" "m2v" "mp2"
        return 0
    fi
    echo "==> Generating MPEG-2 576i field-picture variant (inject every 50 frames)..."
    if [[ ! -s "$SRC" ]]; then
        generate_mpeg2_576i_pal
    fi
    python3 "$SCRIPTDIR/inject_fieldpictures.py" "$SRC" "${BASE}.m2v" --every 50
    if [[ ! -s "${BASE}.mp2" ]]; then
        cp "tux_mpeg2_576i_pal_test.mp2" "${BASE}.mp2"
    fi
    write_ttcut_project "$BASE" "m2v" "mp2"
    echo "    Done: ${BASE}.m2v / ${BASE}.mp2 / ${BASE}.ttcut"
}
generate_mpeg2_720p() {
    local BASE="tux_mpeg2_720p_test"
    if output_already_present "${BASE}.m2v"; then
        echo "==> MPEG-2 720p progressive already present: ${BASE}.m2v (use --force to regenerate)"
        write_ttcut_project "$BASE" "m2v" "mp2"
        return 0
    fi
    echo "==> Generating MPEG-2 720p progressive 50fps..."
    render_tux
    gen_audio_mp2 "${BASE}.mp2"
    ffmpeg -y -hide_banner -loglevel warning -stats \
        $(build_tux_timeline_args 1280 720 50) \
        -filter_complex "$TUX_FILTERGRAPH" \
        -map "[outv]" \
        -c:v mpeg2video -pix_fmt yuv420p -aspect 16:9 \
        -b:v 8M -minrate 8M -maxrate 12M -bufsize 1835008 \
        -force_key_frames "0,30,31,60,61,90,91" \
        -g 50 \
        -an "${BASE}.m2v"
    write_ttcut_project "$BASE" "m2v" "mp2"
    echo "    Done: ${BASE}.m2v / ${BASE}.mp2 / ${BASE}.ttcut"
}

# ============================================================================
# Duplicate-segment generators (Equal-Frame-Search test fixtures, 30s each)
# ============================================================================
# Test pattern (per file):
#   - Reference frame: index R within instance 1 of Sequence A
#   - Search start:    index S = first frame of instance 2 of Sequence A
#   - Expected match:  offset R from S (= same content position in A2)
#
# Frame indices per fps:
#   25 fps:  A1=0..249, BLACK=250..299, A2=300..549, padding=550..749
#            Suggested test: Ref=100 (in A1), Search=300, expect offset 100
#   50 fps:  A1=0..499, BLACK=500..599, A2=600..1099, padding=1100..1499
#            Suggested test: Ref=200 (in A1), Search=600, expect offset 200

# ---------- HEVC 4K CRA-only Open-GOP, duplicate ----------
generate_hevc4k_cra_duplicate() {
    local BASE="tux_hevc4k_cra_duplicate"
    if output_already_present "${BASE}.265"; then
        echo "==> HEVC 4K duplicate already present: ${BASE}.265 (use --force to regenerate)"
        write_ttcut_project "$BASE" "265" "ac3"
        return 0
    fi
    echo "==> Generating HEVC 4K duplicate (Main 10, CRA-only Open-GOP, 30s)..."
    render_tux
    gen_audio_ac3 "${BASE}.ac3" 30
    ffmpeg -y -hide_banner -loglevel warning -stats \
        $(build_duplicate_timeline_args 3840 2160 50) \
        -filter_complex "$DUPLICATE_FILTERGRAPH" \
        -map "[outv]" \
        -c:v libx265 -preset fast -pix_fmt yuv420p10le -profile:v main10 \
        -x265-params "keyint=50:min-keyint=50:bframes=4:b-pyramid=1:open-gop=1:scenecut=0:repeat-headers=1:log-level=error" \
        -an "${BASE}.265"
    write_ttcut_project "$BASE" "265" "ac3"
    echo "    Done: ${BASE}.265 / ${BASE}.ac3 / ${BASE}.ttcut"
}

# ---------- H.264 1080p progressive, duplicate ----------
generate_h264_1080p_progressive_duplicate() {
    local BASE="tux_h264_1080p_progressive_duplicate"
    if output_already_present "${BASE}.264"; then
        echo "==> H.264 1080p progressive duplicate already present: ${BASE}.264 (use --force to regenerate)"
        write_ttcut_project "$BASE" "264" "ac3"
        return 0
    fi
    echo "==> Generating H.264 1080p progressive duplicate 50fps (High Profile, 30s)..."
    render_tux
    gen_audio_ac3 "${BASE}.ac3" 30
    ffmpeg -y -hide_banner -loglevel warning -stats \
        $(build_duplicate_timeline_args 1920 1080 50) \
        -filter_complex "$DUPLICATE_FILTERGRAPH" \
        -map "[outv]" \
        -c:v libx264 -preset fast -pix_fmt yuv420p -profile:v high \
        -x264-params "keyint=50:min-keyint=50:bframes=4:b-pyramid=normal:open-gop=1:scenecut=0:repeat-headers=1:force-cfr=1" \
        -an "${BASE}.264"
    write_ttcut_project "$BASE" "264" "ac3"
    echo "    Done: ${BASE}.264 / ${BASE}.ac3 / ${BASE}.ttcut"
}

# ---------- H.264 1080i MBAFF, duplicate ----------
generate_h264_1080i_mbaff_duplicate() {
    local BASE="tux_h264_1080i_mbaff_duplicate"
    if output_already_present "${BASE}.264"; then
        echo "==> H.264 1080i MBAFF duplicate already present: ${BASE}.264 (use --force to regenerate)"
        write_ttcut_project "$BASE" "264" "ac3"
        return 0
    fi
    echo "==> Generating H.264 1080i MBAFF duplicate 25fps (50 fields, 30s)..."
    render_tux
    gen_audio_ac3 "${BASE}.ac3" 30
    ffmpeg -y -hide_banner -loglevel warning -stats \
        $(build_duplicate_timeline_args 1920 1080 25) \
        -filter_complex "$DUPLICATE_FILTERGRAPH" \
        -map "[outv]" \
        -c:v libx264 -preset fast -pix_fmt yuv420p -profile:v high \
        -x264-params "keyint=25:min-keyint=25:bframes=4:b-pyramid=normal:scenecut=0:repeat-headers=1:interlaced=1:tff=1" \
        -an "${BASE}.264"
    write_ttcut_project "$BASE" "264" "ac3"
    echo "    Done: ${BASE}.264 / ${BASE}.ac3 / ${BASE}.ttcut"
}

# ---------- MPEG-2 PAL DVB-SD, duplicate ----------
generate_mpeg2_576i_pal_duplicate() {
    local BASE="tux_mpeg2_576i_pal_duplicate"
    if output_already_present "${BASE}.m2v"; then
        echo "==> MPEG-2 PAL DVB-SD duplicate already present: ${BASE}.m2v (use --force to regenerate)"
        write_ttcut_project "$BASE" "m2v" "mp2"
        return 0
    fi
    echo "==> Generating MPEG-2 PAL DVB-SD duplicate 720x576 25fps (30s)..."
    render_tux
    gen_audio_mp2 "${BASE}.mp2" 30
    ffmpeg -y -hide_banner -loglevel warning -stats \
        $(build_duplicate_timeline_args 720 576 25) \
        -filter_complex "$DUPLICATE_FILTERGRAPH" \
        -map "[outv]" \
        -c:v mpeg2video -pix_fmt yuv420p -aspect 4:3 \
        -flags +ilme+ildct -top 1 \
        -b:v 5M -minrate 5M -maxrate 9M -bufsize 1835008 \
        -force_key_frames "0,10,12,22" \
        -g 12 \
        -an "${BASE}.m2v"
    write_ttcut_project "$BASE" "m2v" "mp2"
    echo "    Done: ${BASE}.m2v / ${BASE}.mp2 / ${BASE}.ttcut"
}

# ---------- MPEG-2 576i field-picture variant, duplicate ----------
generate_mpeg2_576i_fieldpic_duplicate() {
    local BASE="tux_mpeg2_576i_fieldpic_duplicate"
    local SRC="tux_mpeg2_576i_pal_duplicate.m2v"
    if output_already_present "${BASE}.m2v"; then
        echo "==> MPEG-2 576i field-picture duplicate already present: ${BASE}.m2v (use --force to regenerate)"
        write_ttcut_project "$BASE" "m2v" "mp2"
        return 0
    fi
    echo "==> Generating MPEG-2 576i field-picture duplicate (inject every 50 frames, 30s)..."
    if [[ ! -s "$SRC" ]]; then
        generate_mpeg2_576i_pal_duplicate
    fi
    python3 "$SCRIPTDIR/inject_fieldpictures.py" "$SRC" "${BASE}.m2v" --every 50
    if [[ ! -s "${BASE}.mp2" ]]; then
        cp "tux_mpeg2_576i_pal_duplicate.mp2" "${BASE}.mp2"
    fi
    write_ttcut_project "$BASE" "m2v" "mp2"
    echo "    Done: ${BASE}.m2v / ${BASE}.mp2 / ${BASE}.ttcut"
}

# ---------- MPEG-2 720p progressive, duplicate ----------
generate_mpeg2_720p_duplicate() {
    local BASE="tux_mpeg2_720p_duplicate"
    if output_already_present "${BASE}.m2v"; then
        echo "==> MPEG-2 720p duplicate already present: ${BASE}.m2v (use --force to regenerate)"
        write_ttcut_project "$BASE" "m2v" "mp2"
        return 0
    fi
    echo "==> Generating MPEG-2 720p progressive duplicate 50fps (30s)..."
    render_tux
    gen_audio_mp2 "${BASE}.mp2" 30
    ffmpeg -y -hide_banner -loglevel warning -stats \
        $(build_duplicate_timeline_args 1280 720 50) \
        -filter_complex "$DUPLICATE_FILTERGRAPH" \
        -map "[outv]" \
        -c:v mpeg2video -pix_fmt yuv420p -aspect 16:9 \
        -b:v 8M -minrate 8M -maxrate 12M -bufsize 1835008 \
        -force_key_frames "0,10,12,22" \
        -g 50 \
        -an "${BASE}.m2v"
    write_ttcut_project "$BASE" "m2v" "mp2"
    echo "    Done: ${BASE}.m2v / ${BASE}.mp2 / ${BASE}.ttcut"
}

# ---------- Dispatch ----------
ARG="${1:-all}"
case "$ARG" in
    hevc4k)
        generate_hevc4k_cra
        ;;
    h264)
        generate_h264_1080p_progressive
        generate_h264_1080i_mbaff
        generate_h264_1080i_paff
        ;;
    mpeg2)
        generate_mpeg2_576i_pal
        generate_mpeg2_720p
        generate_mpeg2_576i_fieldpic
        ;;
    mpeg2_576i_fieldpic)
        generate_mpeg2_576i_fieldpic
        ;;
    paff)
        generate_h264_1080i_paff
        ;;
    duplicate)
        # Equal-Frame-Search test fixtures (PAFF deferred — JM Reference too slow)
        generate_hevc4k_cra_duplicate
        generate_h264_1080p_progressive_duplicate
        generate_h264_1080i_mbaff_duplicate
        generate_mpeg2_576i_pal_duplicate
        generate_mpeg2_576i_fieldpic_duplicate
        generate_mpeg2_720p_duplicate
        ;;
    all)
        generate_hevc4k_cra
        generate_h264_1080p_progressive
        generate_h264_1080i_mbaff
        generate_h264_1080i_paff
        generate_mpeg2_576i_pal
        generate_mpeg2_720p
        generate_mpeg2_576i_fieldpic
        generate_hevc4k_cra_duplicate
        generate_h264_1080p_progressive_duplicate
        generate_h264_1080i_mbaff_duplicate
        generate_mpeg2_576i_pal_duplicate
        generate_mpeg2_576i_fieldpic_duplicate
        generate_mpeg2_720p_duplicate
        ;;
    *)
        echo "Unknown argument: $ARG" >&2
        echo "Usage: $0 [all|hevc4k|h264|mpeg2|mpeg2_576i_fieldpic|paff|duplicate]" >&2
        exit 1
        ;;
esac

echo "==> All requested generations complete."
