#!/bin/bash
# start-gdb.sh [ttcut-ng arguments...]
# Start the dev build of TTCut-ng under gdb to catch a crash that cannot be
# provoked on demand (TODO.md, "H.265-UHD: Absturz (SIGABRT) unerklärt").
# On a crash gdb prints every thread's backtrace and writes a core file; the
# TTCut-ng log is copied next to it after gdb ends - it is not written at a
# crash and is overwritten by the next start. The build ID is recorded so the
# core can be matched to its binary later (file dates are no proof).
# Output: /usr/local/src/CLAUDE_TMP/TTCut-ng/gdb-runs/<time>/ (temporary -
# move what is worth keeping).
#
# Moved here from /usr/local/src/CLAUDE_TMP/TTCut-ng/uhd/ on 2026-10-18 so it
# is versioned; it was lost once in the CLAUDE_TMP purge of 2026-08-16.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
RUN=/usr/local/src/CLAUDE_TMP/TTCut-ng/gdb-runs/$(date +%Y%m%d-%H%M%S)
mkdir -p "$RUN"
cd "$ROOT" || exit 1   # the dev build loads its translation from here

echo "Output: $RUN"
gdb -q -batch \
  -ex "set pagination off" \
  -ex "handle SIGPIPE nostop noprint pass" \
  -ex "handle SIGUSR1 nostop noprint pass" \
  -ex "handle SIGUSR2 nostop noprint pass" \
  -ex "run" \
  -ex "echo \n=== CRASH: all threads ===\n" \
  -ex "thread apply all bt full" \
  -ex "generate-core-file $RUN/core.ttcut-ng" \
  --args "$ROOT/build/ttcut-ng" "$@" 2>&1 | tee "$RUN/gdb.txt"

cp -p "${XDG_CACHE_HOME:-$HOME/.cache}/ttcut-ng/logfile.log" "$RUN/" 2>/dev/null
readelf -n "$ROOT/build/ttcut-ng" | grep -i 'build id' > "$RUN/build-id.txt"
echo "Done. Output: $RUN"
