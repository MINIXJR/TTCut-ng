#!/bin/bash
# Objektives QC-Gate für --auto-cut: schneidet ein Referenzprojekt mit zwei
# Binaries und vergleicht die Ausgaben paketweise.
#
# Der Wächter ist der Kern: TTCut-ng beendet sich im MPEG-2-`--auto-cut`-Modus
# NICHT von selbst (Fenster bleibt nach dem Schnitt stehen). Ein `timeout` als
# Absicherung ist die falsche Richtung — es wartet die volle Zeit ab und
# liefert am Ende einen Abbruch, der wie ein Ergebnis aussieht. Stattdessen:
# warten, bis die Ausgabedatei drei Messungen lang unverändert ist, dann
# gezielt beenden.
#
# Zweite Absicherung: Sollwerte. Ein abgebrochener Lauf erzeugt bei BEIDEN
# Binaries eine gleich unvollständige Datei — der Vergleich meldete dann
# fröhlich "identisch". Deshalb Dauer und Paketzahl gegen Erwartungswerte
# prüfen, bevor überhaupt verglichen wird.
#
# MKVs sind nie byte-gleich (zufällige Segment-UID, ±1 Byte zwischen zwei
# Läufen derselben Version), deshalb wird per Paket-MD5 verglichen.
#
# --- Ergänzung 2026-08-10 (Plan „cut-abort", Aufgabe 11) --------------------
# Hierher verschoben aus /usr/local/src/CLAUDE_TMP/TTCut-ng/ (Temp-Verzeichnis)
# und dabei repariert: `BIN_B` zeigte noch auf den qmake-Pfad
# /usr/local/src/TTCut-ng/ttcut-ng (existiert seit der CMake-Umstellung nicht
# mehr), und das Referenzprojekt lag in einem inzwischen gelöschten
# Datums-Verzeichnis. Das Skript war so nicht lauffähig.
#
# Neu, und der Grund für die Ergänzung: **MPEG-2-Videospuren sind nicht
# reproduzierbar.** Der MPEG-2-Neucodierer läuft mit `thread_count = 0`, libav
# wählt die Threadzahl selbst und Slice-Threading macht die Ausgabe vom
# Scheduling abhängig — in Aufgabe 5 des Plans wurden bei sechs Läufen
# **desselben unveränderten Binaries zwei verschiedene Videospuren** gemessen.
# Ein Paket-MD5-Vergleich der Videospur schlägt dort also zufällig fehl und
# beweist nichts. Das Skript erkennt den Codec deshalb selbst und vergleicht
# bei mpeg2video nur Paketzahl, Dauer und die Audiospur (beide über alle
# gemessenen Läufe stabil); bei H.264/H.265 wird die Videospur weiterhin
# paketweise verglichen, dort ist sie byte-gleich.
#
# Aufruf:  qc-autocut.sh <BIN_A> [BIN_B] [projekt.ttcut]
#   BIN_A  Referenz-Binary (z. B. ein Build eines älteren Commits) — Pflicht.
#   BIN_B  Vergleichs-Binary, Vorgabe: <repo>/build/ttcut-ng.
#   projekt.ttcut  eigenes Projekt; dann müssen die Sollwerte über die
#          Umgebungsvariablen EXPECT_DURATION/EXPECT_VPACKETS/EXPECT_APACKETS
#          mitgegeben werden (ohne Sollwerte wird nicht verglichen — genau das
#          ist die zweite Absicherung oben). Ohne Argument wird ein
#          MPEG-2-Referenzprojekt auf dem Testkorpus erzeugt.
#   QC_WORKDIR  Arbeitsverzeichnis für Projekt und Ausgaben.

set -u

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
QC="${QC_WORKDIR:-/usr/local/src/CLAUDE_TMP/TTCut-ng/qc-autocut}"
mkdir -p "$QC" || exit 2

