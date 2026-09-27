#!/bin/bash
# Gate for tools/vdr-demux-example.sh (audit run 17, map demux-helpers.md, D2):
# two recordings whose episode directories carry the same name must both
# survive one run - the second used to overwrite the first's outputs and log.
#
# Runs the real example script without any dialog: HOME points into the work
# directory (IN_PFAD/OUT_PFAD derive from it), a stub kdialog on the PATH
# selects every offered recording and answers "no" to every question, and the
# source-tree ttcut-demux comes first on the PATH.
#
#   usage: gate_vdr_example_names.sh <workdir>
set -u
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
W=${1:?usage: $0 <workdir>}
mkdir -p "$W/bin"
ln -sf "$ROOT/tools/ttcut-demux/ttcut-demux" "$W/bin/ttcut-demux"
cat > "$W/bin/kdialog" <<'STUB'
#!/bin/bash
# --checklist TEXT tag label state ... : print every tag; --yesno: no.
case " $* " in
  *" --checklist "*)
    shift 3                       # --separate-output --checklist TEXT
    while [ $# -ge 3 ] && [ "$1" != "--title" ]; do echo "$1"; shift 3; done ;;
  *" --yesno "*) exit 1 ;;
esac
exit 0
STUB
chmod +x "$W/bin/kdialog"

IN="$W/home/Videos/VDR"
for s in "a:testsrc2:2026-01-01.20.15.1-0" "b:smptebars:2026-01-02.21.30.1-0"; do
  IFS=: read -r series src rec <<<"$s"
  d="$IN/Serie_$series/Folge/$rec.rec"
  mkdir -p "$d"
  ffmpeg -y -v error -f lavfi -i "$src=size=320x240:rate=25" \
      -f lavfi -i "sine=f=440:sample_rate=48000" -t 4 \
      -c:v libx264 -preset ultrafast -c:a ac3 -f mpegts "$d/00001.ts" \
    || { echo "FAIL: could not make the test TS"; exit 1; }
done

HOME="$W/home" PATH="$W/bin:$PATH" bash "$ROOT/tools/vdr-demux-example.sh" \
    < /dev/null > "$W/example.out" 2>&1
OUT="$W/home/Videos/TTCut_Output"
n264=$(find "$OUT" -maxdepth 1 -name '*.264' | wc -l)
nlog=$(find "$OUT" -maxdepth 1 -name '*.log' | wc -l)
echo "outputs: $(find "$OUT" -maxdepth 1 -type f -printf '%f\n' | sort | tr '\n' ' ')"
if [ "$n264" -ne 2 ] || [ "$nlog" -ne 2 ]; then
  echo "FAIL: two recordings named 'Folge' left $n264 video file(s) and $nlog log(s)"
  exit 1
fi
echo "PASS: both recordings named 'Folge' kept their own outputs and logs"
