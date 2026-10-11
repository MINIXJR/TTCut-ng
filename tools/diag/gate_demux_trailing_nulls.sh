#!/bin/bash
# gate_demux_trailing_nulls.sh - trailing_null_bytes of ttcut-demux: the
# count of null bytes at the end of the MPEG-2 ES, for file names as a
# recording can carry them. The name is data: an apostrophe or a backslash
# in it must not change the count, and nothing in it may be run (until
# 2026-10-11 the path was pasted into the Python source).
#   usage: tools/diag/gate_demux_trailing_nulls.sh
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEMUX="$ROOT/tools/ttcut-demux/ttcut-demux"
WORK="${W:-/usr/local/src/CLAUDE_TMP/TTCut-ng/demux-trailing-nulls-tests}"
rm -rf "$WORK/files"; mkdir -p "$WORK/files"
PASS=0; FAIL=0

# Function definitions end with "}" in column 1; sourcing the whole script
# would run it.
extract_fn() {
    awk -v fn="$1" '$0 == fn"() {" {infn=1} infn {print} infn && /^}/ {exit}' "$DEMUX"
}
FN_FILE="$WORK/.fns.sh"
extract_fn trailing_null_bytes > "$FN_FILE"
grep -q "^trailing_null_bytes() {" "$FN_FILE" || { echo "FAIL: trailing_null_bytes not found in $DEMUX"; exit 1; }
# shellcheck source=/dev/null
source "$FN_FILE"

cd "$WORK/files" || exit 1
MARKER="marker_from_file_name"

check() {   # NAME ZEROS LABEL - a file "DATA" + ZEROS null bytes under NAME
    local got
    printf 'DATA' > "$1"; head -c "$2" /dev/zero >> "$1"
    got=$(trailing_null_bytes "$WORK/files/$1")
    if [ "$got" = "$2" ]; then echo "PASS: $3 -> $got"; PASS=$((PASS+1))
    else echo "FAIL: $3 -> $got, want $2"; FAIL=$((FAIL+1)); fi
}
check "plain.m2v"                 100 "plain name"
check "plain_none.m2v"            0   "no null bytes at the end"
check "Grey's_Anatomy.m2v"        100 "apostrophe in the name"
check 'say_"hello".m2v'           100 "double quote in the name"
check 'back\slash\x41\n.m2v'      100 "backslashes in the name"
# shellcheck disable=SC2016  # the name is meant literally
check 'dollar_$(touch x)_`id`.m2v' 100 "shell metacharacters in the name"
check "with space.m2v"            100 "space in the name"
check "A' if __import__('os').system('touch $MARKER') else 'A.m2v" 100 "a name that is a Python expression"

if [ -e "$MARKER" ] || [ -e x ]; then echo "FAIL: a file name was run as code (marker file exists)"; FAIL=$((FAIL+1))
else echo "PASS: no file name was run as code"; PASS=$((PASS+1)); fi

: > empty.m2v
got=$(trailing_null_bytes "$WORK/files/empty.m2v")
if [ "$got" = "0" ]; then echo "PASS: empty file -> 0"; PASS=$((PASS+1)); else echo "FAIL: empty file -> $got, want 0"; FAIL=$((FAIL+1)); fi
got=$(trailing_null_bytes "$WORK/files/does-not-exist.m2v")
if [ "$got" = "0" ]; then echo "PASS: missing file -> 0"; PASS=$((PASS+1)); else echo "FAIL: missing file -> $got, want 0"; FAIL=$((FAIL+1)); fi

echo "---- $PASS PASS, $FAIL FAIL"
[ "$FAIL" -eq 0 ]
