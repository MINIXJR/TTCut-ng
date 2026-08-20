#!/bin/bash
# gate_audiofix.sh — Abnahme-Gates fuer ttcut-audiofix (Spec 2026-08-16).
# Jeder Task erweitert dieses Script; ein Check, der sein Feature noch nicht
# vorfindet, meldet FAIL. Aufruf: tools/diag/gate_audiofix.sh
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TOOL="${AUDIOFIX_TOOL:-$ROOT/tools/ttcut-audiofix/ttcut-audiofix}"
WORK=/usr/local/src/CLAUDE_TMP/TTCut-ng/audiofix-tests

# --full: also run Check 20 (real-recording end-to-end, Task 10). Off by
# default because the source demux alone processes a multi-GB TS and the
# full mplex cut runs on ~80 minutes of MPEG-2 -- keeps the normal gate fast.
RUN_FULL=0
for arg in "$@"; do
  [ "$arg" = "--full" ] && RUN_FULL=1
done
CACHE="$ROOT/tools/test-videos/cache"
mkdir -p "$WORK"
PASS=0; FAIL=0
ok()   { echo "PASS: $1"; PASS=$((PASS+1)); }
bad()  { echo "FAIL: $1"; FAIL=$((FAIL+1)); }

[ -x "$TOOL" ] || { echo "FAIL: binary fehlt ($TOOL) — cmake --build build --target ttcut-audiofix"; exit 1; }

MP2="$CACHE/tux_mpeg2_576i_pal_test.mp2"

# --- Check 1: clean MP2 -> exit 0, total_frames == ffprobe-Paketzahl -------
OUT=$("$TOOL" -a "$MP2"); RC=$?
REF=$(ffprobe -v error -select_streams a:0 -count_packets \
      -show_entries stream=nb_read_packets -of csv=p=0 "$MP2" | tr -d ',')
TF=$(echo "$OUT" | grep -oP '^total_frames=\K[0-9]+')
if [ "$RC" -eq 0 ] && [ "$TF" = "$REF" ]; then ok "clean MP2 analyze (frames=$TF)"
else bad "clean MP2 analyze (rc=$RC frames=$TF ref=$REF)"; fi

# --- Check 2: Muell ZWISCHEN zwei Rahmen -> exit 1, junk_region gemeldet ---
# Rahmengrenze: MP2 48kHz 192kbps -> 576 Bytes/Rahmen (Header pruefen!).
# Frame-Groesse aus dem Report von Check 1 ableiten waere zirkulaer; wir
# nehmen die Groesse aus dem MP2-Header (ffprobe zeigt bit_rate).
FRSZ=576
JUNK="$WORK/mp2_junk_between.mp2"
head -c $((FRSZ*100)) "$MP2" > "$JUNK"
head -c 468 /dev/urandom >> "$JUNK"
tail -c +$((FRSZ*100+1)) "$MP2" >> "$JUNK"
OUT=$("$TOOL" -a "$JUNK"); RC=$?
JR=$(echo "$OUT" | grep -oP '^junk_regions=\K.*')
if [ "$RC" -eq 1 ] && [[ "$JR" == 100@*:468* ]]; then ok "MP2 junk-between analyze ($JR)"
else bad "MP2 junk-between analyze (rc=$RC junk=$JR)"; fi

# --- Check 2b: Datei mit WENIGER als BASELINE_FRAMES(=3) gueltigen Rahmen
# -> total_ms muss trotzdem korrekt aus dem ERSTEN Rahmen stammen, nicht erst
# ab der 3-Rahmen-Plausibilitaets-Baseline (Regressionstest fuer eine
# ms=0-Falle: samplerate fuer current_ms() darf nicht an "have_baseline"
# haengen). 1 Rahmen @ 48kHz/1152 samples = 24ms.
# Hinweis: "Muell direkt nach Rahmen 1" (die urspruenglich geplante
# Konstruktion) ist ueber die volle CLI-Pipeline NICHT erreichbar -
# detect_codec() verlangt selbst eine Kette von RESYNC_CONFIRM(=3) gueltigen
# Rahmen, um den Codec/Startoffset ueberhaupt zu bestaetigen; Muell direkt
# nach Rahmen 1 zerstoert exakt diese Kette, wodurch Rahmen 0 mit in den
# Anfangs-Muellblock faellt (empirisch geprueft: Ergebnis war "0@0:1044",
# nicht "1@24:468"). Diese Variante prueft denselben Zeit-Bug (Case 2 aus
# dem Review-Befund: "Datei mit weniger als 3 gueltigen Rahmen") auf einem
# tatsaechlich erreichbaren Weg. ------------------------------------------
SHORT="$WORK/mp2_short_1frame.mp2"
head -c $((FRSZ*1)) "$MP2" > "$SHORT"
OUT=$("$TOOL" -a "$SHORT"); RC=$?
TF2=$(echo "$OUT" | grep -oP '^total_frames=\K[0-9]+')
TM2=$(echo "$OUT" | grep -oP '^total_ms=\K[0-9]+')
if [ "$RC" -eq 0 ] && [ "$TF2" = "1" ] && [ "$TM2" = "24" ]; then ok "MP2 short-file (<baseline) ms (frames=$TF2 ms=$TM2)"
else bad "MP2 short-file (<baseline) ms (rc=$RC frames=$TF2 ms=$TM2)"; fi

