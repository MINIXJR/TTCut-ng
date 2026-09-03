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
#   2. Demux with ttcut-demux -n (named output)
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

# Run start time — reference point for the "created by this run" filters
# (find -newermt) at the end of the script. A fixed point instead of
# -mmin -5, because a run over several recordings easily exceeds five
# minutes and the early logs would drop out of the listing and the popup.
SCRIPT_START=$(date +%s)

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
    s="${s//#2E/.}"
    s="${s//#23/#}"
    s="${s//\//_}"
    printf '%s' "$s"
}

# Logdatei einfärben statt sie farbig zu erzeugen.
#
# ttcut-demux schaltet seine eigenen Farben ab, sobald stdout kein Terminal
# ist (`[ -t 1 ]`) — und hier IST stdout die Logdatei. Das spätere `cat` gibt
# darum reinen Text aus, auch wenn wir selbst am Terminal sitzen. Die
# Auszeichnung deshalb erst beim Ausgeben nachziehen: die Logdatei bleibt
# frei von Escape-Sequenzen und damit in einem Editor lesbar.
#
# Reihenfolge der Ausdrücke zählt — die Schadenszeile soll rot werden, nicht
# gelb wie eine gewöhnliche Warnung; nach der ersten Ersetzung beginnt die
# Zeile mit ESC, sodass der allgemeine WARN-Ausdruck nicht mehr greift.
colorize_log() {
    if [ -z "$RED" ]; then
        cat "$1"
        return
    fi
    local r y n
    r=$(printf '%b' "$RED"); y=$(printf '%b' "$YELLOW"); n=$(printf '%b' "$NC")
    sed -E -e "s/^\\[WARN\\](.*SEVERELY DAMAGED.*)$/${r}[WARN]\\1${n}/" \
           -e "s/^\\[ERROR\\]/${r}[ERROR]${n}/" \
           -e "s/^\\[WARN\\]/${y}[WARN]${n}/" "$1"
}

# ---- kdialog Fortschritts-Popup ----
# kdialog --progressbar liefert "service objectpath" mit Leerzeichen — daher
# beim Setzen aufsplitten und einzeln gequotet an qdbus weiterreichen.
PROGRESS_SERVICE=""
PROGRESS_PATH=""

progress_start() {
    local title="$1"
    local total="$2"
    PROGRESS_SERVICE=""
    PROGRESS_PATH=""
    if command -v kdialog &>/dev/null; then
        local handle
        handle=$(kdialog --progressbar "$title" "$total" --title "VDR Demux" 2>/dev/null) || handle=""
        if [ -n "$handle" ]; then
            read -r PROGRESS_SERVICE PROGRESS_PATH <<< "$handle"
            qdbus "$PROGRESS_SERVICE" "$PROGRESS_PATH" setAutoClose true 2>/dev/null || true
        fi
    fi
}

progress_update() {
    local value="$1"
    local label="$2"
    if [ -n "$PROGRESS_SERVICE" ]; then
        qdbus "$PROGRESS_SERVICE" "$PROGRESS_PATH" setLabelText "$label" 2>/dev/null || true
        qdbus "$PROGRESS_SERVICE" "$PROGRESS_PATH" Set "" value "$value" 2>/dev/null || true
    fi
}

