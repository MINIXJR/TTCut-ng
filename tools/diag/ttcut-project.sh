# shellcheck shell=bash
# Sourced by the gate scripts: the TTCut project file of one video.
#
#   ttcut_project_xml <video> <audio|-> <language|-> [<cut-in>:<cut-out> ...] [-- <xml line> ...]
#
# Prints the project to stdout: one video (order 0), no or one audio track
# (order 0, with a language only when one is given), the cuts in the given
# order, and after </Video> any extra XML lines, e.g. a <Settings> block.
# Projects with more tracks or more videos write their own XML.
ttcut_project_xml() {
  local video=$1 audio=$2 lang=$3 order=0
  shift 3
  echo '<!DOCTYPE TTCut-Projectfile>'
  echo '<TTCut-Projectfile>'
  echo ' <Version>1.0</Version>'
  echo ' <Video>'
  echo '  <Order>0</Order>'
  echo "  <Name>$video</Name>"
  if [ "$audio" != "-" ]; then
    if [ "$lang" != "-" ]; then
      echo "  <Audio><Order>0</Order><Name>$audio</Name><Language>$lang</Language></Audio>"
    else
      echo "  <Audio><Order>0</Order><Name>$audio</Name></Audio>"
    fi
  fi
  while [ $# -gt 0 ] && [ "$1" != "--" ]; do
    echo "  <Cut><Order>$order</Order><CutIn>${1%%:*}</CutIn><CutOut>${1##*:}</CutOut></Cut>"
    order=$((order + 1))
    shift
  done
  echo ' </Video>'
  if [ "${1:-}" = "--" ]; then
    shift
    printf '%s\n' "$@"
  fi
  echo '</TTCut-Projectfile>'
}
