#!/bin/bash
# gate_demux_gapsync.sh — Abnahme-Gate fuer repair_audio_with_silence_inserts()
# in ttcut-demux (die Silence-/Truncation-ASSEMBLY, nicht mehr die
# Gap-Verrechnung — die wurde durchs Stoerzonen-Modell ersetzt, siehe unten).
#
# Verrechnung (compute_audio_gap_silence_ms + emit_video_only_truncations)
# wurde 2026-08-24 entfernt: beide Funktionen zaehlten je einen Sendeausfall
# als ZWEI Zeilen (eine Audio-Gap-Zeile, eine Video-Truncation-Zeile), die
# BEIDE den Positions-Offset erhoehten — ein Ausfall doppelt verrechnet, Ton
# lief 808 ms hinter dem Bild (Aufnahme "03x15"). Ersetzt durch das
# Stoerzonen-Modell (build_disturbance_zones + build_zone_edits: eine Zone,
# eine Bilanz), Regressionsschutz jetzt tools/diag/gate_demux_zonesync.sh.
#
# Was hier bleibt: repair_audio_with_silence_inserts() nimmt eine fertige
# CLASSIFIED_FILE-Zeile (src_start src_end gap_ms silence_ms) und baut daraus
# per ffmpeg concat die reparierte Audiodatei zusammen — das ist unabhaengig
# davon, WIE die Zeile berechnet wurde, und deckt einen realen Defekt ab
# (2026-08-17, Aufnahme "Gerber"): eine ALLEINSTEHENDE Sub-Frame-Silence
# zerstoerte eine Tonspur (siehe Check 5..7 unten).
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
# repair braucht probe_audio_props, quantize_up_to_frame, audio_es_duration,
# repair_progress (ruft progress_set_repair) sowie warn aus dem Demux-Skript.
{ extract_fn probe_audio_props
  extract_fn quantize_up_to_frame
  extract_fn audio_es_duration
  extract_fn repair_progress
  extract_fn progress_set_repair
  extract_fn repair_audio_with_silence_inserts; } > "$WORK/.fns_repair.sh"
grep -q "^repair_audio_with_silence_inserts() {" "$WORK/.fns_repair.sh" || { echo "FAIL: Extraktion repair_audio_with_silence_inserts"; exit 1; }
warn() { echo "WARN: $1"; }
info() { :; }
# progress_set_repair's arithmetic reads these under "set -u"; progress_set()
# itself (called from progress_set_repair, not extracted here — its early
# "PROGRESS_FILE unset -> return" makes it a no-op) is skipped entirely
# because PROGRESS_FILE stays empty, so a stub is enough.
progress_set() { :; }
PROGRESS_REPAIR_BASE=0
PROGRESS_REPAIR_END=0
_REPAIR_TRACK_IDX=0
_REPAIR_TRACK_N=1
# shellcheck disable=SC1090
source "$WORK/.fns_repair.sh"
# Silence-Menge wird auf das NAECHSTE Framevielfache gerundet (nearest,
# nicht aufgerundet) — minimiert den Sync-Restfehler pro Edit auf ±1/2
# Frame (MP2-Frame = 24 ms):
# Check 5: 8ms-Soll -> nearest 0 Frames: kein Eintrag, Dauer exakt 120s
check_repair_e2e "sil8ms"  "116.464000 117.712000 1248 8"    120.000 0.005
# Check 6: 32ms-Soll -> nearest 1 Frame (24ms): Dauer 120.024s
check_repair_e2e "sil32ms" "116.464000 117.736000 1272 32"   120.024 0.005
# Check 7: Truncation-Edit -812ms (Schnitt quantisiert auf Framegrenzen)
check_repair_e2e "trunc"   "116.044000 117.292000 1248 -812" 119.188 0.026

echo
echo "=== $PASS PASS, $FAIL FAIL ==="
[ "$FAIL" -eq 0 ]
