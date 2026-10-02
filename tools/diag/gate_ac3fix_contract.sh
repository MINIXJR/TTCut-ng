#!/bin/bash
# gate_ac3fix_contract.sh - what ttcut-ac3fix writes and reports, and when
# ttcut-demux replaces a track with the repaired copy (audit run 19, map
# ttcut-ac3fix.md). Self-verdicting; needs ffmpeg and python3 only.
#
# Tool (T): the output is the input with nothing but acmod bits changed;
# bytes that belong to no frame are kept and reported; a track that is not
# 48 kHz AC3 yields no frame; argument errors.
# Demux (D): repair_ac3_track is taken out of the script and called on single
# files. A track is replaced only when the repaired copy decodes with fewer
# errors than the original; every failure leaves the original untouched and
# does not end the calling shell.
#
#   usage: gate_ac3fix_contract.sh <ttcut-ac3fix> <ttcut-demux> <workdir>
# check() evaluates its condition string: the single quotes are meant
# (SC2016), and rc is read inside those strings (SC2034).
# shellcheck disable=SC2016,SC2034
set -u
BIN=${1:?usage: $0 <ttcut-ac3fix> <ttcut-demux> <workdir>}
DEMUX=${2:?usage: $0 <ttcut-ac3fix> <ttcut-demux> <workdir>}
W=${3:?usage: $0 <ttcut-ac3fix> <ttcut-demux> <workdir>}
BIN=$(readlink -f "$BIN"); DEMUX=$(readlink -f "$DEMUX")
mkdir -p "$W" && cd "$W" || exit 1
PASS=0; FAIL=0
ok()  { echo "PASS: $1"; PASS=$((PASS+1)); }
bad() { echo "FAIL: $1"; FAIL=$((FAIL+1)); }
check() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }   # LABEL CONDITION

# ---- fixtures ----------------------------------------------------------------
# Pink noise, so that every frame differs. 448 kbit/s at 48 kHz = 1792 bytes
# per frame, 250 frames in 8 s.
noise() { ffmpeg -y -v error -f lavfi -i "anoisesrc=d=8:c=pink:r=$1:a=0.3" "${@:2}"; }
if ! { noise 48000 -ac 2 -c:a ac3 -b:a 448k stereo.ac3 &&
        noise 48000 -af "pan=5.1|FL=c0|FR=c0|FC=c0|LFE=c0|BL=c0|BR=c0" -c:a ac3 -b:a 448k s51.ac3 &&
        noise 44100 -ac 2 -c:a ac3 -b:a 448k stereo441.ac3 &&
        noise 48000 -ac 2 -c:a eac3 -b:a 448k -f eac3 eac3.ac3; }; then
  echo "FAIL: fixture generation (ffmpeg)"; exit 1
