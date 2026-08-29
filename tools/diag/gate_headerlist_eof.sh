#!/usr/bin/env bash
# Gate: MPEG-2 header list against a stream that ends mid-data.
#
# Prepares the material the defect needs and runs test_headerlist_eof under a
# timeout. Two verdicts:
#
#   TERMINATES  createHeaderList() returns at all. Before the fix it did not:
#               readByte(quint8*,int) reset readPos to writePos in its EOF
#               catch, which clears what atEnd() tests, so the parser re-read
#               the same trailing byte forever.
#   REAL        every header in the list sits on a real 00 00 01 <type> start
#               code in the file (no phantom built from uninitialised data).
#
# The trigger is a file whose last byte doubles as a start-code type. 0xB8
# (group_start_code) is what the reported recording ended on, so the gate cuts
# the source at the first 0xB8 past a 4 MB offset. A file that ends on a clean
# sequence_end_code - TEST.m2v does - never reaches the defect, which is why
# the existing cut matrix passed throughout.
#
# Usage: gate_headerlist_eof.sh <test_headerlist_eof-bin> <source.m2v> [timeout-s]
set -euo pipefail
BIN="${1:?usage: gate_headerlist_eof.sh <bin> <source.m2v> [timeout-s]}"
SRC="${2:?}"
TMO="${3:-60}"

WORK=/usr/local/src/CLAUDE_TMP/TTCut-ng/headerlist_eof; mkdir -p "$WORK"
CUT="$WORK/ends_on_b8.m2v"

# First 0xB8 at or past 4 MB; xxd -p -c1 gives one hex byte per line, so the
# grep line number is the 1-based offset inside the searched window. Dumped to
# a file first: closing the pipe early makes xxd take SIGPIPE, which under
# `set -o pipefail` would abort the gate before it runs anything.
head -c 4200000 "$SRC" | tail -c 200000 | xxd -p -c1 > "$WORK/window.hex"
OFF=$(grep -n -m1 '^b8$' "$WORK/window.hex" | cut -d: -f1)
[ -n "$OFF" ] || { echo "VERDICT: FAIL (no 0xB8 in the search window of $SRC)"; exit 1; }
SZ=$((4000000 + OFF))
head -c "$SZ" "$SRC" > "$CUT"

LAST=$(tail -c 1 "$CUT" | xxd -p)
[ "$LAST" = "b8" ] || { echo "VERDICT: FAIL (prepared file ends on 0x$LAST, not 0xb8)"; exit 1; }
echo "prepared: $CUT ($SZ bytes, last byte 0x$LAST)"

set +e
timeout "$TMO" "$BIN" "$CUT" >"$WORK/out.txt" 2>"$WORK/err.txt"
RC=$?
set -e

if [ $RC -eq 124 ]; then
  echo "TERMINATES: FAIL (still running after ${TMO}s, $(wc -l < "$WORK/err.txt") stderr lines)"
  echo "VERDICT: FAIL"
  exit 1
fi
echo "TERMINATES: PASS"

cat "$WORK/out.txt"
if [ $RC -ne 0 ]; then
  echo "REAL: FAIL (rc=$RC)"; sed -n '1,5p' "$WORK/err.txt"
  echo "VERDICT: FAIL"
  exit 1
fi
echo "REAL: PASS"
echo "VERDICT: PASS"
