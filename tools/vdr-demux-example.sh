#!/bin/bash
#
# vdr-demux-example.sh - Example script for demuxing VDR recordings with ttcut-demux
#
# This is an anonymized example of a VDR recording demux workflow.
# Adapt paths to your system before use.
#
# Prerequisites:
#   - ttcut-demux (from TTCut-ng tools/)
#   - ttcut-ng (optional, for launching the editor)
#   - kdialog or dialog (optional, for GUI selection)
#
# Workflow:
#   1. Discover VDR recordings and select via dialog
#   2. Demux with ttcut-demux -e -n (ES mode, named output)
#   3. Clean up unwanted audio tracks
#   4. Launch TTCut-ng
#
# Input:  VDR .rec directories containing 00001.ts (or 001.vdr)
# Output: Elementary streams (.264/.265/.m2v + audio) in output directory
#
# ttcut-demux automatically detects and concatenates multi-file VDR
# recordings (00001.ts, 00002.ts, ...) — no manual renaming needed.
#

set -e

# Konfiguration — adapt these paths to your system
IN_PFAD="$HOME/Videos/VDR"
OUT_PFAD="$HOME/Videos/TTCut_Output"
TTCUT="ttcut-ng"
TTCUT_DEMUX="ttcut-demux"

# Fallback auf lokale Version falls nicht installiert
[ -x "$(command -v "$TTCUT_DEMUX")" ] || TTCUT_DEMUX="/usr/local/src/TTCut-ng/tools/ttcut-demux/ttcut-demux"
[ -x "$(command -v "$TTCUT")" ] || TTCUT="/usr/local/src/TTCut-ng/ttcut-ng"

# Farben (deaktiviert wenn stdout kein Terminal ist)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    NC=''
fi

info()  { printf '%b\n' "${GREEN}[INFO]${NC} $1"; }
warn()  { printf '%b\n' "${YELLOW}[WARN]${NC} $1"; }
error() { printf '%b\n' "${RED}[ERROR]${NC} $1"; }
step()  { printf '%b\n' "${BLUE}[STEP]${NC} $1"; }

# VDR-VFAT-Demaskierung: #XX-Hex-Sequenzen → ASCII (Linux-safe)
# Spaces bleiben als Underscore. `#` selbst (#23) wird als Letztes ersetzt,
# damit ein literales `#23` im Originalnamen nicht doppelt entkodiert wird.
# Slashes (aus #2F oder bereits vorhanden) werden zu Underscore neutralisiert,
# damit das Ergebnis nie als Pfadtrenner missverstanden wird.
vdr_unmask() {
    local s="$1"
    s="${s//#3A/:}"
    s="${s//#3F/?}"
    s="${s//#22/\"}"
    s="${s//#2A/*}"
    s="${s//#2F//}"
    s="${s//#3C/<}"
    s="${s//#3E/>}"
    s="${s//#7C/|}"
    s="${s//#7E/~}"
    s="${s//#23/#}"
    s="${s//\//_}"
    printf '%s' "$s"
}

# ---- kdialog Fortschritts-Popup ----
PROGRESS_DBUS=""

progress_start() {
    local title="$1"
    local total="$2"
    if command -v kdialog &>/dev/null; then
        PROGRESS_DBUS=$(kdialog --progressbar "$title" "$total" --title "VDR Demux" 2>/dev/null) || PROGRESS_DBUS=""
        if [ -n "$PROGRESS_DBUS" ]; then
            qdbus $PROGRESS_DBUS setAutoClose true 2>/dev/null || true
        fi
    fi
}

progress_update() {
    local value="$1"
    local label="$2"
    if [ -n "$PROGRESS_DBUS" ]; then
        qdbus $PROGRESS_DBUS setLabelText "$label" 2>/dev/null || true
        qdbus $PROGRESS_DBUS Set "" value "$value" 2>/dev/null || true
    fi
}

progress_close() {
    if [ -n "$PROGRESS_DBUS" ]; then
        qdbus $PROGRESS_DBUS close 2>/dev/null || true
        PROGRESS_DBUS=""
    fi
}

# Prüfe Voraussetzungen
[ -d "$IN_PFAD" ] || { error "Quellverzeichnis nicht gefunden: $IN_PFAD"; exit 1; }
[ -x "$TTCUT_DEMUX" ] || { error "ttcut-demux nicht gefunden: $TTCUT_DEMUX"; exit 1; }

mkdir -p "$OUT_PFAD"

