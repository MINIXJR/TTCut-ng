#!/bin/bash
# gate_anomaly_real.sh - the audio anomaly scan on a real recording.
#
# Runs both searches of TTAudioAnomalyScanTask on a demuxed AC3 file and
# compares with what was established by ear on 2026-10-04
# (docs/completed-work.md, "Tonanomalie-Scan: LFE-Grenze und Abbruch-Suche").
#
#   usage: gate_anomaly_real.sh <file.ac3> <lfe ranges> <stop seconds>
#     lfe ranges    comma-separated "from-to" AC3 frames, or "none"
#     stop seconds  comma-separated start seconds, or "none"; a reported stop
#                   matches when it lies within 0.05 s
#   example (02x06, demuxed with ttcut-demux):
#     gate_anomaly_real.sh 02x06_deu.ac3 63894-63901 969.525,3099.730,3468.170
#
# Not in the run-gates.sh table: it needs a recording.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="$ROOT/tools/diag/test_anomalyscan"
WORK=/usr/local/src/CLAUDE_TMP/TTCut-ng/anomaly-real
[ $# -eq 3 ] || { sed -n '2,15p' "$0"; exit 2; }
[ -x "$BIN" ] || { echo "FAIL: $BIN missing - cmake --build build --target test_anomalyscan"; exit 1; }
[ -f "$1" ] || { echo "FAIL: no such file: $1"; exit 1; }
mkdir -p "$WORK"
OUT=$(XDG_CACHE_HOME="$WORK/xdg" "$BIN" "$1" 0 25 2>/dev/null)
RC=0
GOT_LFE=$(echo "$OUT" | awk '$1=="LFE"{printf "%s%s-%s", (n++?",":""), $2, $3} END{if(!n) printf "none"}')
if [ "$GOT_LFE" = "$2" ]; then echo "PASS: LFE findings $GOT_LFE"; else echo "FAIL: LFE findings $GOT_LFE, expected $2"; RC=1; fi
GOT_STOPS=$(echo "$OUT" | awk '$1=="STOP"{printf "%s%s", (n++?",":""), $2} END{if(!n) printf "none"}')
MATCH=$(awk -v got="$GOT_STOPS" -v want="$3" 'BEGIN {
  ng = (got == "none") ? 0 : split(got, g, ","); nw = (want == "none") ? 0 : split(want, w, ",")
  if (ng != nw) { print "count"; exit }
  for (i = 1; i <= nw; i++) { ok = 0; for (j = 1; j <= ng; j++) if (g[j] - w[i] < 0.05 && w[i] - g[j] < 0.05) ok = 1; if (!ok) { print "miss " w[i]; exit } }
  print "ok" }')
if [ "$MATCH" = "ok" ]; then echo "PASS: stops $GOT_STOPS"; else echo "FAIL: stops $GOT_STOPS, expected $3 ($MATCH)"; RC=1; fi
echo "$OUT" | grep '^STOP' | sed 's/^/  /'
exit $RC
