#!/bin/bash
# gate_demux_slot_e2e.sh - the slot rule of ttcut-demux end to end, on
# generated recordings (no material needed): encode a TS with noise audio
# (unique frames), demux it, and let av_track_audit.py - an independent walk
# over the original's packets - say where every audio frame sits against the
# picture. Checked per case: demux exit 0, every MP2/AC3 track carries
# audio_N_start_offset_ms, every reading within one audio frame, and the
# written start offset is the one the walk finds.
#   A  MPEG-2 + MP2, audio leading the picture by 0.31 s
#   B  H.264 + AC3 leading by 0.31 s + MP2 starting 0.1 s AFTER the picture
#   C  case B with 1.5 MB cut out of the middle of the TS (picture hole)
#   D  a ttcut-audiofix without the -p mode in PATH: the run must stop
#   E  case A with mawk as awk under a comma-decimal locale (de_DE): mawk
#      prints and reads numbers with the locale's decimal point
#   usage: tools/diag/gate_demux_slot_e2e.sh
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEMUX="$ROOT/tools/ttcut-demux/ttcut-demux"
AUDIT="$ROOT/tools/diag/av_track_audit.py"
# A subdirectory of its own: under run-gates.sh $W also holds the runner's
# HOME and XDG directories, which must survive the cleanup below.
WORK="${W:-/usr/local/src/CLAUDE_TMP/TTCut-ng/demux-slot-e2e}/e2e"
rm -rf "$WORK"; mkdir -p "$WORK"; cd "$WORK" || exit 1
export XDG_CACHE_HOME="$WORK/xdg"
REPO_PATH="$ROOT/tools/ttcut-audiofix:$ROOT/tools/ttcut-ac3fix:$PATH"
PASS=0; FAIL=0
ok()  { echo "PASS: $1"; PASS=$((PASS+1)); }
bad() { echo "FAIL: $1"; FAIL=$((FAIL+1)); }
[ -x "$ROOT/tools/ttcut-audiofix/ttcut-audiofix" ] || { echo "FAIL: ttcut-audiofix not built (cmake --build build --target ttcut-audiofix)"; exit 1; }

NOISE="anoisesrc=r=48000:c=pink:a=0.3"
ffmpeg -v error -y -f lavfi -i "testsrc2=r=25:s=352x288" -itsoffset -0.31 -f lavfi -i "$NOISE:seed=1" \
    -map 0:v -map 1:a -c:v mpeg2video -b:v 2M -g 12 -bf 2 -c:a mp2 -b:a 192k -ac 2 -t 20 -f mpegts a.ts \
    || { echo "FAIL: ffmpeg could not build case A"; exit 1; }
ffmpeg -v error -y -f lavfi -i "testsrc2=r=50:s=320x180" -itsoffset -0.31 -f lavfi -i "$NOISE:seed=2" \
    -itsoffset 0.1 -f lavfi -i "$NOISE:seed=3" \
    -map 0:v -map 1:a -map 2:a -c:v libx264 -preset ultrafast -g 50 -bf 2 -b:v 2M \
    -c:a:0 ac3 -b:a:0 192k -c:a:1 mp2 -b:a:1 192k -ac 2 -t 20 -f mpegts b.ts \
    || { echo "FAIL: ffmpeg could not build case B"; exit 1; }
# C: drop 1.5 MB (whole TS packets) from the middle of B
SIZE=$(stat -c %s b.ts); CUT_AT=$(( SIZE / 2 / 188 * 188 )); CUT_LEN=$(( 1500000 / 188 * 188 ))
{ head -c "$CUT_AT" b.ts; tail -c +$(( CUT_AT + CUT_LEN + 1 )) b.ts; } > c.ts