# --- Check 3 (Gate 1a): Muell zwischen Rahmen -> Fix byte-identisch Original
FIXED="$WORK/mp2_junk_between_fixed.mp2"
"$TOOL" -f "$JUNK" "$FIXED"; RC=$?
if [ "$RC" -eq 1 ] && cmp -s "$FIXED" "$MP2"; then ok "MP2 fix junk-between (byte-identisch)"
else bad "MP2 fix junk-between (rc=$RC)"; fi

# --- Check 4 (Gate 1b): Rahmenanfang ueberschrieben -> Original minus Rahmen
CLOB="$WORK/mp2_clobbered.mp2"
REFMINUS="$WORK/mp2_minus_frame100.mp2"
head -c $((FRSZ*100)) "$MP2" > "$CLOB"
head -c 32 /dev/urandom >> "$CLOB"                 # zerstoert Header von Rahmen 100
tail -c +$((FRSZ*100+33)) "$MP2" >> "$CLOB"
head -c $((FRSZ*100)) "$MP2" > "$REFMINUS"
tail -c +$((FRSZ*101+1)) "$MP2" >> "$REFMINUS"
FIXED2="$WORK/mp2_clobbered_fixed.mp2"
"$TOOL" -f "$CLOB" "$FIXED2"; RC=$?
if [ "$RC" -eq 1 ] && cmp -s "$FIXED2" "$REFMINUS"; then ok "MP2 fix clobbered-frame (Original minus Rahmen 100)"
else bad "MP2 fix clobbered-frame (rc=$RC)"; fi

# --- Check 5: Fix auf sauberer Datei -> exit 0, byte-identisch
FIXED3="$WORK/mp2_clean_fixed.mp2"
"$TOOL" -f "$MP2" "$FIXED3"; RC=$?
if [ "$RC" -eq 0 ] && cmp -s "$FIXED3" "$MP2"; then ok "MP2 fix clean passthrough"
else bad "MP2 fix clean passthrough (rc=$RC)"; fi

# --- CRC-Referenzmaterial fuer Check 6/7 --------------------------------
# Gemessen (2026-08-16): Tux-MP2 hat KEIN CRC (protection bit=1, xxd -l4
# zeigt fd), der ffmpeg-Standard-mp2-Encoder schreibt NIE ein CRC (kein
# entsprechender Codec-Parameter), und beide Tonspuren des SDTV-Korpus
# (MPEG2_SD576i25_16-9_AV-async_MP2-deu+eng_Comedy-Central) ebenfalls ohne
# CRC (DVB-Encoder verzichten haeufig auf Error Protection). Abweichung von
# der im Brief skizzierten Fallback-Kette: statt des SDTV-Korpus liefert
# libtwolame selbst eine echte CRC-Referenz (`-error_protection 1`,
# gemessen: Byte1-Bit0=0) - dieselbe Bibliothek, aus der die nbal/line-
# Tabellen transkribiert wurden. Kein synthetisches CRC, sondern ein
# echter ISO-11172-3-Encoder-Pfad auf dem Tux-Ton.
MP2CRC="$WORK/mp2_crc_twolame.mp2"
if ffmpeg -hide_banner -encoders 2>/dev/null | grep -q libtwolame; then
    ffmpeg -y -v error -i "$MP2" -c:a libtwolame -error_protection 1 -b:a 192k "$MP2CRC"
fi
if [ ! -s "$MP2CRC" ]; then
    bad "MP2 CRC-Referenz: libtwolame-Encoder nicht verfuegbar/fehlgeschlagen"
