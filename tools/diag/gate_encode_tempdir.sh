#!/bin/bash
# Gate: parallel runs must not share encodePart()'s temporary files.
#
# Four instances of test_mpeg2cut_abort at once, three rounds, one shared
# TMPDIR. Before the fix this produced 3-5 failed runs out of 12 and SIGABRT
# dumps from firstSequenceHeader(); after it, every run has to pass.
#
# XDG_CONFIG_HOME is per instance on purpose: it isolates the settings file so
# the only thing left shared is the temp directory. TMPDIR alone would not do
# it - a stored TempDirPath in TTCut-ng.conf wins over the environment.
#
# Usage: tools/diag/gate_encode_tempdir.sh [rounds]
set -u

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BIN="$ROOT/tools/diag/test_mpeg2cut_abort"
CACHE="$ROOT/tools/test-videos/cache"
OUT=/usr/local/src/CLAUDE_TMP/TTCut-ng/gate-encode-tempdir
ROUNDS=${1:-3}

[ -x "$BIN" ] || { echo "missing $BIN - build with: cmake --build build --target diag-abort"; exit 2; }

rm -rf "$OUT"; mkdir -p "$OUT/sharedtmp"

for r in $(seq 1 "$ROUNDS"); do
  for j in 1 2 3 4; do
    mkdir -p "$OUT/cfg$j"
    (
      cd "$OUT" || exit 1
      XDG_CONFIG_HOME="$OUT/cfg$j" TMPDIR="$OUT/sharedtmp" \
        "$BIN" "$CACHE/tux_mpeg2_576i_pal_test.m2v" \
               "$CACHE/tux_mpeg2_576i_pal_test.mp2" \
               "$OUT/w${r}_$j" mplexabort > "$OUT/r${r}_$j.log" 2>&1
      echo "$r/$j rc=$?" >> "$OUT/results.txt"
    ) &
  done
  wait
done

runs=$(wc -l < "$OUT/results.txt")
fails=$(grep -vc 'rc=0' "$OUT/results.txt")
cores=$(ls "$OUT"/core* 2>/dev/null | wc -l)

echo "runs=$runs failed=$fails dumps=$cores"
grep -v 'rc=0' "$OUT/results.txt"
grep -h '^FAIL' "$OUT"/*.log 2>/dev/null | sort | uniq -c
grep -l 'ASSERT failure' "$OUT"/*.log 2>/dev/null

[ "$fails" -eq 0 ] && [ "$cores" -eq 0 ] && { echo "GATE PASS"; exit 0; }
echo "GATE FAIL"; exit 1
