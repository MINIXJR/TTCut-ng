#!/bin/bash
# acm-cut.sh <project.ttcut> <out.mkv>
# Headless --auto-cut with a finalization watcher: the app does not reliably
# self-exit after a correct cut (MPEG-2 completion slot), so we wait until the
# output MKV is finalized (size stable + valid ffprobe duration) and then kill
# the hung process. If the app exits on its own (H.264 path), we detect that too.
#
# --- promotion note (2026-08-10, cut-abort plan, Task 11) -------------------
# Moved here from /usr/local/src/CLAUDE_TMP/TTCut-ng/ (a temp directory) so it
# is versioned alongside the other gate scripts. Two changes only:
#   * the binary is now $ROOT/build/ttcut-ng (CMake output path) instead of
#     ./ttcut-ng (the pre-CMake qmake path, which no longer exists), and it can
#     be overridden with a third argument;
#   * the repository root is derived from this script's location instead of
#     being hard-coded.
# The self-exit hang the header describes was fixed on 2026-08-02 (a missing
# `emit cutFinished()`); the watcher is kept because it still guards against
# an old binary and because a wall-clock `timeout` would fake a result.
#
# Usage: acm-cut.sh <project.ttcut> <out.mkv> [ttcut-ng-binary]
set -u
if [ $# -lt 2 ]; then
  echo "usage: $(basename "$0") <project.ttcut> <out.mkv> [ttcut-ng-binary]" >&2
  exit 2
fi
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
PRJ=$(readlink -f "$1"); OUT=$(readlink -f "$2")
BIN=$(readlink -f "${3:-$ROOT/build/ttcut-ng}")
[ -x "$BIN" ] || { echo "no such binary: $BIN (build it with: cmake --build build)" >&2; exit 2; }
[ -f "$PRJ" ] || { echo "no such project: $PRJ" >&2; exit 2; }
cd "$ROOT" || exit 2
rm -f "$OUT"
QT_QPA_PLATFORM=offscreen "$BIN" --project "$PRJ" --auto-cut "$OUT" >/dev/null 2>&1 &
PID=$!
echo "started pid=$PID -> $OUT"
last=-1; stable=0
for i in $(seq 1 240); do          # up to 240*15s = 60 min hard cap
  sleep 15
  if ! kill -0 "$PID" 2>/dev/null; then echo "app self-exited after ~$((i*15))s"; break; fi
  sz=$(stat -c %s "$OUT" 2>/dev/null || echo 0)
  if [ "$sz" -gt 0 ] && [ "$sz" -eq "$last" ]; then stable=$((stable+1)); else stable=0; fi
  last=$sz
  if [ "$stable" -ge 3 ]; then     # size unchanged for ~45s
    d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$OUT" 2>/dev/null)
    # require a NUMERIC duration — ffprobe prints the literal "N/A" for an
    # unfinalized file, which must NOT count as finalized (else we kill mid-mux).
    if [[ "$d" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
      echo "finalized: size=$sz duration=${d}s after ~$((i*15))s — killing hung process"
      kill "$PID" 2>/dev/null; sleep 1; kill -9 "$PID" 2>/dev/null
      break
    fi
  fi
done
kill -9 "$PID" 2>/dev/null
# Emit per-track payload md5 (the sound content oracle)
n=$(ffprobe -v error -select_streams a -show_entries stream=index -of csv=p=0 "$OUT" 2>/dev/null | wc -l)
echo "--- audio payload md5 ($n tracks) ---"
for s in $(seq 0 $((n-1))); do
  echo "a$s $(ffmpeg -v error -i "$OUT" -map "a:$s" -c copy -f md5 - 2>/dev/null)"
done