else
    FRSZ2=576   # 192kbit/48kHz -> 576 Byte/Rahmen exakt (kein Padding-Rest)

    # --- Check 6: clean MP2 -> 0 crc_bad_frames (Kalibrierung: falscher
    #     CRC-Scope wuerde hier ~100% aller Rahmen als defekt melden)
    OUT=$("$TOOL" -a "$MP2CRC")
    CB=$(echo "$OUT" | grep -oP '^crc_bad_frames=\K.*' || true)
    if [ -z "$CB" ]; then ok "MP2 clean: keine CRC-Defekte"
    else bad "MP2 clean: crc_bad=$CB"; fi

    # --- Check 7: Bit-Flip im geschuetzten Bereich von Rahmen 50 -> genau
    #     dieser Rahmen gemeldet, Struktur intakt (exit 1, total_frames voll),
    #     Cross-Check: ffmpeg -err_detect crccheck meckert ebenfalls
    FLIP="$WORK/mp2_crcflip.mp2"
    cp "$MP2CRC" "$FLIP"
    printf '\x00' | dd of="$FLIP" bs=1 seek=$((FRSZ2*50+6)) conv=notrunc 2>/dev/null
    OUT=$("$TOOL" -a "$FLIP"); RC=$?
    CB=$(echo "$OUT" | grep -oP '^crc_bad_frames=\K.*' || true)
    FFERR=$(ffmpeg -v error -err_detect crccheck -i "$FLIP" -f null - 2>&1 | head -1)
    if [ "$RC" -eq 1 ] && [[ "$CB" == 50@* ]] && [ -n "$FFERR" ]; then
        ok "MP2 crc-flip: Rahmen 50 gemeldet, ffmpeg bestaetigt ($FFERR)"
    else bad "MP2 crc-flip (rc=$RC crc_bad=$CB ffmpeg='$FFERR')"; fi

    # --- Check 7b (Finding 1a): Fix auf CRC-only-beschaedigter Datei -> rc=1,
    #     Ausgabe byte-identisch zur beschaedigten Eingabe (der CRC-defekte
    #     Rahmen wird bewusst durchkopiert, nicht als "nicht sauber" verworfen
    #     -- Controller-Vorgabe: Selbstpruefung ist STRUKTURELL, nicht
    #     defektfrei), analyze auf der Ausgabe meldet weiterhin denselben
    #     CRC-Befund.
    FIXCRC="$WORK/mp2_crcflip_fixed.mp2"
    "$TOOL" -f "$FLIP" "$FIXCRC"; RC=$?
    OUT2=$("$TOOL" -a "$FIXCRC" 2>/dev/null); CB2=$(echo "$OUT2" | grep -oP '^crc_bad_frames=\K.*' || true)
    if [ "$RC" -eq 1 ] && cmp -s "$FIXCRC" "$FLIP" && [[ "$CB2" == 50@* ]]; then
        ok "MP2 fix crc-only preserves CRC-bad frame (rc=$RC, crc_bad=$CB2)"
    else bad "MP2 fix crc-only preserves CRC-bad frame (rc=$RC crc=$CB2, byte-identisch=$(cmp -s "$FIXCRC" "$FLIP" && echo ja || echo nein))"; fi
fi

AC3="$CACHE/tux_h264_1080p_progressive_test.ac3"
# AC3-Rahmengroesse = Abstand der ersten beiden 0b77-Syncwords (CBR)
FRSZ_AC3=$(python3 - "$AC3" <<'EOF'
import sys
d = open(sys.argv[1], 'rb').read(65536)
first = d.find(b'\x0b\x77')
second = d.find(b'\x0b\x77', first + 2)
print(second - first)
EOF
)

# --- Check 8: clean AC3 analyze -> exit 0, Rahmenzahl == ffprobe
OUT=$("$TOOL" -a "$AC3"); RC=$?
REF=$(ffprobe -v error -select_streams a:0 -count_packets \
      -show_entries stream=nb_read_packets -of csv=p=0 "$AC3" | tr -d ',')
TF=$(echo "$OUT" | grep -oP '^total_frames=\K[0-9]+')
CODEC=$(echo "$OUT" | grep -oP '^codec=\K.*')
if [ "$RC" -eq 0 ] && [ "$TF" = "$REF" ] && [ "$CODEC" = "ac3" ]; then
    ok "clean AC3 analyze (frames=$TF)"
else bad "clean AC3 analyze (rc=$RC frames=$TF ref=$REF codec=$CODEC)"; fi

# --- Check 9 (Gate 1a AC3): Muell zwischen Rahmen -> Fix byte-identisch
JUNKA="$WORK/ac3_junk_between.ac3"
head -c $((FRSZ_AC3*100)) "$AC3" > "$JUNKA"
head -c 468 /dev/urandom >> "$JUNKA"
tail -c +$((FRSZ_AC3*100+1)) "$AC3" >> "$JUNKA"
FIXA="$WORK/ac3_junk_between_fixed.ac3"
"$TOOL" -f "$JUNKA" "$FIXA"; RC=$?
if [ "$RC" -eq 1 ] && cmp -s "$FIXA" "$AC3"; then ok "AC3 fix junk-between"
else bad "AC3 fix junk-between (rc=$RC)"; fi

