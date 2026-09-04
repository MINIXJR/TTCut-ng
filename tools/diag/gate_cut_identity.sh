#!/bin/bash
# gate_cut_identity.sh — bit-identity gate for --auto-cut across a refactor.
#
# Runs the same five Tux projects through two binaries and compares the
# results per stream: video MD5, audio MD5 and the packet list (pts, dts,
# size, flags, payload MD5). MKVs are never byte-identical (segment UID),
# so the container is not compared.
#
#   tools/diag/gate_cut_identity.sh ref  <ttcut-ng>   capture the reference
#   tools/diag/gate_cut_identity.sh cand <ttcut-ng>   capture and compare
#
# Projects are written into WORK from the fixture paths on every run, so the
# cut positions below are the single source. Moved here on 2026-09-04 from
# the code-audit run's temp directory (run-qc.sh), extended from three to
# five fixtures.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CACHE="$ROOT/tools/test-videos/cache"
WORK=/usr/local/src/CLAUDE_TMP/TTCut-ng/cut-identity
export QT_QPA_PLATFORM=offscreen

tag=${1:-}; bin=${2:-}
[ "$tag" = ref ] || [ "$tag" = cand ] || { sed -n '2,13p' "$0"; exit 1; }
[ -x "$bin" ] || { echo "FAIL: binary missing ($bin)"; exit 1; }
mkdir -p "$WORK"

# fixture  video-ext audio-ext  cut1  cut2  cut3   (cut = in-out, display frames)
FIXTURES="
tux_mpeg2_576i_pal_test        m2v mp2 0-740  800-1480  1550-2200
tux_h264_1080p_progressive_test 264 ac3 0-1480 1600-2960 3100-4400
tux_h264_1080i_mbaff_test      264 ac3 0-740  800-1480  1550-2200
tux_h264_1080i_paff_test       264 ac3 0-740  800-1480  1550-2200
tux_hevc4k_cra_test            265 ac3 0-1480 1600-2960 3100-4400
"

write_project() {   # name vext aext cuts...
  local name=$1 vext=$2 aext=$3; shift 3
  local prj="$WORK/$name.ttcut" order=0
  {
    echo '<!DOCTYPE TTCut-Projectfile>'
    echo '<TTCut-Projectfile>'
    echo ' <Version>1.0</Version>'
    echo ' <Video>'
    echo '  <Order>0</Order>'
    echo "  <Name>$CACHE/$name.$vext</Name>"
    echo '  <Audio>'
    echo '   <Order>0</Order>'
    echo "   <Name>$CACHE/$name.$aext</Name>"
    echo '   <Language>deu</Language>'
    echo '  </Audio>'
    for c in "$@"; do
      echo '  <Cut>'
      echo "   <Order>$order</Order>"
      echo "   <CutIn>${c%-*}</CutIn>"
      echo "   <CutOut>${c#*-}</CutOut>"
      echo '  </Cut>'
      order=$((order+1))
    done
    echo ' </Video>'
    echo '</TTCut-Projectfile>'
  } > "$prj"
}

ok=1
while read -r name vext aext c1 c2 c3; do
  [ -n "$name" ] || continue
  [ -f "$CACHE/$name.$vext" ] || { echo "FAIL: fixture missing $CACHE/$name.$vext"; ok=0; continue; }
  write_project "$name" "$vext" "$aext" "$c1" "$c2" "$c3"
  out="$WORK/${tag}_$name.mkv"; rm -f "$out"
  timeout 1200 "$bin" --project "$WORK/$name.ttcut" --auto-cut "$out" > "$WORK/$tag.$name.log" 2>&1
  echo "$tag $name rc=$? size=$(stat -c %s "$out" 2>/dev/null)"
  ffmpeg -v error -i "$out" -map v:0 -c copy -f md5 - > "$WORK/$tag.$name.vmd5" 2>/dev/null
  ffmpeg -v error -i "$out" -map a:0 -c copy -f md5 - > "$WORK/$tag.$name.amd5" 2>/dev/null
  ffprobe -v error -show_packets -show_entries packet=stream_index,pts,dts,size,flags \
      -show_data_hash md5 -of csv=nk=1:p=0 "$out" > "$WORK/$tag.$name.pk.csv" 2>/dev/null
  [ -s "$WORK/$tag.$name.pk.csv" ] || { echo "FAIL: $name produced no packets"; ok=0; }
done <<< "$FIXTURES"

if [ "$tag" = cand ]; then
  while read -r name rest; do
    [ -n "$name" ] || continue
    if cmp -s "$WORK/ref.$name.pk.csv" "$WORK/cand.$name.pk.csv" \
       && cmp -s "$WORK/ref.$name.vmd5" "$WORK/cand.$name.vmd5" \
       && cmp -s "$WORK/ref.$name.amd5" "$WORK/cand.$name.amd5"; then
      echo "$name: identical ($(wc -l < "$WORK/ref.$name.pk.csv") packets)"
    else echo "$name: DIFFERENT"; ok=0; fi
  done <<< "$FIXTURES"
  [ $ok = 1 ] && echo "QC: IDENTICAL" || { echo "QC: DIFFERENT"; exit 1; }
fi
[ $ok = 1 ]
