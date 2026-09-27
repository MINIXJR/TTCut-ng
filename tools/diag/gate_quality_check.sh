#!/bin/bash
# Gate for tools/ttcut-quality-check (audit run 18, map quality-check.md).
# Builds its own material from the Tux progressive H.264 fixture and pink
# noise (a steady tone correlates at every lag - it cannot show an offset):
#
#   Q1  the defect-region settings are read from TTCut-ng.conf as TTCut-ng
#       writes it ([Settings], Common\ExtraFrameClusterGap)
#   Q2  an A/V offset that starts in the second segment is found
#   T   a steady tone gives "not measurable", not a PASS
#   Q3  a correct 29.97 fps MKV passes the PTS test
#   Q5  one reference MKV, and the temp directory lies next to the cut
#
#   usage: gate_quality_check.sh <video.264> <tone.ac3> <workdir>
set -u
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
TOOL="$ROOT/tools/ttcut-quality-check/ttcut-quality-check.py"
V=${1:?usage: $0 <video.264> <tone.ac3> <workdir>}
TONE=${2:?}
W=${3:?}
mkdir -p "$W"
cd "$W" || exit 2
fail=0
check() { if [ "$1" = 0 ]; then echo "PASS: $2"; else echo "FAIL: $2"; fail=1; fi; }
CUTS="100-899,1300-2099,2300-2799"

ffmpeg -y -v error -f lavfi -i "anoisesrc=d=120:c=pink:r=48000:a=0.3" \
    -af "aformat=channel_layouts=stereo" -c:a ac3 -b:a 448k noise.ac3 || exit 1
# The cut of $CUTS with the noise track; DELAY ms of offset from segment 2 on.
make_cut() {
  local out=$1 audio=$2 d=$3
  ffmpeg -y -v error -r 50 -i "$V" -i "$audio" -filter_complex "\
[0:v]trim=start_frame=100:end_frame=900,setpts=PTS-STARTPTS[v1];\
[0:v]trim=start_frame=1300:end_frame=2100,setpts=PTS-STARTPTS[v2];\
[0:v]trim=start_frame=2300:end_frame=2800,setpts=PTS-STARTPTS[v3];\
[1:a]atrim=2:18,asetpts=PTS-STARTPTS[a1];\
[1:a]atrim=26:42,asetpts=PTS-STARTPTS,adelay=$d|$d,atrim=0:16[a2];\
[1:a]atrim=46:56,asetpts=PTS-STARTPTS,adelay=$d|$d,atrim=0:10[a3];\
[v1][a1][v2][a2][v3][a3]concat=n=3:v=1:a=1[v][a]" \
    -map "[v]" -map "[a]" -c:v libx264 -preset ultrafast -g 50 -c:a ac3 -b:a 448k "$out"
}
make_cut good.mkv noise.ac3 0 || exit 1
make_cut desync.mkv noise.ac3 200 || exit 1
make_cut tone.mkv "$TONE" 0 || exit 1
qc() { python3 "$TOOL" --video "$V" --cuts "$CUTS" --fps 50 "$@" 2>&1; }

# Q1
mkdir -p home/.config/TTCut-ng
printf '[Settings]\nCommon\\ExtraFrameClusterGap=9\nCommon\\ExtraFrameClusterOffset=4\n' \
    > home/.config/TTCut-ng/TTCut-ng.conf
printf '[video]\nframe_rate=50/1\n\n[warnings]\nes_doubled_pts_aus=150,151,500\nes_total_aus=6000\n' > q1.info
out=$(HOME="$W/home" qc --audio noise.ac3 --cut good.mkv --info q1.info --tests defects)
echo "$out" | grep -q "defect-gap=9s" && echo "$out" | grep -q "in 1 regions"
check $? "Q1: gap 9 s from TTCut-ng.conf, one region - got: $(echo "$out" | grep -m1 'Settings:' | sed 's/^ *//')"

# Q2 and the reference case
out=$(qc --audio noise.ac3 --cut good.mkv --tests avsync)
echo "$out" | grep -q "^\[PASS\] A/V Sync"
check $? "Q2: a correct cut passes A/V sync - got: $(echo "$out" | grep -m1 '^\[.*A/V Sync')"
out=$(qc --audio noise.ac3 --cut desync.mkv --tests avsync)
echo "$out" | grep -q "^\[FAIL\] A/V Sync"
check $? "Q2: 200 ms from segment 2 on fails A/V sync - got: $(echo "$out" | grep -m1 '^\[.*A/V Sync')"

# T
out=$(qc --audio "$TONE" --cut tone.mkv --tests avsync)
echo "$out" | grep -q "^\[WARN\] A/V Sync.*not measurable"
check $? "T: a steady tone is not measurable - got: $(echo "$out" | grep -m1 '^\[.*A/V Sync')"

# Q3
ffmpeg -y -v error -f lavfi -i "testsrc2=size=320x240:rate=30000/1001" \
    -f lavfi -i "anoisesrc=d=20:c=pink:r=48000" -t 20 -c:v libx264 -preset ultrafast -c:a ac3 ntsc.mkv || exit 1
ffmpeg -y -v error -f lavfi -i "testsrc2=size=320x240:rate=30000/1001" -t 20 \
    -c:v libx264 -preset ultrafast -f h264 ntsc.264 || exit 1
out=$(python3 "$TOOL" --video ntsc.264 --audio noise.ac3 --cut ntsc.mkv --cuts 0-599 --fps 29.97 --tests timing 2>&1)
echo "$out" | grep -q "^\[PASS\] PTS Consistency"
check $? "Q3: a correct 29.97 fps MKV passes - got: $(echo "$out" | grep -m1 '^\[.*PTS')"

# Q5
out=$(qc --audio noise.ac3 --cut good.mkv --tests visual,avsync --keep-tmpdir)
tmp=$(echo "$out" | sed -n 's/^Tmpdir: //p')
nref=$(find "$tmp" -maxdepth 1 -name '*.mkv' 2>/dev/null | wc -l)
[ "$(realpath "$(dirname "$tmp")")" = "$(realpath "$W")" ] && [ "$nref" -eq 1 ]
check $? "Q5: one reference MKV in a temp directory next to the cut - got: $nref in $tmp"
rm -rf "$tmp"

exit $fail
