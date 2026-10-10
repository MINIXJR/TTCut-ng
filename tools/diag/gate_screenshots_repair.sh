#!/bin/bash
# gate_screenshots_repair.sh - the screenshot run takes the pictures of the
# repair dialog.
#
# Runs tools/ttcut-screenshots.sh --repair-only without a window and checks
# that the three pictures of the audio repair dialog come out - one per view:
# mute (C+LFE burst), fade-out (lasting stop), fill (hole) -, that they
# differ from each other, that nothing else is written and that the tracked
# docs/MainWindow.png is left alone.
#
#   usage: gate_screenshots_repair.sh <workdir>
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
W="${1:?usage: gate_screenshots_repair.sh <workdir>}"
OUT="$W/out"
rm -rf "$OUT"; mkdir -p "$OUT"
BEFORE=$(git -C "$ROOT" status --porcelain docs/MainWindow.png)
if ! TTCUT_QPA_PLATFORM=offscreen "$ROOT/tools/ttcut-screenshots.sh" --repair-only "$OUT" > "$W/run.log" 2>&1; then
  echo "FAIL: ttcut-screenshots.sh --repair-only failed"; tail -n 15 "$W/run.log"; exit 1
fi
RC=0
for n in mute fadeout fill; do
  f="$OUT/ttcutng-repair-$n.png"
  if [ -f "$f" ] && [ "$(stat -c %s "$f")" -gt 2000 ]; then echo "PASS: picture of the $n view ($(stat -c %s "$f") bytes)"
  else echo "FAIL: picture of the $n view is missing or empty"; RC=1; fi
done
for pair in "mute fadeout" "mute fill" "fadeout fill"; do
  set -- $pair
  if cmp -s "$OUT/ttcutng-repair-$1.png" "$OUT/ttcutng-repair-$2.png"; then echo "FAIL: the $1 and $2 pictures are the same"; RC=1
  else echo "PASS: the $1 and $2 pictures differ"; fi
done
OTHERS=$(ls "$OUT" | grep -vc '^ttcutng-repair-')
if [ "$OTHERS" -eq 0 ]; then echo "PASS: nothing but the repair pictures is written"; else echo "FAIL: $OTHERS other file(s) written"; RC=1; fi
if [ "$(git -C "$ROOT" status --porcelain docs/MainWindow.png)" = "$BEFORE" ]; then echo "PASS: docs/MainWindow.png is left alone"
else echo "FAIL: the run changed docs/MainWindow.png"; RC=1; fi
exit $RC