#############################################################################
# Aufnahmen suchen und Auswahldialog
#############################################################################
REC_DIRS=()
while IFS= read -r -d '' rec_dir; do
    # Nur .rec-Verzeichnisse mit Video-Dateien (.ts oder .vdr)
    if compgen -G "$rec_dir"/*.ts > /dev/null 2>&1 || compgen -G "$rec_dir"/*.vdr > /dev/null 2>&1; then
        REC_DIRS+=("$rec_dir")
    fi
done < <(find "$IN_PFAD" -type d -name "*.rec" -print0 2>/dev/null | sort -z)

if [ ${#REC_DIRS[@]} -eq 0 ]; then
    error "Keine VDR-Aufnahmen gefunden in: $IN_PFAD"
    exit 1
fi

# Baue Auswahlliste: Tag=Verzeichnispfad, Label=Sendung — Datum
CHECKLIST_ARGS=()
for dir in "${REC_DIRS[@]}"; do
    rel_path="${dir#$IN_PFAD/}"
    show_name="$(vdr_unmask "$(basename "$(dirname "$dir")")")"
    rec_name="${rel_path#*/}"
    rec_date=$(echo "$rec_name" | sed -E 's/^([0-9]{4}-[0-9]{2}-[0-9]{2})\.([0-9]{2})\.([0-9]{2})\..*/\1 \2:\3/')
    CHECKLIST_ARGS+=("$dir" "${show_name} — ${rec_date}" "on")
done

# Auswahldialog: kdialog -> dialog -> alle verarbeiten
SELECTED_DIRS=()
if command -v kdialog &>/dev/null; then
    # --separate-output prints one tag per line; mapfile parses safely without
    # `eval`, which would otherwise execute backticks/$() embedded in directory
    # names (or the kdialog response) as shell code.
    SELECTED=$(kdialog --separate-output --checklist \
        "Quellverzeichnis: $IN_PFAD"$'\n'"Zielverzeichnis: $OUT_PFAD"$'\n\n'"Aufnahmen auswählen (abgewählte werden übersprungen):" \
        "${CHECKLIST_ARGS[@]}" \
        --title "VDR Demux — ${#REC_DIRS[@]} Aufnahme(n)" 2>/dev/null) || exit 0
    if [[ -n "$SELECTED" ]]; then
        mapfile -t SELECTED_DIRS <<< "$SELECTED"
    fi
elif command -v dialog &>/dev/null; then
    SELECTED=$(dialog --stdout --separator $'\n' \
        --backtitle "Quelle: $IN_PFAD → Ziel: $OUT_PFAD" \
        --checklist "Aufnahmen auswählen (abgewählte werden übersprungen):" \
        0 0 0 "${CHECKLIST_ARGS[@]}") || exit 0
    mapfile -t SELECTED_DIRS <<< "$SELECTED"
    clear
else
    SELECTED_DIRS=("${REC_DIRS[@]}")
fi

