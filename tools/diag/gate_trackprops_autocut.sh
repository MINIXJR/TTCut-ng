#!/bin/bash
# gate_trackprops_autocut.sh — do track name and flags reach the MKV of a cut?
#
# Writes a project per codec with one audio track (<TrackName>Hörfilm,
# <TrackFlags>original,visual-impaired) and one subtitle (<TrackFlags>
# hearing-impaired,forced), cuts it headless with --auto-cut and reads the
# streams back with ffprobe. H.264 goes through TTH26xCutTask, MPEG-2 through
# the mux list (TTAVData -> TTMuxTask).
#
#   tools/diag/gate_trackprops_autocut.sh <workdir> <app> <h264> <ac3> <m2v> <mp2>
set -u
W=${1:?usage: $0 <workdir> <app> <h264> <ac3> <m2v> <mp2>}
APP=$2
command -v ffprobe >/dev/null || { echo "SKIP: ffprobe missing"; exit 77; }
[ -x "$APP" ] || { echo "SKIP: not built: $APP"; exit 77; }
mkdir -p "$W"

ok=1
run_case() {
  local name=$1 video=$2 audio=$3 dir="$W/$1"
  rm -rf "$dir"; mkdir -p "$dir/out"
  printf '1\n00:00:03,000 --> 00:00:04,000\nOne\n\n2\n00:00:05,000 --> 00:00:06,000\nTwo\n\n' > "$dir/s_deu.srt"
  cat > "$dir/p.ttcut" <<EOF
<!DOCTYPE TTCut-Projectfile>
<TTCut-Projectfile>
 <Version>1.0</Version>
 <Video>
  <Order>0</Order>
  <Name>$video</Name>
  <Audio><Order>0</Order><Name>$audio</Name><Language>deu</Language><TrackName>Hörfilm</TrackName><TrackFlags>original,visual-impaired</TrackFlags></Audio>
  <Cut><Order>0</Order><CutIn>50</CutIn><CutOut>349</CutOut></Cut>
  <Subtitle><Order>0</Order><Name>$dir/s_deu.srt</Name><Language>deu</Language><TrackFlags>hearing-impaired,forced</TrackFlags></Subtitle>
 </Video>
</TTCut-Projectfile>
EOF
  if ! LC_ALL=C.UTF-8 "$APP" --project "$dir/p.ttcut" --auto-cut "$dir/out/p.mkv" > "$dir/app.log" 2>&1; then
    echo "FAIL: $name: auto-cut failed (log $dir/app.log)"; ok=0; return
  fi
  local probe
  probe=$(ffprobe -v error -show_entries \
    stream=codec_type:stream_tags=title:stream_disposition=original,visual_impaired,hearing_impaired,forced \
    -of compact "$dir/out/p.mkv")
  while IFS= read -r line; do echo "  $name: $line"; done <<< "$probe"
  local a s
  a=$(grep 'codec_type=audio' <<< "$probe")
  s=$(grep 'codec_type=subtitle' <<< "$probe")
  if grep -q 'disposition:original=1' <<< "$a" && grep -q 'disposition:visual_impaired=1' <<< "$a" \
     && grep -q 'disposition:forced=0' <<< "$a" && grep -q 'tag:title=Hörfilm' <<< "$a"; then
    echo "PASS: $name: audio title Hörfilm, original + visually impaired"
  else
    echo "FAIL: $name: audio title/flags missing"; ok=0
  fi
  if grep -q 'disposition:hearing_impaired=1' <<< "$s" && grep -q 'disposition:forced=1' <<< "$s" \
     && grep -q 'disposition:visual_impaired=0' <<< "$s"; then
    echo "PASS: $name: subtitle hearing impaired + forced"
  else
    echo "FAIL: $name: subtitle flags missing"; ok=0
  fi
}

run_case h264  "$3" "$4"
run_case mpeg2 "$5" "$6"

[ $ok = 1 ] && { echo "PASS: track name and flags reach the MKV (H.264 and MPEG-2)"; exit 0; }
exit 1
