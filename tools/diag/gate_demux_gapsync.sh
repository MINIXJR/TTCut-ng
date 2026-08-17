#!/bin/bash
# gate_demux_gapsync.sh — Abnahme-Gate fuer die Audio-Gap/Video-Gap-
# Verrechnung in ttcut-demux (compute_audio_gap_silence_ms +
# emit_video_only_truncations).
#
# Hintergrund (2026-08-17, Aufnahme "Gerber"): Ein Sendeausfall trifft Video
# und Audio zur selben Realzeit, aber die PTS-Fenster der Gaps liegen durch
# den PES-Muxer-Vorlauf des Videos ~0.8s auseinander und ueberlappen nur
# teilweise. compute_audio_gap_silence_ms zaehlt bei Intersect bewusst den
# VOLLEN Video-Gap (korrekt), emit_video_only_truncations rechnete aber nur
# die geometrische Ueberlappung als "geteilt" und kuerzte den Rest ZUSAETZLICH
# aus dem Audio -> derselbe Ausfall doppelt verrechnet, Ton lief ab der
# Stoerzone konstant ~0.8s voraus (gemessen: +812/+836/+843 ms auf allen 4
# Spuren, Kreuzkorrelation ES gegen Original-TS).
#
# Aufruf: tools/diag/gate_demux_gapsync.sh
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEMUX="$ROOT/tools/ttcut-demux/ttcut-demux"
WORK=/usr/local/src/CLAUDE_TMP/TTCut-ng/demux-gapsync-tests
mkdir -p "$WORK"
PASS=0; FAIL=0
ok()  { echo "PASS: $1"; PASS=$((PASS+1)); }
bad() { echo "FAIL: $1"; FAIL=$((FAIL+1)); }

[ -f "$DEMUX" ] || { echo "FAIL: $DEMUX fehlt"; exit 1; }

# Funktionen aus dem Skript extrahieren (Definitionen enden mit "}" in
# Spalte 1). Kein source des Gesamtskripts: das wuerde main ausfuehren.
extract_fn() {
    awk -v fn="$1" '$0 == fn"() {" {infn=1} infn {print} infn && /^}/ {exit}' "$DEMUX"
}
FN_FILE="$WORK/.fns.sh"
{ extract_fn compute_audio_gap_silence_ms
  extract_fn emit_video_only_truncations; } > "$FN_FILE"
grep -q "^compute_audio_gap_silence_ms() {" "$FN_FILE" || { echo "FAIL: Extraktion compute_audio_gap_silence_ms"; exit 1; }
grep -q "^emit_video_only_truncations() {" "$FN_FILE" || { echo "FAIL: Extraktion emit_video_only_truncations"; exit 1; }
# shellcheck disable=SC1090
source "$FN_FILE"

VG="$WORK/video_gaps.txt"
AG="$WORK/audio_gaps.txt"
OUT="$WORK/trunc_out.txt"

# --- Check 1: Realdaten Gerber — gleicher Ausfall, PES-versetzt ------------
# Am Original-TS gemessen (ffprobe, 2026-08-17):
#   Video-Gap: 90358.602989 -> 90359.842989 (1240 ms)
#   Audio-Gaps: Spur1/2 1248 ms, Spur3 1272 ms, Spur4(AC3) 1280 ms
# Erwartung: Silence = audio_gap - video_gap (8/8/32/40 ms), KEINE Truncation
# (Audio hat den Ausfall selbst schon verloren; audio_gap >= video_gap).
printf "90358.602989 90359.842989 1240\n" > "$VG"
declare -A EXP_SIL=( [1248a]=8 [1248b]=8 [1272]=32 [1280]=40 )
declare -A AGAP=(
  [1248a]="90357.774989 90359.022989 1248"
  [1248b]="90357.774989 90359.022989 1248"
  [1272]="90357.750989 90359.022989 1272"
  [1280]="90357.694989 90358.974989 1280"
)
for k in 1248a 1248b 1272 1280; do
    read -r AS AE AMS <<< "${AGAP[$k]}"
    printf "%s %s %s\n" "$AS" "$AE" "$AMS" > "$AG"
    SIL=$(compute_audio_gap_silence_ms "$AS" "$AE" "$VG" 50)
    if [ "$SIL" = "${EXP_SIL[$k]}" ]; then ok "Gerber $k: silence_ms=$SIL"
    else bad "Gerber $k: silence_ms=$SIL erwartet ${EXP_SIL[$k]}"; fi
    : > "$OUT"
    emit_video_only_truncations "$VG" "$AG" "$OUT"
    if [ ! -s "$OUT" ]; then ok "Gerber $k: keine Zusatz-Truncation"
    else bad "Gerber $k: unerwartete Truncation: $(cat "$OUT")"; fi
done

# --- Check 2: Echter Video-only-Verlust (Audio lief durch) -----------------
# Kein Audio-Gap -> Audio traegt Content, den das Video verlor: volle
# Kuerzung um 1240 ms muss bleiben (Kernaufgabe der Funktion).
: > "$AG"; : > "$OUT"
emit_video_only_truncations "$VG" "$AG" "$OUT"
if grep -q " -1240$" "$OUT"; then ok "video-only: Truncation -1240 bleibt"
else bad "video-only: erwartet -1240, bekam: $(cat "$OUT")"; fi

