#!/usr/bin/env bash
# Gate: H.264 open-GOP cold-start leading-picture handling in TTDisplayOrderMap
# (spec 2026-07-05-h264-coldstart-leading-pics-design.md, v0.72.1).
#
# For each stream, test_h264_leading builds the production map
# (TTDisplayOrderMap::buildFromFile) and asserts it against decoder ground truth
# (thread_count=1, pts=AU index): the cold-start drop count, displayToDecode(0)
# == the decoder's first output AU, and prefix display->decode alignment.
#
# The whole regression lives at the cold start, so short (~15 s) head slices of
# each corpus stream exercise it fully and decode fast. Pass raw ES paths; each
# arg may carry an expected drop count as "path:drops" (asserted exactly).
#
# Requires: a built tools/diag/test_h264_leading.
#
# Usage:
#   gate_h264_leading.sh <test_h264_leading-binary> <es[:drops]> [es[:drops] ...]
#
# Reference corpus expectations (head slices):
#   open-GOP H.264 (ZDF-neo)     : 3 drops
#   720p50 progressive (Das Erste): 7 drops
#   MBAFF H.264 (ServusTV)       : 0 drops (no false positive)
#   Tux progressive (IDR start)  : 0 drops (no false positive)
#   HEVC RASL (Designermode)     : unchanged (classifier handles it; markH264 no-op)
set -euo pipefail
BIN="${1:?usage: gate_h264_leading.sh <test_h264_leading-binary> <es[:drops]> ...}"; shift
[ $# -ge 1 ] || { echo "need at least one ES path"; exit 2; }

rc=0
for spec in "$@"; do
  es="${spec%%:*}"
  drops=""
  [ "$spec" != "$es" ] && drops="${spec##*:}"
  if [ ! -f "$es" ]; then echo "SKIP (missing) $es"; continue; fi
  echo "---- $es ${drops:+(expect $drops drops)}"
  # Run once; capture status without tripping `set -e`, then show the verdicts.
  out="$("$BIN" "$es" ${drops:+$drops} 2>/dev/null)" && tool_rc=0 || tool_rc=$?
  printf '%s\n' "$out" | grep -E "^(PASS|FAIL|INFO|RESULT)" || true
  [ "$tool_rc" -ne 0 ] && rc=1
done

echo
[ $rc -eq 0 ] && echo "GATE: PASS" || echo "GATE: FAIL"
exit $rc