# --- Check 10 (Gate 1b AC3): Rahmenanfang ueberschrieben -> Original minus Rahmen
CLOBA="$WORK/ac3_clobbered.ac3"
head -c $((FRSZ_AC3*100)) "$AC3" > "$CLOBA"
head -c 32 /dev/urandom >> "$CLOBA"
tail -c +$((FRSZ_AC3*100+33)) "$AC3" >> "$CLOBA"
REFA="$WORK/ac3_minus_frame100.ac3"
head -c $((FRSZ_AC3*100)) "$AC3" > "$REFA"
tail -c +$((FRSZ_AC3*101+1)) "$AC3" >> "$REFA"
FIXA2="$WORK/ac3_clobbered_fixed.ac3"
"$TOOL" -f "$CLOBA" "$FIXA2"; RC=$?
if [ "$RC" -eq 1 ] && cmp -s "$FIXA2" "$REFA"; then ok "AC3 fix clobbered-frame"
else bad "AC3 fix clobbered-frame (rc=$RC)"; fi

# --- Check 11: CRC-Flip in Rahmen 50 -> gemeldet + ffmpeg bestaetigt
FLIPA="$WORK/ac3_crcflip.ac3"
cp "$AC3" "$FLIPA"
printf '\x00' | dd of="$FLIPA" bs=1 seek=$((FRSZ_AC3*50+10)) conv=notrunc 2>/dev/null
OUT=$("$TOOL" -a "$FLIPA"); RC=$?
CB=$(echo "$OUT" | grep -oP '^crc_bad_frames=\K.*' || true)
FFERR=$(ffmpeg -v error -err_detect crccheck -i "$FLIPA" -f null - 2>&1 | head -1)
if [ "$RC" -eq 1 ] && [[ "$CB" == 50@* ]] && [ -n "$FFERR" ]; then
    ok "AC3 crc-flip: Rahmen 50 gemeldet, ffmpeg bestaetigt"
else bad "AC3 crc-flip (rc=$RC crc_bad=$CB ffmpeg='$FFERR')"; fi

# --- Check 11b (Finding 1b): Muell + CRC-Defekt kombiniert -> Fix entfernt
#     NUR den Muell, der CRC-defekte Rahmen (50) bleibt erhalten (rc=1,
#     Ausgabe == crcflip-Datei, crc_bad_frames weiterhin gemeldet). Muell
#     zwischen Rahmen 20/21, weit vor dem CRC-Defekt in Rahmen 50, damit sich
#     beide Effekte nicht ueberlappen.
JUNKCRC="$WORK/ac3_junk_plus_crcflip.ac3"
head -c $((FRSZ_AC3*20)) "$FLIPA" > "$JUNKCRC"
head -c 468 /dev/urandom >> "$JUNKCRC"
tail -c +$((FRSZ_AC3*20+1)) "$FLIPA" >> "$JUNKCRC"
FIXJC="$WORK/ac3_junk_plus_crcflip_fixed.ac3"
"$TOOL" -f "$JUNKCRC" "$FIXJC"; RC=$?
OUT3=$("$TOOL" -a "$FIXJC" 2>/dev/null); CB3=$(echo "$OUT3" | grep -oP '^crc_bad_frames=\K.*' || true)
if [ "$RC" -eq 1 ] && cmp -s "$FIXJC" "$FLIPA" && [[ "$CB3" == 50@* ]]; then
    ok "AC3 fix junk+crc: junk removed, CRC-bad frame preserved (crc_bad=$CB3)"
else bad "AC3 fix junk+crc (rc=$RC crc=$CB3, byte-identisch=$(cmp -s "$FIXJC" "$FLIPA" && echo ja || echo nein))"; fi

# E-AC3-Material einmalig aus der SES-UHD-Demo extrahieren (Cache in $WORK)
EAC3="$WORK/ses_demo.eac3"
SES="/net/nase2021/Video/Sonstiges/TTCut-ng-Test-Files/UHDTV/HEVC_Main10-HDR10_2160p50_IDRonly-noRASL-noB_EAC3-2.0-deu_SES-UHD-Demo/2026-06-22.17.15.87-0.rec/00001.ts"
[ -s "$EAC3" ] || ffmpeg -v error -i "$SES" -map 0:a:0 -c copy "$EAC3"

# Byte-Offset von Rahmen N und N+1 (0-basiert) ueber Syncword-Kette
eac3_frame_offset() {  # $1=Datei $2=RahmenNr -> stdout: offset
    python3 - "$1" "$2" <<'EOF'
import sys
d = open(sys.argv[1], 'rb').read()
n = int(sys.argv[2]); pos = 0
for _ in range(n):
    pos = d.find(b'\x0b\x77', pos + 2)
    if pos < 0: sys.exit(1)
print(d.find(b'\x0b\x77', 0) if n == 0 else pos)
EOF
}