# --- Check 3: Audio verlor WENIGER als Video am selben Ort -----------------
# Audio-Gap 400 ms innerhalb des Video-Gap-Fensters: Video verlor 840 ms,
# die das Audio noch traegt -> Truncation -840 muss bleiben. Silence = 0.
printf "90358.700000 90359.100000 400\n" > "$AG"; : > "$OUT"
SIL=$(compute_audio_gap_silence_ms 90358.700000 90359.100000 "$VG" 50)
emit_video_only_truncations "$VG" "$AG" "$OUT"
if [ "$SIL" = "0" ]; then ok "Teilverlust: silence_ms=0"
else bad "Teilverlust: silence_ms=$SIL erwartet 0"; fi
if grep -q " -840$" "$OUT"; then ok "Teilverlust: Truncation -840 bleibt"
else bad "Teilverlust: erwartet -840, bekam: $(cat "$OUT")"; fi

# --- Check 5..7: repair_audio_with_silence_inserts end-to-end -------------
# Befund 2 (2026-08-17): eine ALLEINSTEHENDE Mini-Insertion (8 ms < 1
# MP2-Frame) erzeugte ueber den Datei-Trim eine 1-Frame-Silence-Datei, die
# der mp3-Demuxer nicht oeffnen kann ("two consecutive MPEG audio frames");
# der concat-Demuxer brach dort ab, schrieb nur Segment 1 (16.464s statt
# 120s) und ffmpeg gab trotzdem rc=0 -> Spur still zerstoert. Vor dem
# Verrechnungs-Fix latent (die Truncation-Zeile verschmolz die Insertion
# im Coalesce), danach real ausgeloest (Gerber deu/mis).
check_repair_e2e() {
    local label=$1 cls_line=$2 exp_dur=$3 tol=$4
    local f="$WORK/repro_$label.mp2"
    cp "$ROOT/tools/test-videos/cache/tux_mpeg2_576i_pal_test.mp2" "$f" || { bad "repair-e2e $label: Testdatei fehlt"; return; }
    printf "%s\n" "$cls_line" > "$WORK/cls_$label.txt"
    if ! repair_audio_with_silence_inserts "$f" "$WORK/cls_$label.txt" 100.0; then
        bad "repair-e2e $label: rc!=0"; return
    fi
    local dur
    dur=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f")
    if awk -v d="$dur" -v e="$exp_dur" -v t="$tol" 'BEGIN {a=d-e; if (a<0) a=-a; exit !(a<=t)}'; then
        ok "repair-e2e $label: Dauer $dur (Soll ${exp_dur}±${tol})"
    else
        bad "repair-e2e $label: Dauer $dur, erwartet ${exp_dur}±${tol}"
    fi
}
# repair braucht probe_audio_props + warn aus dem Demux-Skript
{ extract_fn probe_audio_props
  extract_fn repair_audio_with_silence_inserts; } > "$WORK/.fns_repair.sh"
grep -q "^repair_audio_with_silence_inserts() {" "$WORK/.fns_repair.sh" || { echo "FAIL: Extraktion repair_audio_with_silence_inserts"; exit 1; }
warn() { echo "WARN: $1"; }
# shellcheck disable=SC1090
source "$WORK/.fns_repair.sh"
# Check 5: alleinstehende 8ms-Insertion (Gerber-Fall) — Datei muss ~120s bleiben
check_repair_e2e "sil8ms"  "116.464000 117.712000 1248 8"    120.0 0.2
# Check 6: 32ms-Insertion (2 MP2-Frames, war schon vorher ok — Regression)
check_repair_e2e "sil32ms" "116.464000 117.736000 1272 32"   120.1 0.2
# Check 7: Truncation-Edit (war schon vorher ok — Regression): -812ms
check_repair_e2e "trunc"   "116.044000 117.292000 1248 -812" 119.2 0.2

# --- Check 4: Disjunkte Ereignisse (separater Audio-Ausfall + Video-Gap) ---
# Audio-Gap weit weg vom Video-Gap: beide unabhaengig voll verrechnen
# (Silence 500 dort, Truncation -1240 hier).
printf "90100.000000 90100.500000 500\n" > "$AG"; : > "$OUT"
SIL=$(compute_audio_gap_silence_ms 90100.000000 90100.500000 "$VG" 50)
emit_video_only_truncations "$VG" "$AG" "$OUT"
if [ "$SIL" = "500" ]; then ok "disjunkt: silence_ms=500"
else bad "disjunkt: silence_ms=$SIL erwartet 500"; fi
if grep -q " -1240$" "$OUT"; then ok "disjunkt: Truncation -1240 bleibt"
else bad "disjunkt: erwartet -1240, bekam: $(cat "$OUT")"; fi

echo
echo "=== $PASS PASS, $FAIL FAIL ==="
[ "$FAIL" -eq 0 ]
