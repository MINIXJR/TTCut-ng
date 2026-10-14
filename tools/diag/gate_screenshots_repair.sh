#!/bin/bash
# gate_screenshots_repair.sh - the screenshot run takes the pictures of the
# repair dialog.
#
# Runs tools/ttcut-screenshots.sh --repair-only without a window and checks
# that the three pictures of the audio repair dialog come out - one per view:
# mute (C+LFE burst), fade-out (lasting stop), fill (hole) -, that they
# differ from each other, that nothing else is written and that the tracked
# docs/MainWindow.png is left alone. Then the usual set, which until
# 2026-10-14 copied its main-window picture over docs/MainWindow.png on every
# run (offscreen: a light Fusion picture into the repository); it must leave
# the file alone as well. The file's bytes are kept aside and put back if a
# run changes them, so the gate never leaves the working tree different.
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

DOCPNG="$ROOT/docs/MainWindow.png"
cp -p "$DOCPNG" "$W/MainWindow.before.png"
OUT2="$W/out-usual"
rm -rf "$OUT2"; mkdir -p "$OUT2"
if ! TTCUT_QPA_PLATFORM=offscreen "$ROOT/tools/ttcut-screenshots.sh" "$OUT2" > "$W/run-usual.log" 2>&1; then
  echo "FAIL: ttcut-screenshots.sh (usual set) failed"; tail -n 15 "$W/run-usual.log"; RC=1
elif [ -f "$OUT2/ttcutng-main.png" ]; then echo "PASS: the usual set took the main-window picture"
else echo "FAIL: the usual set wrote no ttcutng-main.png"; RC=1; fi
if cmp -s "$DOCPNG" "$W/MainWindow.before.png"; then echo "PASS: the usual set leaves docs/MainWindow.png alone"
else echo "FAIL: the usual set changed docs/MainWindow.png (put back)"; cp -p "$W/MainWindow.before.png" "$DOCPNG"; RC=1; fi
exit $RC