if [ ${#SELECTED_DIRS[@]} -eq 0 ]; then
    warn "Keine Aufnahmen ausgewählt."
    exit 0
fi

info "=== VDR Demux gestartet: $(date) ==="
info "Quellverzeichnis: $IN_PFAD"
info "Zielverzeichnis: $OUT_PFAD"
info "${#SELECTED_DIRS[@]} von ${#REC_DIRS[@]} Aufnahme(n) ausgewählt"
echo ""

# Fortschritt: N (Demux pro Aufnahme) + 1 (Aufräumen) + 1 (Fertig)
TOTAL_STEPS=$((${#SELECTED_DIRS[@]} + 2))
CURRENT_STEP=0
progress_start "Vorbereitung..." "$TOTAL_STEPS"

#############################################################################
# SCHRITT 1: TS-Dateien finden und mit ttcut-demux demuxen
#############################################################################
step "Schritt 1: Demuxen mit ttcut-demux..."

DEMUX_COUNT=0
DEMUX_ERRORS=0

# Finde erste VDR-Segmentdatei in jedem .rec-Verzeichnis
TS_FILES=()
for rec_dir in "${SELECTED_DIRS[@]}"; do
    # Find first VDR segment (00001.ts or 001.vdr)
    first_seg=""
    if [ -f "$rec_dir/00001.ts" ]; then
        first_seg="$rec_dir/00001.ts"
    elif [ -f "$rec_dir/001.vdr" ]; then
        first_seg="$rec_dir/001.vdr"
    fi
    if [ -n "$first_seg" ]; then
        TS_FILES+=("$first_seg")
    fi
done

info "  ${#TS_FILES[@]} Aufnahmen gefunden"

# Fortschritt neu berechnen mit tatsächlicher Dateianzahl
TOTAL_STEPS=$((${#TS_FILES[@]} + 2))
progress_close
progress_start "Demuxe ${#TS_FILES[@]} Aufnahme(n)..." "$TOTAL_STEPS"
progress_update "$CURRENT_STEP" "Demuxe ${#TS_FILES[@]} Aufnahme(n)..."

# Verarbeite gesammelte Dateien
for ts_datei in "${TS_FILES[@]}"; do
    rec_dir=$(dirname "$ts_datei")
    # VDR directory structure: .../Series/Episode/Date.Time.rec/00001.ts
    # Use the directory directly above .rec as the show/episode name
    show_name="$(vdr_unmask "$(basename "$(dirname "$rec_dir")")")"

    info "Demuxe: $show_name ($(basename "$ts_datei"))"
    progress_update "$CURRENT_STEP" "Demuxe: $show_name"$'\n'"(${DEMUX_COUNT}/${#TS_FILES[@]})"

    LOG_FILE="$OUT_PFAD/${show_name}.log"

    # Prüfe ob marks Datei vorhanden ist (VDR markad)
    marks_info=""
    if [ -f "$rec_dir/marks" ]; then
        marks_info=" (mit VDR markers)"
    fi

    # ttcut-demux handles multi-file detection and concat automatically
    if "$TTCUT_DEMUX" -e -n "$show_name" "$ts_datei" "$OUT_PFAD" > "$LOG_FILE" 2>&1; then
        cat "$LOG_FILE"
        info "  OK: $show_name$marks_info"
        DEMUX_COUNT=$((DEMUX_COUNT + 1))
    else
        cat "$LOG_FILE"
        error "  FEHLER bei: $show_name"
        DEMUX_ERRORS=$((DEMUX_ERRORS + 1))
    fi
    echo ""
    CURRENT_STEP=$((CURRENT_STEP + 1))
done

info "  $DEMUX_COUNT Dateien demuxed, $DEMUX_ERRORS Fehler"
echo ""

#############################################################################
# SCHRITT 2: Unerwünschte Audio-Spuren löschen
#############################################################################
step "Schritt 2: Unerwünschte Audio-Spuren löschen..."
progress_update "$CURRENT_STEP" "Aufräumen..."

# Optional: delete unwanted audio tracks (e.g., *_mis.ac3, *_mul.mp2)
# Uncomment and adapt patterns as needed:
# for pattern in "*_mis.mp2" "*_mis.ac3" "*_mul.mp2" "*_mul.ac3"; do
#     while IFS= read -r -d '' datei; do
#         rm -v "$datei"
#     done < <(find "$OUT_PFAD" -name "$pattern" -print0 2>/dev/null)
# done

# Lösche übrig gebliebene .pad_logs_* Verzeichnisse
pad_cleaned=0
while IFS= read -r -d '' pad_dir; do
    rm -rf "$pad_dir" && pad_cleaned=$((pad_cleaned + 1))
done < <(find "$OUT_PFAD" -maxdepth 1 -type d -name ".pad_logs_*" -print0 2>/dev/null)
[ "$pad_cleaned" -gt 0 ] && info "  $pad_cleaned .pad_logs Verzeichnisse aufgeräumt"
echo ""
CURRENT_STEP=$((CURRENT_STEP + 1))

#############################################################################
# SCHRITT 3: Zusammenfassung und TTCut starten
#############################################################################
step "Schritt 3: Zusammenfassung"
progress_update "$CURRENT_STEP" "Fertig — $DEMUX_COUNT Aufnahme(n) demuxed"
echo ""

info "=== Demux abgeschlossen ==="
echo ""
echo "Ausgabeverzeichnis: $OUT_PFAD"
echo ""

# Zeige erstellte Dateien
if [ -d "$OUT_PFAD" ]; then
    echo "Erstellte Dateien:"
    find "$OUT_PFAD" -maxdepth 1 -type f \( -name "*.264" -o -name "*.265" -o -name "*.m2v" -o -name "*.info" -o -name "*.log" \) \
        -mmin -5 -exec ls -lh {} \; 2>/dev/null | head -20
    echo ""
fi

# Zeige Marker-Info falls vorhanden
marker_count=$(grep -l "^\[markers\]" "$OUT_PFAD"/*.info 2>/dev/null | wc -l)
if [ "$marker_count" -gt 0 ]; then
    info "VDR Marker gefunden in $marker_count Datei(en)"
    echo ""
fi

info "Log-Dateien: $OUT_PFAD/*.log"
echo ""

progress_close

# Frage ob TTCut gestartet werden soll
if command -v kdialog &>/dev/null; then
    if kdialog --yesno "TTCut starten?" --title "VDR Demux" 2>/dev/null; then
        info "Starte TTCut..."
        if [ -x "$TTCUT" ] || command -v "$TTCUT" &>/dev/null; then
            # Wayland-Kompatibilität
            QT_QPA_PLATFORM=xcb "$TTCUT" &
        else
            error "TTCut nicht gefunden: $TTCUT"
        fi
    fi
else
    read -p "TTCut starten? [j/N] " antwort
    if [[ "$antwort" =~ ^[jJyY] ]]; then
        QT_QPA_PLATFORM=xcb "$TTCUT" &
    fi
fi

# Log-Dateien in Editor öffnen (optional)
if command -v kdialog &>/dev/null; then
    LOG_FILES=($(find "$OUT_PFAD" -maxdepth 1 -name "*.log" -mmin -5 2>/dev/null))
    if [ ${#LOG_FILES[@]} -gt 0 ]; then
        if kdialog --yesno "Log-Dateien anzeigen? (${#LOG_FILES[@]} Datei(en))" --title "VDR Demux" 2>/dev/null; then
            kwrite "${LOG_FILES[@]}" &
        fi
    fi
fi

info "=== Fertig: $(date) ==="