run_case() {   # NAME TS FPS [environment assignments for the demux run]
    local name=$1 ts=$2 fps=$3 rc=0
    shift 3
    env PATH="$REPO_PATH" "$@" "$DEMUX" -e --no-subs -n t "$ts" "out-$name/" > "demux-$name.log" 2>&1 || rc=$?
    [ "$rc" = 0 ] && ok "$name: demux exit 0" || { bad "$name: demux exit $rc: $(tail -1 "demux-$name.log")"; return; }
    local tracks offsets
    tracks=$(grep -c '^audio_[0-9]*_file=' "out-$name/t.info")
    offsets=$(grep -c '^audio_[0-9]*_start_offset_ms=' "out-$name/t.info")
    [ "$tracks" -gt 0 ] && [ "$tracks" = "$offsets" ] && ok "$name: start offset written for all $tracks track(s)" \
        || bad "$name: $offsets start offsets for $tracks tracks: $(grep -i 'offset\|placement' "demux-$name.log" | head -3)"
    if python3 "$AUDIT" "$fps" "out-$name" t "$ts" --check-offset > "audit-$name.txt" 2>&1; then
        ok "$name: $(grep -c 'ok\]' "audit-$name.txt") track(s) within one frame, written offset = independent walk"
    else
        bad "$name: audit: $(grep -v '^   course' "audit-$name.txt" | tr '\n' ' ')"
    fi
}
run_case A a.ts 25
run_case B b.ts 50
grep -q '^audio_1_silence_ms=' out-B/t.info 2>/dev/null && [ "$(grep -c 'head -0 ' demux-B.log)" -ge 1 ] \
    && ok "B: the late MP2 track starts with silence, nothing cut from its head" || bad "B: late track: $(grep 't_.*mp2:' demux-B.log)"
run_case C c.ts 50
grep -q '^es_missing_ranges=' out-C/t.info 2>/dev/null && ok "C: picture hole reported ($(grep '^es_missing_ranges=' out-C/t.info))" \
    || bad "C: no es_missing_ranges in the .info"
grep -q 'in picture holes -[1-9]' demux-C.log && ok "C: audio of the hole dropped ($(grep -o 'in picture holes -[0-9]* ([0-9]* ms)' demux-C.log | head -1))" \
    || bad "C: no audio dropped: $(grep 't_.*:' demux-C.log | head -2)"

# E: mawk + de_DE. Skipped with a note where either is missing.
DE_LOCALE=$(locale -a 2>/dev/null | grep -i -m1 '^de_DE\.utf-\?8$' || true)
if command -v mawk > /dev/null && [ -n "$DE_LOCALE" ]; then
    mkdir -p mawkbin; ln -sf "$(command -v mawk)" mawkbin/awk
    # LC_ALL, the strongest of the locale variables: it overrides LC_NUMERIC
    run_case E a.ts 25 PATH="$WORK/mawkbin:$REPO_PATH" LC_ALL="$DE_LOCALE"
    cmp -s out-A/t_und.mp2 out-E/t_und.mp2 && ok "E: mawk under $DE_LOCALE writes the same audio ES as case A" \
        || bad "E: audio ES differs from case A ($(stat -c %s out-A/t_und.mp2 2>/dev/null) vs $(stat -c %s out-E/t_und.mp2 2>/dev/null) bytes)"
else
    echo "note: case E not run (mawk: $(command -v mawk || echo missing), de_DE locale: ${DE_LOCALE:-missing})"
fi

# D: an old ttcut-audiofix (usage without -p) must stop the run before any work
mkdir -p oldbin
cat > oldbin/ttcut-audiofix <<'EOF'
#!/bin/bash
echo "Usage: $0 -a <file>          Analyze (report only)" >&2
echo "       $0 -f <in> <out>      Fix (write sanitized copy)" >&2
exit 2
EOF
chmod +x oldbin/ttcut-audiofix
rc=0; PATH="$WORK/oldbin:$PATH" "$DEMUX" -e --no-subs -n t a.ts out-D/ > demux-D.log 2>&1 || rc=$?
[ "$rc" != 0 ] && grep -q 'ttcut-audiofix' demux-D.log && [ ! -e out-D/t.info ] \
    && ok "D: run stops without the -p mode ($(grep -o 'ttcut-audiofix.*' demux-D.log | head -1 | cut -c1-70))" \
    || bad "D: exit $rc, .info $( [ -e out-D/t.info ] && echo written || echo absent )"

echo "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
