#!/bin/bash
# gate_demux_zonesync.sh — Abnahme-Gate fuer das Stoerzonen-Modell in
# ttcut-demux (build_disturbance_zones + build_zone_edits).
#
# Hintergrund (2026-08-24, Aufnahme "03x15 Die singende Saege"): Ein
# Sendeausfall erzeugt eine Audio-Gap-Zeile UND eine Video-Truncation-Zeile;
# beide erhoehten accumulated_collapse, wodurch ein Ausfall doppelt zaehlte
# und jede spaetere Reparatur ~26.4 s zu frueh landete. Der Ton lief danach
# 808 ms hinter dem Bild - ueber 82 Minuten konstant, byte-genau gemessen.
#
# Das alte gate_demux_gapsync.sh misst die Drift am DATEIENDE. Das End-Padding
# normiert genau diese Groesse, deshalb konnte es den Defekt nicht sehen.
# Dieses Gate prueft die Zonenbilanz selbst - mid-stream, ohne Material.
#
# Aufruf: tools/diag/gate_demux_zonesync.sh
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEMUX="$ROOT/tools/ttcut-demux/ttcut-demux"
WORK=/usr/local/src/CLAUDE_TMP/TTCut-ng/demux-zonesync-tests
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
{ extract_fn build_disturbance_zones
  extract_fn build_zone_edits; } > "$FN_FILE"
grep -q "^build_disturbance_zones() {" "$FN_FILE" || { echo "FAIL: Extraktion build_disturbance_zones"; exit 1; }
grep -q "^build_zone_edits() {" "$FN_FILE" || { echo "FAIL: Extraktion build_zone_edits"; exit 1; }
# shellcheck disable=SC1090
source "$FN_FILE"

VG="$WORK/vgaps.txt"; AG="$WORK/agaps.txt"; Z="$WORK/zones.txt"

# --- Check 1: 03x15 — ein Ausfall, PES-versetzt, plus Segmentnaht ----------
# Am Original-TS gemessen (2026-08-24, mit den Detektoren des Skripts):
#   Video: [5717.053156 5743.533156] 26.480 s   [5744.493156 5853.533156] 109.040 s
#   Audio: [5716.626711 5742.954711] 26.328 s   [5743.818711 5852.994711] 109.176 s
# Video-1-Ende -> Audio-2-Start = 286 ms < 2 s  ->  EINE Zone.
# Erwartung: video_lost 135.520, audio_lost 135.504, balance -16 ms -> KEIN
# Edit (unter 20 ms und unter einer MP2-Framedauer von 24 ms).
cat > "$VG" <<'EOF'
5717.053156 5743.533156 26480
5744.493156 5853.533156 109040
EOF
cat > "$AG" <<'EOF'
5716.626711 5742.954711 26328
5743.818711 5852.994711 109176
EOF
build_disturbance_zones "$VG" "$AG" 2.0 "$Z"
N_ZONES=$(grep -c . "$Z")
if [ "$N_ZONES" = "1" ]; then ok "03x15: eine Zone"
else bad "03x15: $N_ZONES Zonen erwartet 1 -> $(cat "$Z")"; fi
read -r ZS ZE VL AL < "$Z"
if awk -v v="$VL" 'BEGIN{exit !(v > 135.519 && v < 135.521)}'; then ok "03x15: video_lost=$VL"
else bad "03x15: video_lost=$VL erwartet 135.520"; fi
if awk -v a="$AL" 'BEGIN{exit !(a > 135.503 && a < 135.505)}'; then ok "03x15: audio_lost=$AL"
else bad "03x15: audio_lost=$AL erwartet 135.504"; fi
EDITS=$(build_zone_edits "$Z" 5674.693156)
if [ -z "$EDITS" ]; then ok "03x15: kein Edit (balance -16 ms unter Schwelle)"
else bad "03x15: unerwartete Edits: $EDITS"; fi

# --- Check 2: Gerber — Regression aus gate_demux_gapsync.sh ----------------
# Am Original-TS gemessen (2026-08-17). Gleicher Ausfall, PES-versetzt.
# AC3-Spur verlor 40 ms mehr als das Video -> Stille +40 ms muss bleiben.
printf "90358.602989 90359.842989 1240\n" > "$VG"
printf "90357.694989 90358.974989 1280\n" > "$AG"
build_disturbance_zones "$VG" "$AG" 2.0 "$Z"
EDITS=$(build_zone_edits "$Z" 90000.0)
if echo "$EDITS" | grep -qE " 40$"; then ok "Gerber AC3: balance +40 ms"
else bad "Gerber AC3: erwartet +40, bekam: ${EDITS:-<leer>}"; fi

# --- Check 3: Video-only-Verlust (Audio lief durch) ------------------------
# Kernaufgabe: Audio traegt Content, den das Video verlor -> volle Kuerzung.
printf "90358.602989 90359.842989 1240\n" > "$VG"
: > "$AG"
build_disturbance_zones "$VG" "$AG" 2.0 "$Z"
EDITS=$(build_zone_edits "$Z" 90000.0)
if echo "$EDITS" | grep -qE " -1240$"; then ok "video-only: balance -1240 ms"
else bad "video-only: erwartet -1240, bekam: ${EDITS:-<leer>}"; fi

# --- Check 4: Teilverlust — Audio verlor WENIGER als Video -----------------
printf "90358.602989 90359.842989 1240\n" > "$VG"
printf "90358.700000 90359.100000 400\n" > "$AG"
build_disturbance_zones "$VG" "$AG" 2.0 "$Z"
EDITS=$(build_zone_edits "$Z" 90000.0)
if echo "$EDITS" | grep -qE " -840$"; then ok "Teilverlust: balance -840 ms"
else bad "Teilverlust: erwartet -840, bekam: ${EDITS:-<leer>}"; fi

# --- Check 5: Zwei getrennte Ausfaelle bleiben getrennt --------------------
# Abstand 10 s > Toleranz 2 s -> zwei Zonen, zweiter Offset um den ersten
# Ton-Verlust verschoben.
cat > "$VG" <<'EOF'
100.000000 101.000000 1000
120.000000 121.000000 1000
EOF
cat > "$AG" <<'EOF'
100.000000 101.500000 1500
120.000000 121.500000 1500
EOF
build_disturbance_zones "$VG" "$AG" 2.0 "$Z"
if [ "$(grep -c . "$Z")" = "2" ]; then ok "getrennte Ausfaelle: zwei Zonen"
else bad "getrennte Ausfaelle: $(grep -c . "$Z") Zonen erwartet 2"; fi
# Zone 1: pos = 100 - 100 - 0 = 0 ; Zone 2: pos = 120 - 100 - 1.5 = 18.5
EDITS=$(build_zone_edits "$Z" 100.0)
if [ "$(echo "$EDITS" | awk 'NR==2 {printf "%.1f", $1}')" = "18.5" ]; then ok "getrennte Ausfaelle: Offset akkumuliert nur Ton-Verlust"
else bad "getrennte Ausfaelle: pos2=$(echo "$EDITS" | awk 'NR==2{print $1}') erwartet 18.5"; fi

echo
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