progress_close() {
    if [ -n "$PROGRESS_SERVICE" ]; then
        qdbus "$PROGRESS_SERVICE" "$PROGRESS_PATH" close 2>/dev/null || true
        PROGRESS_SERVICE=""
        PROGRESS_PATH=""
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
    rel_path="${dir#"$IN_PFAD"/}"
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

# Fortschritt: 100 Punkte je Aufnahme, dazu 2+2 für Aufräumen und Fertig.
# Die beiden Nachlaufschritte dauern zusammen unter einer Sekunde und dürfen
# nicht so viel Balken bekommen wie ein ganzer Demux-Durchgang.
TOTAL_STEPS=$((${#SELECTED_DIRS[@]} * 100 + 4))
CURRENT_STEP=0
progress_start "Vorbereitung..." "$TOTAL_STEPS"

#############################################################################
# SCHRITT 1: TS-Dateien finden und mit ttcut-demux demuxen
#############################################################################
step "Schritt 1: Demuxen mit ttcut-demux..."

# Aufnahmen, die ttcut-demux als schwer beschädigt gemeldet hat.
DAMAGED_LIST=()
# Aufnahmen mit Materialverlust: das Bild springt an der Störstelle, der Ton
# bleibt synchron. Eigene Liste neben DAMAGED_LIST, weil es ein anderer Befund
# ist — beide können auf dieselbe Aufnahme zutreffen.
LOSS_LIST=()
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
TOTAL_STEPS=$((${#TS_FILES[@]} * 100 + 4))
progress_close
progress_start "Demuxe ${#TS_FILES[@]} Aufnahme(n)..." "$TOTAL_STEPS"
progress_update $((CURRENT_STEP * 100)) "Demuxe ${#TS_FILES[@]} Aufnahme(n)..."

# Verarbeite gesammelte Dateien
for ts_datei in "${TS_FILES[@]}"; do
    rec_dir=$(dirname "$ts_datei")
    # VDR directory structure: .../Series/Episode/Date.Time.rec/00001.ts
    # Use the directory directly above .rec as the show/episode name
    show_name="$(vdr_unmask "$(basename "$(dirname "$rec_dir")")")"

    info "Demuxe: $show_name ($(basename "$ts_datei"))"
    progress_update $((CURRENT_STEP * 100)) "Demuxe: $show_name"$'\n'"(${DEMUX_COUNT}/${#TS_FILES[@]})"

    LOG_FILE="$OUT_PFAD/${show_name}.log"

    # Prüfe ob marks Datei vorhanden ist (VDR markad)
    marks_info=""
    if [ -f "$rec_dir/marks" ]; then
        marks_info=" (mit VDR markers)"
    fi

    # ttcut-demux handles multi-file detection and concat automatically.
    #
    # Run in the foreground here for clarity. The author's own wrapper puts it
    # in its own process group and polls instead, which lets it read the
    # ".<name>.progress" file ttcut-demux writes (a 0..100 for the recording)
    # and drive a real progress bar off it - not reproducible without that
    # poll loop, so this example simply waits.
    if "$TTCUT_DEMUX" -n "$show_name" "$ts_datei" "$OUT_PFAD" > "$LOG_FILE" 2>&1; then
        colorize_log "$LOG_FILE"
        info "  OK: $show_name$marks_info"
        DEMUX_COUNT=$((DEMUX_COUNT + 1))
    else
        colorize_log "$LOG_FILE"
        error "  FEHLER bei: $show_name"
        DEMUX_ERRORS=$((DEMUX_ERRORS + 1))
    fi

    # Schadensurteil von ttcut-demux einsammeln, damit der Abschlussdialog
    # sagen kann, WELCHE Aufnahme nichts taugt — im Log geht die Zeile
    # zwischen Tausenden Defektmeldungen unter.
    # "|| true" ist Pflicht: dieses Skript laeuft unter set -e, und eine
    # Zuweisung aus einem fehlgeschlagenen Kommando beendet es sofort. grep
    # liefert 1, wenn es NICHTS findet - also bei jeder unbeschaedigten
    # Aufnahme.
    DAMAGE_LINE=$(grep -m1 'SEVERELY DAMAGED RECORDING' "$LOG_FILE" 2>/dev/null || true)
    if [ -n "$DAMAGE_LINE" ]; then
        DMG_PCT=$(sed -E 's/.*RECORDING: ([0-9.]+)% .*/\1/' <<<"$DAMAGE_LINE")
        DMG_GPM=$(sed -E 's|.*, ([0-9.]+) audio gaps/min.*|\1|' <<<"$DAMAGE_LINE")
        DAMAGED_LIST+=("$show_name — ${DMG_PCT/./,} % Bilder fehlen, ${DMG_GPM/./,} Tonlücken/min je Spur")
    fi

    if grep -q "^\[WARN\] Material loss:" "$LOG_FILE" 2>/dev/null; then
        LOSS_LIST+=("$show_name: $(grep -m1 -oP '^\[WARN\] Material loss: \K[^-]+' "$LOG_FILE" | sed 's/ *$//')")
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
progress_update $((${#TS_FILES[@]} * 100 + 2)) "Aufräumen..."

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

#############################################################################
# SCHRITT 3: Zusammenfassung und TTCut starten
#############################################################################
step "Schritt 3: Zusammenfassung"
progress_update $((${#TS_FILES[@]} * 100 + 4)) "Fertig — $DEMUX_COUNT Aufnahme(n) demuxed"
echo ""

info "=== Demux abgeschlossen ==="
echo ""
echo "Ausgabeverzeichnis: $OUT_PFAD"
echo ""

# Zeige erstellte Dateien
if [ -d "$OUT_PFAD" ]; then
    echo "Erstellte Dateien:"
    find "$OUT_PFAD" -maxdepth 1 -type f \( -name "*.264" -o -name "*.265" -o -name "*.m2v" -o -name "*.info" -o -name "*.log" \) \
        -newermt "@$SCRIPT_START" -exec ls -lh {} \; 2>/dev/null | head -20
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

# Abschlussfrage. Die Schadensliste steht im Kopftext derselben Frage statt
# in einem eigenen Popup — sonst muss man bei einem unbeaufsichtigten Lauf
# zwei Fenster wegklicken, um zum selben Ergebnis zu kommen.
FINAL_TEXT=""
if [ ${#DAMAGED_LIST[@]} -gt 0 ]; then
    FINAL_TEXT="${#DAMAGED_LIST[@]} Aufnahme(n) schwer beschädigt:"
    for entry in "${DAMAGED_LIST[@]}"; do
        FINAL_TEXT="$FINAL_TEXT
   $entry"
    done
    FINAL_TEXT="$FINAL_TEXT

Das Ergebnis ist voraussichtlich unbrauchbar.

"
fi
if [ ${#LOSS_LIST[@]} -gt 0 ]; then
    FINAL_TEXT="${FINAL_TEXT}${#LOSS_LIST[@]} Aufnahme(n) mit Materialverlust:"
    for entry in "${LOSS_LIST[@]}"; do
        FINAL_TEXT="$FINAL_TEXT
   $entry"
    done
    FINAL_TEXT="$FINAL_TEXT

Das Bild springt an diesen Stellen. Der Ton bleibt synchron.

"
fi
FINAL_TEXT="${FINAL_TEXT}TTCut starten?"

if command -v kdialog &>/dev/null; then
    if kdialog --yesno "$FINAL_TEXT" --title "VDR Demux" 2>/dev/null; then
        info "Starte TTCut..."
        if [ -x "$TTCUT" ] || command -v "$TTCUT" &>/dev/null; then
            "$TTCUT" &
        else
            error "TTCut nicht gefunden: $TTCUT"
        fi
    fi
else
    { [ ${#DAMAGED_LIST[@]} -gt 0 ] || [ ${#LOSS_LIST[@]} -gt 0 ]; } && { echo ""; echo "$FINAL_TEXT"; }
    read -r -p "TTCut starten? [j/N] " antwort
    if [[ "$antwort" =~ ^[jJyY] ]]; then
        "$TTCUT" &
    fi
fi

# Log-Dateien in Editor öffnen (optional)
if command -v kdialog &>/dev/null; then
    # Use mapfile + -print0 to handle filenames with spaces/glob chars safely.
    mapfile -d '' -t LOG_FILES < <(find "$OUT_PFAD" -maxdepth 1 -name "*.log" -newermt "@$SCRIPT_START" -print0 2>/dev/null | sort -z)
    if [ ${#LOG_FILES[@]} -gt 0 ]; then
        if kdialog --yesno "Log-Dateien anzeigen? (${#LOG_FILES[@]} Datei(en))" --title "VDR Demux" 2>/dev/null; then
            kwrite "${LOG_FILES[@]}" &
        fi
    fi
fi

info "=== Fertig: $(date) ==="