# --- Check 12: clean E-AC3 analyze -> Rahmenzahl == ffprobe minus 1, codec=eac3
# Kalibrierungsbefund (2026-08-16): die SES-Demo-TS selbst (nicht nur die
# hier extrahierte ES-Datei) hat einen echt abgeschnittenen letzten E-AC3-
# Rahmen -- `ffprobe -show_packets` auf der ORIGINAL-TS zeigt denselben
# letzten Paket mit nur 170 statt 512 Byte, und ffmpegs eigener eac3-Decoder
# meldet dafuer "incomplete frame". Das ist ein echtes Aufnahme-Ende-
# Artefakt (kein Extraktions- oder Parser-Bug) -- ffprobes nb_read_packets
# zaehlt dieses unvollstaendige Container-Paket mit, unser Tool zaehlt nur
# vollstaendige Syncframes und meldet den Rest korrekt als abgeschnittenen
# Rahmen (gleiches Verhalten wie der truncated-file-Fall aus Task 1/Task 6).
# Erwartung deshalb TF=REF-1, dropped_frames=1, rc=1, 0 CRC-Defekte auf den
# 11389 echten Rahmen.
OUT=$("$TOOL" -a "$EAC3"); RC=$?
REF=$(ffprobe -v error -select_streams a:0 -count_packets \
      -show_entries stream=nb_read_packets -of csv=p=0 "$EAC3" | tr -d ',')
TF=$(echo "$OUT" | grep -oP '^total_frames=\K[0-9]+')
DF=$(echo "$OUT" | grep -oP '^dropped_frames=\K[0-9]+')
CB=$(echo "$OUT" | grep -oP '^crc_bad_frames=\K.*' || true)
CODEC=$(echo "$OUT" | grep -oP '^codec=\K.*')
if [ "$RC" -eq 1 ] && [ "$TF" = "$((REF-1))" ] && [ "$DF" = "1" ] && [ -z "$CB" ] && [ "$CODEC" = "eac3" ]; then
    ok "clean E-AC3 analyze (frames=$TF, 1 genuinely truncated tail frame, 0 CRC defects)"
else bad "clean E-AC3 analyze (rc=$RC frames=$TF ref=$REF dropped=$DF crc_bad=$CB codec=$CODEC)"; fi

# Kanonische bereinigte Referenz: EAC3 hat selbst einen echten abgeschnittenen
# letzten Rahmen (Kalibrierungsbefund oben) -- ein korrekter Fix entfernt
# diesen ebenfalls. Checks 13/14 vergleichen deshalb gegen die vom Tool selbst
# erzeugte bereinigte Fassung (EAC3REF), nicht gegen die rohe EAC3-Datei --
# sonst schluege der Vergleich am echten Rahmen-11389-Defekt fehl, nicht an
# der jeweils injizierten Kunst-Beschaedigung, die der Check pruefen soll.
EAC3REF="$WORK/eac3_clean_fixed.eac3"
"$TOOL" -f "$EAC3" "$EAC3REF"

# --- Check 13 (Gate 1a E-AC3): Muell zwischen Rahmen 100 und 101
O100=$(eac3_frame_offset "$EAC3" 100)
JUNKE="$WORK/eac3_junk_between.eac3"
head -c "$O100" "$EAC3" > "$JUNKE"
head -c 468 /dev/urandom >> "$JUNKE"
tail -c +$((O100+1)) "$EAC3" >> "$JUNKE"
FIXE="$WORK/eac3_junk_between_fixed.eac3"
"$TOOL" -f "$JUNKE" "$FIXE"; RC=$?
if [ "$RC" -eq 1 ] && cmp -s "$FIXE" "$EAC3REF"; then ok "E-AC3 fix junk-between"
else bad "E-AC3 fix junk-between (rc=$RC)"; fi

# --- Check 14 (Gate 1b E-AC3): Rahmen-100-Anfang ueberschrieben -> Referenz minus Rahmen 100
O101=$(eac3_frame_offset "$EAC3" 101)
CLOBE="$WORK/eac3_clobbered.eac3"
head -c "$O100" "$EAC3" > "$CLOBE"
head -c 32 /dev/urandom >> "$CLOBE"
tail -c +$((O100+33)) "$EAC3" >> "$CLOBE"
REFE="$WORK/eac3_minus_frame100.eac3"
head -c "$O100" "$EAC3REF" > "$REFE"
tail -c +$((O101+1)) "$EAC3REF" >> "$REFE"
FIXE2="$WORK/eac3_clobbered_fixed.eac3"
"$TOOL" -f "$CLOBE" "$FIXE2"; RC=$?
if [ "$RC" -eq 1 ] && cmp -s "$FIXE2" "$REFE"; then ok "E-AC3 fix clobbered-frame"
else bad "E-AC3 fix clobbered-frame (rc=$RC)"; fi