fi
: > empty.ac3
if ! python3 - <<'PY'; then echo "FAIL: fixture generation (python)"; exit 1; fi
import random
FS = 1792
def acmod(data, value, first=0):
    b = bytearray(data)
    for i in range(first, len(b) // FS):
        b[i*FS+6] = (b[i*FS+6] & 0x1F) | (value << 5)
    return bytes(b)
s51 = open('s51.ac3', 'rb').read()
stereo = open('stereo.ac3', 'rb').read()
assert len(s51) == len(stereo) == 250 * FS and s51[FS:FS+2] == b'\x0b\x77'
# 5.1 frames whose header says stereo: the case the repair is made for.
open('wrong_all.ac3', 'wb').write(acmod(s51, 2))
open('wrong_late.ac3', 'wb').write(acmod(s51, 2, first=100))
# 3000 foreign bytes after frame 100, a sync word with a parsable header in
# the middle of them.
random.seed(7)
junk = bytearray(random.randrange(256) for _ in range(3000))
junk[1500:1507] = stereo[:7]
open('junk_mid.ac3', 'wb').write(stereo[:100*FS] + bytes(junk) + stereo[100*FS:])
# Valid stereo with 63 frames of destroyed payload: decodes with errors, and
# a repair makes it worse.
random.seed(3)
b = bytearray(stereo)
for i in range(63):
    b[i*FS+7:(i+1)*FS] = bytes(random.randrange(256) for _ in range(FS-7))
open('stereo_damaged.ac3', 'wb').write(b)
PY

num() { grep -oP "$1:\\s+\\K[0-9]+" "$2" | head -1; }

# ---- tool --------------------------------------------------------------------
"$BIN" -F -f wrong_all.ac3 t1.ac3 >t1.out 2>t1.err
check "T1 repaired 5.1 equals the original 5.1" 'cmp -s t1.ac3 s51.ac3'

"$BIN" -f junk_mid.ac3 t2.ac3 >t2.out 2>t2.err
check "T2 copy keeps every byte, foreign bytes included" 'cmp -s t2.ac3 junk_mid.ac3'

"$BIN" -a junk_mid.ac3 >t3.out 2>t3.err
check "T3 foreign bytes in mid-file are reported" 'grep -q "3000 bytes could not be parsed" t3.err'
check "T4 250 frames counted around the foreign bytes" '[ "$(num "Total frames" t3.out)" = 250 ]'

"$BIN" -F -f junk_mid.ac3 t5.ac3 >t5.out 2>t5.err
check "T5 fix run keeps the file size" '[ "$(stat -c %s t5.ac3)" = "$(stat -c %s junk_mid.ac3)" ]'

"$BIN" -a stereo441.ac3 >t6.out 2>t6.err
check "T6 44.1 kHz: no frame, nothing inconsistent" \
      '[ "$(num "Total frames" t6.out)" = 0 ] && [ "$(num "Inconsistent frames" t6.out)" = 0 ]'
check "T7 44.1 kHz: says that no 48 kHz AC3 frame was found" 'grep -q "no 48 kHz AC3 frame" t6.err'

"$BIN" -a eac3.ac3 >t8.out 2>t8.err
check "T8 E-AC3: no frame" '[ "$(num "Total frames" t8.out)" = 0 ]'

cp stereo.ac3 same.ac3
"$BIN" -F -f same.ac3 same.ac3 >t9.out 2>t9.err; rc=$?
check "T9 input as output is refused, file untouched" '[ $rc -eq 1 ] && cmp -s same.ac3 stereo.ac3'

"$BIN" -F stereo.ac3 >t10.out 2>t10.err; rc=$?
check "T10 --force-fix without output: error, no -o in the text" \
      '[ $rc -eq 1 ] && ! grep -q -- "-o" t10.err && ! grep -q "analyze mode" t10.out'

"$BIN" -a stereo.ac3 x.ac3 y.ac3 >t11.out 2>t11.err; rc=$?
check "T11 a third file argument is an error" '[ $rc -eq 1 ]'

# ---- demux -------------------------------------------------------------------
# Function definitions end with "}" in column 1; sourcing the whole script
# would run it.
extract_fn() {
    awk -v fn="$1" '$0 == fn"() {" {infn=1} infn {print} infn && /^}/ {exit}' "$DEMUX"
}
{ echo 'info() { echo "[INFO] $1"; }'; echo 'warn() { echo "[WARN] $1"; }'
  extract_fn ac3_decode_errors
  extract_fn repair_ac3_track; } > fns.sh
grep -q "^repair_ac3_track() {" fns.sh || { echo "FAIL: repair_ac3_track not found in $DEMUX"; exit 1; }

mkdir -p bin stub
ln -sf "$BIN" bin/ttcut-ac3fix
# A repair that fails after it has written something.
cat > stub/ttcut-ac3fix <<STUB
#!/bin/bash
case " \$* " in *" -F "*) echo garbage > "\${@: -1}"; exit 1 ;; esac
exec "$BIN" "\$@"
STUB
chmod +x stub/ttcut-ac3fix

# repair <path-dir> <fixture>: run the function on a copy under "set -e" as
# the script does; prints FIXED=<n> when the shell survives.
repair() {
    cp "$2" "d_$2"
    PATH="$W/$1:$PATH" bash -c 'set -e; source ./fns.sh; repair_ac3_track "$1"; echo "FIXED=$AC3_FIXED_FRAMES"' _ "d_$2" > "d_$2.log" 2>&1
}
fixed() { grep -oP '^FIXED=\K[0-9]+' "d_$1.log"; }

repair bin stereo.ac3
check "D1 valid stereo at 448 kbit/s stays as it is" 'cmp -s d_stereo.ac3 stereo.ac3 && [ "$(fixed stereo.ac3)" = 0 ]'

repair bin wrong_all.ac3
check "D2 5.1 with stereo headers is repaired" 'cmp -s d_wrong_all.ac3 s51.ac3 && [ "$(fixed wrong_all.ac3)" = 250 ]'

repair bin wrong_late.ac3
check "D3 wrong headers that start after 3 s are repaired" 'cmp -s d_wrong_late.ac3 s51.ac3 && [ "$(fixed wrong_late.ac3)" = 150 ]'

repair bin stereo_damaged.ac3
check "D4 damaged valid stereo is not replaced by a worse repair" \
      'cmp -s d_stereo_damaged.ac3 stereo_damaged.ac3 && [ "$(fixed stereo_damaged.ac3)" = 0 ]'

repair bin empty.ac3
check "D5 a failing analyze run does not end the shell" '[ "$(fixed empty.ac3)" = 0 ] && grep -q "WARN" d_empty.ac3.log'

repair stub wrong_all.ac3
check "D6 a failing repair leaves the original" \
      'cmp -s d_wrong_all.ac3 wrong_all.ac3 && [ "$(fixed wrong_all.ac3)" = 0 ] && [ ! -e d_wrong_all.ac3.fixed ]'

echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