if [ $# -lt 1 ]; then
    echo "Aufruf: $(basename "$0") <BIN_A> [BIN_B] [projekt.ttcut]" >&2
    exit 2
fi
BIN_A=$(readlink -f "$1")
BIN_B=$(readlink -f "${2:-$ROOT/build/ttcut-ng}")
PROJECT="${3:-}"

for b in "$BIN_A" "$BIN_B"; do
    [ -x "$b" ] || { echo "kein ausführbares Binary: $b" >&2; exit 2; }
done

if [ -n "$PROJECT" ]; then
    PROJECT=$(readlink -f "$PROJECT")
    [ -f "$PROJECT" ] || { echo "kein solches Projekt: $PROJECT" >&2; exit 2; }
    if [ -z "${EXPECT_DURATION:-}" ] || [ -z "${EXPECT_VPACKETS:-}" ] || [ -z "${EXPECT_APACKETS:-}" ]; then
        echo "FEHLER: eigenes Projekt ohne Sollwerte. Bitte EXPECT_DURATION," >&2
        echo "        EXPECT_VPACKETS und EXPECT_APACKETS setzen — ohne sie ist" >&2
        echo "        der A/B-Vergleich wertlos (siehe Kopfkommentar)." >&2
        exit 2
    fi
else
    # Referenzprojekt auf dem Testkorpus erzeugen (tools/test-videos/cache wird
    # von tools/test-videos/make_test_video.sh gebaut). Die drei Schnitte sind
    # dieselben wie in tools/diag/test_mpeg2cut_abort.cpp, damit Harness und
    # Gate dieselben Sollwerte haben: 800+800+500 = 2100 Bilder à 25 fps.
    C="$ROOT/tools/test-videos/cache"
    V="$C/tux_mpeg2_576i_pal_test.m2v"
    A="$C/tux_mpeg2_576i_pal_test.mp2"
    for f in "$V" "$A"; do
        [ -f "$f" ] || { echo "Testkorpus fehlt: $f (tools/test-videos/make_test_video.sh)" >&2; exit 2; }
    done
    PROJECT="$QC/qc_ref_mpeg2.ttcut"
    {
        echo '<!DOCTYPE TTCut-Projectfile>'
        echo '<TTCut-Projectfile>'
        echo ' <Version>1.0</Version>'
        echo ' <Video>'
        echo '  <Order>0</Order>'
        echo "  <Name>$V</Name>"
        echo '  <Audio>'
        echo '   <Order>0</Order>'
        echo "   <Name>$A</Name>"
        echo '   <Language>deu</Language>'
        echo '  </Audio>'
        i=0
        for range in "100:899" "1300:2099" "2300:2799"; do
            echo '  <Cut>'
            echo "   <Order>$i</Order>"
            echo "   <CutIn>${range%%:*}</CutIn>"
            echo "   <CutOut>${range##*:}</CutOut>"
            echo '  </Cut>'
            i=$((i + 1))
        done
        echo ' </Video>'
        echo '</TTCut-Projectfile>'
    } > "$PROJECT"
    # Sollwerte des Referenzprojekts: 2100 Bilder à 25 fps = 84,000 s,
    # MP2 mit 1152 Samples à 48 kHz = 24 ms je Paket -> 3500 Audiopakete.
    EXPECT_DURATION="${EXPECT_DURATION:-84.000000}"
    EXPECT_VPACKETS="${EXPECT_VPACKETS:-2100}"
    EXPECT_APACKETS="${EXPECT_APACKETS:-3500}"
fi

STABLE_ROUNDS=3          # so viele gleiche Messungen = fertig
POLL=2                   # Sekunden zwischen den Messungen
MAX_WAIT=900             # Notbremse, falls gar nichts passiert

run_cut() {              # $1 = Binary, $2 = Ausgabename → 0 = ok
    local bin="$1" out="$2" pid last=-1 same=0 waited=0 size
    rm -f "$out"
    QT_QPA_PLATFORM=offscreen "$bin" --project "$PROJECT" --auto-cut "$out" >/dev/null 2>&1 &
    pid=$!

    while kill -0 "$pid" 2>/dev/null; do
        sleep "$POLL"; waited=$((waited + POLL))
        size=$(stat -c%s "$out" 2>/dev/null || echo 0)
        if [ "$size" -gt 0 ] && [ "$size" = "$last" ]; then
            same=$((same + 1))
            if [ "$same" -ge "$STABLE_ROUNDS" ]; then
                echo "    Ausgabe stabil bei $size B nach ${waited}s — beende Prozess"
                kill "$pid" 2>/dev/null
                for _ in $(seq 1 10); do kill -0 "$pid" 2>/dev/null || break; sleep 0.5; done
                kill -9 "$pid" 2>/dev/null
                wait "$pid" 2>/dev/null
                return 0
            fi
        else
            same=0
        fi
        last="$size"
        if [ "$waited" -ge "$MAX_WAIT" ]; then
            echo "    ABBRUCH: nach ${MAX_WAIT}s keine stabile Ausgabe"
            kill -9 "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
            return 1
        fi
    done
    # Prozess hat sich doch selbst beendet (z. B. anderer Codec-Pfad)
    echo "    Prozess endete selbst nach ${waited}s"
    return 0
}

check_sollwerte() {      # $1 = Datei → 0 = plausibel
    local f="$1" dur vp ap
    dur=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f" 2>/dev/null)
    vp=$(ffprobe -v error -select_streams v -show_packets -of csv=p=0 "$f" 2>/dev/null | wc -l)
    ap=$(ffprobe -v error -select_streams a -show_packets -of csv=p=0 "$f" 2>/dev/null | wc -l)
    printf '    Dauer %s s (soll %s), Video %s (soll %s), Audio %s (soll %s)\n' \
           "$dur" "$EXPECT_DURATION" "$vp" "$EXPECT_VPACKETS" "$ap" "$EXPECT_APACKETS"
    [ "$vp" = "$EXPECT_VPACKETS" ] && [ "$ap" = "$EXPECT_APACKETS" ] \
        && python3 -c "import sys; sys.exit(0 if abs($dur - $EXPECT_DURATION) < 0.05 else 1)"
}

echo "=== Projekt: $PROJECT ==="
echo "=== A: $BIN_A ==="
run_cut "$BIN_A" "$QC/gate_a.mkv" || exit 1
check_sollwerte "$QC/gate_a.mkv" || { echo "    FEHLER: Sollwerte verfehlt — Lauf war unvollständig"; exit 1; }

echo "=== B: $BIN_B ==="
run_cut "$BIN_B" "$QC/gate_b.mkv" || exit 1
check_sollwerte "$QC/gate_b.mkv" || { echo "    FEHLER: Sollwerte verfehlt — Lauf war unvollständig"; exit 1; }

# Codec der Videospur bestimmt, ob die Videospur überhaupt vergleichbar ist.
# -of csv=p=0 hängt ein Trennzeichen an ("mpeg2video,"), deshalb abschneiden.
VCODEC=$(ffprobe -v error -select_streams v:0 -show_entries stream=codec_name -of csv=p=0 "$QC/gate_a.mkv" 2>/dev/null)
VCODEC="${VCODEC%%,*}"
STREAMS="v a"
if [ "$VCODEC" = "mpeg2video" ]; then
    STREAMS="a"
    echo "=== Videospur ($VCODEC) wird NICHT paketweise verglichen ==="
    echo "    Grund: der MPEG-2-Neucodierer läuft mit thread_count = 0 und ist"
    echo "    nicht reproduzierbar (gemessen: zwei verschiedene Ausgaben in sechs"
    echo "    Läufen desselben Binaries). Paketzahl und Dauer sind oben geprüft."
fi

echo "=== Paketweiser Vergleich ==="
for s in $STREAMS; do
    ffprobe -v error -select_streams $s -show_packets -show_data_hash md5 "$QC/gate_a.mkv" 2>/dev/null | grep '^data_hash' > "$QC/gate_a_$s.txt"
    ffprobe -v error -select_streams $s -show_packets -show_data_hash md5 "$QC/gate_b.mkv" 2>/dev/null | grep '^data_hash' > "$QC/gate_b_$s.txt"
    if [ ! -s "$QC/gate_a_$s.txt" ]; then
        echo "    $s: FEHLER — ffprobe liefert kein data_hash-Feld (Oracle prüfen)"; exit 1
    fi
    if diff -q "$QC/gate_a_$s.txt" "$QC/gate_b_$s.txt" >/dev/null; then
        echo "    $s: BIT-IDENTISCH ($(wc -l < "$QC/gate_a_$s.txt") Pakete)"
    else
        echo "    $s: ABWEICHUNG"; diff "$QC/gate_a_$s.txt" "$QC/gate_b_$s.txt" | head -5; exit 1
    fi
done
echo "GATE BESTANDEN"