# --- Check 15: Zufallsdaten -> Exit 2 (kein Codec erkennbar), kein Crash ---
head -c 1048576 /dev/urandom > "$WORK/random.bin"
"$TOOL" -a "$WORK/random.bin"; RC=$?
[ "$RC" -eq 2 ] && ok "random data -> exit 2" || bad "random data (rc=$RC)"

# --- Check 16: Abgeschnittene Datei (halber Rahmen am Ende) -> Exit 1, dropped_frames=1
head -c $((FRSZ*10+100)) "$MP2" > "$WORK/trunc.mp2"
OUT=$("$TOOL" -a "$WORK/trunc.mp2"); RC=$?
DF=$(echo "$OUT" | grep -oP '^dropped_frames=\K[0-9]+')
[ "$RC" -eq 1 ] && [ "$DF" = "1" ] && ok "truncated file" || bad "truncated file (rc=$RC dropped=$DF)"

# --- Check 17: Leere Datei -> Exit 2
: > "$WORK/empty.bin"
"$TOOL" -a "$WORK/empty.bin"; RC=$?
[ "$RC" -eq 2 ] && ok "empty file -> exit 2" || bad "empty file (rc=$RC)"

# --- Check 18: Demux-Integration: defect_slice.ts -> repariertes MP2 +
#     .info-Felder (Aufruf-Syntax verifiziert: `ttcut-demux <input> [outdir]`,
#     kein `-o`-Flag)
SLICE=/usr/local/src/CLAUDE_TMP/TTCut-ng/audiobug/defect_slice.ts
DEMUXOUT="$WORK/demux_slice"
rm -rf "$DEMUXOUT"; mkdir -p "$DEMUXOUT"
PATH="$ROOT/tools/ttcut-audiofix:$PATH" \
    "$ROOT/tools/ttcut-demux/ttcut-demux" "$SLICE" "$DEMUXOUT" >"$WORK/demux_slice.log" 2>&1
