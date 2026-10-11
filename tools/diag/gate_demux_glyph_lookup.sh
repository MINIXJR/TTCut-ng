#!/bin/bash
# gate_demux_glyph_lookup.sh - locate_glyph_helper of ttcut-demux: where the
# OCR glyph repair takes its helper and its templates from. Until 2026-10-11
# a script without the helper next to it fell back to a fixed path of the
# author's source tree; it asks PATH now.
#   usage: tools/diag/gate_demux_glyph_lookup.sh
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEMUX="$ROOT/tools/ttcut-demux/ttcut-demux"
WORK="${W:-/usr/local/src/CLAUDE_TMP/TTCut-ng/demux-glyph-lookup-tests}"
rm -rf "$WORK/layouts"; mkdir -p "$WORK/layouts"
PASS=0; FAIL=0

# Function definitions end with "}" in column 1; sourcing the whole script
# would run it.
extract_fn() {
    awk -v fn="$1" '$0 == fn"() {" {infn=1} infn {print} infn && /^}/ {exit}' "$DEMUX"
}
FN_FILE="$WORK/.fns.sh"
extract_fn locate_glyph_helper > "$FN_FILE"
grep -q "^locate_glyph_helper() {" "$FN_FILE" || { echo "FAIL: locate_glyph_helper not found in $DEMUX"; exit 1; }
# shellcheck source=/dev/null
source "$FN_FILE"

ok()  { echo "PASS: $1"; PASS=$((PASS+1)); }
bad() { echo "FAIL: $1"; FAIL=$((FAIL+1)); }
expect() {   # LABEL GOT WANT
    if [ "$2" = "$3" ]; then ok "$1 -> $2"; else bad "$1 -> '$2', want '$3'"; fi
}

L="$WORK/layouts"
mkdir -p "$L/tree/ocr-glyphs" "$L/alone" "$L/bin"
printf '#!/bin/sh\n' > "$L/tree/ttcut-ocr-glyphs"; chmod +x "$L/tree/ttcut-ocr-glyphs"
printf '#!/bin/sh\n' > "$L/bin/ttcut-ocr-glyphs";  chmod +x "$L/bin/ttcut-ocr-glyphs"

# 1. Source tree: helper and templates next to the script win over PATH.
PATH="$L/bin:$PATH" locate_glyph_helper "$L/tree"
expect "helper next to the script"    "$GLYPH_HELPER" "$L/tree/ttcut-ocr-glyphs"
expect "templates next to the script" "$GLYPH_DIR"    "$L/tree/ocr-glyphs"

# 2. The script alone, a helper in PATH (the installed layout).
PATH="$L/bin:$PATH" locate_glyph_helper "$L/alone"
expect "script alone, helper from PATH" "$GLYPH_HELPER" "$L/bin/ttcut-ocr-glyphs"
if [ -d /usr/share/ttcut-ng/ocr-glyphs ]; then
    expect "script alone, installed templates" "$GLYPH_DIR" "/usr/share/ttcut-ng/ocr-glyphs"
else
    expect "script alone, no templates anywhere" "$GLYPH_DIR" "$L/alone/ocr-glyphs"
fi

# 3. The script alone and no helper in PATH: nothing, and no error under set -e.
got=$(set -e; PATH="$L/alone" locate_glyph_helper "$L/alone"; echo "helper=[$GLYPH_HELPER]")
expect "script alone, no helper anywhere" "$got" "helper=[]"

# 4. No path of a particular machine in the script.
if grep -n '/usr/local/src' "$DEMUX"; then bad "ttcut-demux names a path under /usr/local/src"
else ok "ttcut-demux names no path under /usr/local/src"; fi

echo "---- $PASS PASS, $FAIL FAIL"
[ "$FAIL" -eq 0 ]
