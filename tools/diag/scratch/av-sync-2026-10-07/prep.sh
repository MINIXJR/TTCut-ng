#!/bin/bash
# prep.sh TAG ORIGINAL_TS VARIANT... : first 80 MB -> demux per variant -> project -> headless cut
D=/usr/local/src/CLAUDE_TMP/TTCut-ng/demux-shift
R=/usr/local/src/TTCut-ng
export XDG_CACHE_HOME=/usr/local/src/CLAUDE_TMP/TTCut-ng/xdg-cache
tag=$1; src=$2; shift 2
W="$D/chain/$tag"; mkdir -p "$W"; cd "$W" || exit 1
[ -f head.ts ] || head -c "${HEAD_BYTES:-80000000}" "$src" > head.ts
for v in "$@"; do
    rm -rf "out-$v"; mkdir -p "out-$v"
    "$D/variants/$v" -e --no-subs -n t head.ts "out-$v/" > "run-$v.log" 2>&1 </dev/null
    echo "$tag/$v demux exit $?"
    vid=$(ls "$W/out-$v"/t.264 "$W/out-$v"/t.h264 "$W/out-$v"/t.265 "$W/out-$v"/t.h265 "$W/out-$v"/t.hevc "$W/out-$v"/t.m2v 2>/dev/null | head -1)
    {
      echo '<!DOCTYPE TTCut-Projectfile>'; echo '<TTCut-Projectfile>'; echo ' <Version>1.0</Version>'
      echo ' <Video>'; echo '  <Order>0</Order>'; echo "  <Name>$vid</Name>"
      n=0
      while IFS= read -r f; do
        echo "  <Audio><Order>$n</Order><Name>$W/out-$v/$f</Name></Audio>"; n=$((n+1))
      done < <(sed -n 's/^audio_[0-9]*_file=//p' "out-$v/t.info")
      echo "  <Cut><Order>0</Order><CutIn>500</CutIn><CutOut>2000</CutOut></Cut>"
      echo ' </Video>'; echo '</TTCut-Projectfile>'
    } > "p-$v.ttcut"
    "$R/tools/diag/acm-cut.sh" "p-$v.ttcut" "$W/cut-$v.mkv" 2>&1 | head -3
done