INFO=$(ls "$DEMUXOUT"/*.info 2>/dev/null | head -1)
MP2OUT=$(ls "$DEMUXOUT"/*.mp2 2>/dev/null | head -1)
CR=$(grep -oP '^audio_0_corrupt_ranges=\K.*' "$INFO" 2>/dev/null || true)
"$TOOL" -a "$MP2OUT" >/dev/null; RC=$?
if [ -n "$CR" ] && [ "$RC" -eq 0 ]; then ok "demux integration (ranges=$CR, ES sauber)"
else bad "demux integration (ranges='$CR' es_rc=$RC)"; fi

# --- Check 19: Clean-Baseline — unbeschaedigte Aufnahme, ein Lauf MIT
#     audiofix im PATH, ein Lauf OHNE -> Audio-ES byte-identisch, und die
#     audio_*_corrupt_*-Felder fehlen in BEIDEN .infos
#
# Messbefund (2026-08-16): die im Brief vorgeschlagene SDTV-Aufnahme
# (Comedy-Central-Mitschnitt) ist NICHT beschaedigungsfrei -- ttcut-audiofix
# meldet auf beiden Sprachspuren identisch 778 Muell-Bytes bei Rahmen ~1033
# (~41s, echte DVB-Empfangsstoerung, beide Tonspuren gleichzeitig betroffen)
# UND je 1 dropped_frame am Dateiende (Rahmen ~81425, echter abgeschnittener
# letzter Rahmen -- dieselbe Klasse Aufnahme-Ende-Artefakt wie beim E-AC3-
# Kalibrierungsbefund bei Check 12/EAC3REF). Als "Clean-Baseline" ungeeignet.
# Ersetzt durch ein vollsynthetisches TS (lavfi testsrc2+sine -> libx264/ac3),
# das End-zu-Ende gueltige PTS hat und damit strukturell garantiert frei von
# Muell/CRC-Defekten UND vom Aufnahme-Ende-Abschneider ist (Encoder flusht nur
# vollstaendige AC3-Rahmen). Einmalig erzeugt, in $WORK gecacht.
CLEANTS="$WORK/synth_clean.ts"
if [ ! -s "$CLEANTS" ]; then
    ffmpeg -y -v error -f lavfi -i "testsrc2=size=1280x720:rate=25:duration=20" \
        -f lavfi -i "sine=frequency=1000:duration=20" \
        -c:v libx264 -preset ultrafast -pix_fmt yuv420p -c:a ac3 -b:a 384k \
        "$CLEANTS"
fi
CLEAN_A="$WORK/clean_with"; CLEAN_B="$WORK/clean_without"
rm -rf "$CLEAN_A" "$CLEAN_B"; mkdir -p "$CLEAN_A" "$CLEAN_B"
PATH="$ROOT/tools/ttcut-audiofix:$PATH" \
  "$ROOT/tools/ttcut-demux/ttcut-demux" "$CLEANTS" "$CLEAN_A" >"$WORK/clean_a.log" 2>&1
"$ROOT/tools/ttcut-demux/ttcut-demux" "$CLEANTS" "$CLEAN_B" >"$WORK/clean_b.log" 2>&1
CLEAN_OK=1
for f in "$CLEAN_A"/*.mp2 "$CLEAN_A"/*.ac3; do
    [ -e "$f" ] || continue
    cmp -s "$f" "$CLEAN_B/$(basename "$f")" || CLEAN_OK=0
done
grep -q 'audio_._corrupt_ranges' "$CLEAN_A"/*.info && CLEAN_OK=0
grep -q 'audio_._corrupt_ranges' "$CLEAN_B"/*.info && CLEAN_OK=0
[ "$CLEAN_OK" -eq 1 ] && ok "clean baseline byte-identisch" || bad "clean baseline"

# --- Check 20 (--full only, Task 10): real-recording end-to-end -----------
# Replays the original failure: a 90-min-class MPEG-2/MP2 cut of the
# RTLZWEI recording whose 468-byte junk region at ~283s (frame ~7095) made
# mplex abort with "defekter Strom" and drop the audio track (85 min video,
# 4.7 min audio). Full pipeline: ttcut-demux -> ttcut-audiofix sanitizes the
# MP2 in-flight -> TTCut-ng auto-cut (MPEG-2 default muxer = mplex) -> mplex
# must complete without aborting -> final .mpg video/audio duration must
# match within 1s.
#
# Runs against an ISOLATED XDG_CONFIG_HOME (a minimal hand-written .conf,
# NOT a copy of the real user config) so this never reads or writes
# ~/.config/TTCut-ng -- only Mpeg2Muxer=0 (mplex, the header default and the
# setting active at the time of the original bug), LogCutPipeline/LogUI for
# the mplex-outcome check below, and paths into $WORK are set; everything
# else falls back to TTSettings' compiled-in defaults.
if [ "$RUN_FULL" -eq 1 ]; then
    REALTS="/net/nase2021/Video/Sonstiges/TTCut-ng-Test-Files/SDTV/MPEG2_SD576i25_aspect-switch-4-3-to-16-9_MP2-deu_RTLZWEI/2023-10-19.02.32.5-0.rec/00001.ts"
    FULLDIR="$WORK/full"
    if [ ! -s "$REALTS" ]; then
        bad "real-recording end-to-end: source TS not found ($REALTS) -- dev-machine-only corpus"
    else
        # Demux (cached across gate re-runs -- the source is a static corpus
        # file, minutes of ffmpeg work otherwise).
        if [ ! -s "$FULLDIR/RTLZWEI_full.info" ]; then
            rm -rf "$FULLDIR"; mkdir -p "$FULLDIR"
            PATH="$ROOT/tools/ttcut-audiofix:$PATH" \
                "$ROOT/tools/ttcut-demux/ttcut-demux" -e -n RTLZWEI_full "$REALTS" "$FULLDIR" \
                >"$WORK/full.log" 2>&1
        fi
        FULLINFO="$FULLDIR/RTLZWEI_full.info"
        FULLM2V="$FULLDIR/RTLZWEI_full.m2v"
        FULLMP2="$FULLDIR/RTLZWEI_full_deu.mp2"
        CR=$(grep -oP '^audio_0_corrupt_ranges=\K.*' "$FULLINFO" 2>/dev/null || true)
        # The corrupt range's first frame number must fall near the known
        # defect at frame ~7095 (measured 2026-08-16: corrupt_frame_ranges
        # in the .info is exactly 7095-7095; audio_0_corrupt_ranges' first
        # entry is the nearest audio-frame equivalent). +-50 frames tolerance
        # for demux-version drift in exact frame accounting.
        CRFIRST=$(echo "$CR" | grep -oP '^\K[0-9]+' || echo -1)
        "$TOOL" -a "$FULLMP2" >/dev/null 2>&1; AFRC=$?
        if [ -n "$CR" ] && [ "$CRFIRST" -ge 7045 ] && [ "$CRFIRST" -le 7145 ] && [ "$AFRC" -eq 0 ]; then
            ok "real-recording demux+sanitize (corrupt_ranges=$CR, audiofix -a rc=$AFRC)"
        else
            bad "real-recording demux+sanitize (corrupt_ranges='$CR' first=$CRFIRST audiofix_rc=$AFRC)"
        fi

        # Project file: adapt the rtl2.ttcut template (audiobug/) to the
        # fresh demux output. CutOut=120499 (~80min) spans the defect frame.
        PROJ="$WORK/realcase.ttcut"
        cat > "$PROJ" <<EOF
<!DOCTYPE TTCut-Projectfile>
<TTCut-Projectfile>
 <Version>1.0</Version>
 <Video>
  <Order>0</Order>
  <Name>$FULLM2V</Name>
  <Audio>
   <Order>0</Order>
   <Name>$FULLMP2</Name>
   <Language>deu</Language>
  </Audio>
  <Cut>
   <Order>0</Order>
   <CutIn>500</CutIn>
   <CutOut>120499</CutOut>
  </Cut>
 </Video>
</TTCut-Projectfile>
EOF

        XDGCFG="$WORK/full_xdgconfig"; XDGCACHE="$WORK/full_xdgcache"
        APPTMP="$WORK/full_apptmp"; OUTDIR="$WORK/full_output"
        rm -rf "$XDGCFG" "$XDGCACHE" "$APPTMP" "$OUTDIR"
        mkdir -p "$XDGCFG/TTCut-ng" "$XDGCACHE" "$APPTMP" "$OUTDIR"
        # Both Mpeg2Muxer AND OutputContainer must be 0/mplex: TTSettings::load()
        # sets the transient workingOutputContainer from OutputContainer
        # directly; setEncoderCodec(0)'s Mpeg2Muxer resync (called by
        # runAutoCutMode for an MPEG-2 stream) is a no-op here because
        # mEncoderCodec's compiled default is ALREADY 0 (early-return on
        # "value unchanged" -- confirmed by measurement: Mpeg2Muxer=0 alone
        # produced OutputContainer=1/MKV in the run's own saved settings).
        cat > "$XDGCFG/TTCut-ng/TTCut-ng.conf" <<EOF
[Settings]
Common\\TempDirPath=$APPTMP
CutOptions\\DirPath=$OUTDIR
Encoder\\Mpeg2Muxer=0
LogFile\\CreateLogFile=false
LogFile\\LogModeConsole=true
LogFile\\LogCutPipeline=true
LogFile\\LogUI=true
Muxer\\MuxOutputDir=$OUTDIR
Muxer\\OutputContainer=0
EOF

        OUTBASE="$OUTDIR/realcase_cut"
        # XDG_CACHE_HOME isolates TTMessageLogger's own logfile.log (written
        # regardless of CreateLogFile's file-content toggle) from the real
        # ~/.cache/ttcut-ng -- LogModeConsole=true is what actually gets the
        # mplex transcript into full_autocut.log below (stderr).
        env XDG_CONFIG_HOME="$XDGCFG" XDG_CACHE_HOME="$XDGCACHE" TMPDIR="$APPTMP" \
            PATH="$ROOT/tools/ttcut-audiofix:$PATH" \
            "$ROOT/build/ttcut-ng" --project "$PROJ" --auto-cut "$OUTBASE" \
            >"$WORK/full_autocut.log" 2>&1
        AUTOCUT_RC=$?

        FINALMPG="${OUTBASE}.mpg"
        # mplex success: reaches its end-of-run marker, never emits the
        # abort message that dropped the audio track in the original bug
        # ("defekter Strom" / "corrupt stream" mplex error text).
        if grep -qi "defekter Strom\|corrupt stream" "$WORK/full_autocut.log" 2>/dev/null; then
            bad "real-recording mplex aborted (see $WORK/full_autocut.log)"
        elif [ "$AUTOCUT_RC" -ne 0 ] || ! grep -q "Scanned to end AU" "$WORK/full_autocut.log" 2>/dev/null; then
            bad "real-recording auto-cut/mplex did not complete (rc=$AUTOCUT_RC, see $WORK/full_autocut.log)"
        elif [ ! -s "$FINALMPG" ]; then
            bad "real-recording: no output .mpg produced"
        else
            VDUR=$(ffprobe -v error -select_streams v:0 -show_entries stream=duration -of default=nw=1:nk=1 "$FINALMPG")
            ADUR=$(ffprobe -v error -select_streams a:0 -show_entries stream=duration -of default=nw=1:nk=1 "$FINALMPG")
            DIFF=$(python3 -c "print(abs($VDUR-$ADUR))" 2>/dev/null || echo 999)
            UNDER1S=$(python3 -c "print(1 if abs($VDUR-$ADUR) < 1.0 else 0)" 2>/dev/null || echo 0)
            if [ "$UNDER1S" = "1" ]; then
                ok "real-recording mplex end-to-end (video=${VDUR}s audio=${ADUR}s diff=${DIFF}s)"
            else
                bad "real-recording A/V duration drift too large (video=${VDUR}s audio=${ADUR}s diff=${DIFF}s)"
            fi
        fi
    fi
    echo "NOTE: Step 3 (GUI acceptance -- 'Tonstoerungen:'/'Bildstoerungen:' markers visible in TTCut-ng on $FULLDIR) is a human check, not automated here."
fi

echo "----"; echo "gate_audiofix: $PASS pass, $FAIL fail"
[ "$FAIL" -eq 0 ]
