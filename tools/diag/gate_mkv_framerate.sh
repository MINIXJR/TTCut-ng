#!/bin/bash
# gate_mkv_framerate.sh — the MKV video timeline must follow the frame rate.
#
# The matroska muxer stores timestamps in milliseconds. TTMkvMergeProvider
# synthesizes the video PTS from a frame counter; before code-audit run 7 it
# rounded the frame duration to whole milliseconds once and multiplied that,
# so 29.97 fps ran 1.1 % short (-659 ms per minute) and 23.976 fps 0.7 % long
# against the audio. 25 fps (40 ms exactly) was never affected.
#
# Makes a 60 s H.264 ES per rate (ffmpeg testsrc2, B-frames on), cuts and muxes
# it with test_mkvmux (Smart Cut + the app's mux configuration) and checks the
# last video PTS against (frames - 1) / fps: at most 1 ms off.
#
#   tools/diag/gate_mkv_framerate.sh <workdir>
set -u
D="$(cd "$(dirname "$0")" && pwd)"
W=${1:?usage: $0 <workdir>}
BIN="$D/test_mkvmux"
[ -x "$BIN" ] || { echo "SKIP: not built: $BIN"; exit 77; }
command -v ffmpeg >/dev/null && command -v ffprobe >/dev/null \
    || { echo "SKIP: ffmpeg/ffprobe missing"; exit 77; }
ffmpeg -hide_banner -encoders 2>/dev/null | grep -q libx264 \
    || { echo "SKIP: ffmpeg without libx264"; exit 77; }
mkdir -p "$W"
# test_mkvmux reads the frame rate with atof(): under a German locale
# "29.97" parses as 29.
export LC_ALL=C

ok=1
#     name       ffmpeg rate   fps value      gop
for spec in "ntsc  30000/1001  29.97002997  30" \
            "film  24000/1001  23.97602398  24" \
            "pal   25          25           25"; do
  set -- $spec
  name=$1 rate=$2 fps=$3 gop=$4
  es="$W/$name.264"
  ffmpeg -y -v error -f lavfi -i "testsrc2=size=320x240:rate=$rate" -t 60 \
      -c:v libx264 -preset ultrafast -bf 2 -g "$gop" -f h264 "$es" \
      || { echo "FAIL: $name: ffmpeg could not make the ES"; ok=0; continue; }
  frames=$(ffprobe -v error -count_frames -select_streams v:0 \
      -show_entries stream=nb_read_frames -of csv=p=0 "$es")
  "$BIN" "$es" "$W/$name.mkv" 0 $((frames - 1)) "$fps" > "$W/$name.order" 2> "$W/$name.log" \
      || { echo "FAIL: $name: test_mkvmux failed (see $W/$name.log)"; ok=0; continue; }
  if grep -q 'display-PTS list' "$W/$name.log"; then
    echo "FAIL: $name: display-PTS list warning"; ok=0
  fi
  read -r n last < <(ffprobe -v error -select_streams v:0 \
      -show_entries packet=pts_time -of csv=p=0 "$W/$name.mkv" \
      | sort -g | awk 'END { print NR, $1 }')
  verdict=$(awk -v n="$n" -v last="$last" -v fps="$fps" 'BEGIN {
      want = (n - 1) / fps; d = (last - want) * 1000
      printf "%s %d frames, last pts %.3f s, expected %.3f s, diff %+.1f ms",
             (d <= 1 && d >= -1) ? "PASS" : "FAIL", n, last, want, d }')
  echo "$name: $verdict"
  case $verdict in PASS*) ;; *) ok=0 ;; esac
done

if [ $ok = 1 ]; then echo "PASS: MKV video timeline follows the frame rate"; exit 0; fi
echo "FAIL: MKV video timeline drifts from the frame rate"
exit 1
