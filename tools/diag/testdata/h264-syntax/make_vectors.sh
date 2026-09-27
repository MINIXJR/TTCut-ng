#!/bin/bash
# make_vectors.sh - small H.264/H.265 streams for test_h264_syntax_golden.
# Generated ONCE and committed: an ffmpeg/x264/x265 update must not shift
# the golden inputs. Rerun only to add a vector, and then re-record nothing:
# a new vector gets its golden lines from the OLD code (see the plan).
#   tools/diag/testdata/h264-syntax/make_vectors.sh
set -euo pipefail
cd "$(dirname "$0")"
src=(-f lavfi -i testsrc2=s=64x64:r=25:d=0.4)
enc() { ffmpeg -v error -y "${src[@]}" "$@"; }

enc -pix_fmt yuv420p -x264-params cqm=jvt -c:v libx264 -threads 1 -f h264 cqm.264
enc -pix_fmt yuv444p -profile:v high444 -c:v libx264 -threads 1 -f h264 yuv444.264
enc -pix_fmt yuv420p -x264-params nal-hrd=vbr:vbv-maxrate=500:vbv-bufsize=1000 \
    -c:v libx264 -threads 1 -f h264 hrd.264
enc -pix_fmt yuv420p -x264-params weightp=2:b-pyramid=strict:bframes=3 \
    -c:v libx264 -threads 1 -f h264 weightp.264
enc -pix_fmt yuv420p -x264-params interlaced=1 -c:v libx264 -threads 1 -f h264 mbaff.264
# Encoder-side HEVC like the seam re-encode: no B-frames, main10, the
# SPS-derived x265 params TTESSmartCut::deriveX265SeamParams sets.
enc -pix_fmt yuv420p10le -profile:v main10 -c:v libx265 \
    -x265-params "bframes=0:frame-threads=1:pools=none:log-level=error:tu-intra-depth=1:tu-inter-depth=1:amp=0:sao=1:tmvp=1:strong-intra-smoothing=1" \
    -f hevc x265enc.265
# HEVC branches the seam encoder never emits: conformance window (66x66),
# scaling lists without data, deblocking offsets in the PPS, luma+chroma
# weights (the fade makes x265 use them).
ffmpeg -v error -y -f lavfi -i testsrc2=s=66x66:r=25:d=0.4 -vf "fade=in:0:8" \
    -pix_fmt yuv420p10le -profile:v main10 -c:v libx265 \
    -x265-params "bframes=0:frame-threads=1:pools=none:log-level=error:scaling-list=default:deblock=2,-1:weightp=1" \
    -f hevc x265misc.265
# Raw ES WITHOUT SPS timing, for the frame-rate fallback (spec
# 2026-09-27-frame-rate-source): libav then reports codecpar->framerate 0/1
# and r_frame_rate 1200000/1. H.264 from the JM reference encoder with the
# VUI switched off (x264 always writes VUI timing), HEVC from x265.
JM=/usr/local/src/jm-reference
if [ -x "$JM/bin/lencod_static" ]; then
    ffmpeg -v error -y -f lavfi -i testsrc2=s=64x64:r=25:d=0.4 -pix_fmt yuv420p \
        -f rawvideo novui.yuv
    "$JM/bin/lencod_static" -d "$JM/cfg/encoder_main.cfg" \
        -p InputFile=novui.yuv -p OutputFile=novui.264 -p ReconFile=/dev/null \
        -p SourceWidth=64 -p SourceHeight=64 -p FramesToBeEncoded=10 \
        -p FrameRate=25 -p EnableVUISupport=0 > /dev/null
    rm -f novui.yuv log.dat stats.dat data.txt leakybucketparam.cfg
else
    echo "JM not found at $JM - novui.264 not regenerated" >&2
fi
enc -pix_fmt yuv420p -c:v libx265 \
    -x265-params "vui-timing-info=0:log-level=error:frame-threads=1:pools=none" \
    -f hevc novui.265
ls -l ./*.264 ./*.265
