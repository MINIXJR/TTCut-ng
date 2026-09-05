#!/bin/bash
# gate_refactor_identity.sh — output-identity gates for refactors, one script,
# several suites. Each suite captures everything a byte-for-byte compare of two
# builds needs into WORK/<suite>-<tag>; `compare` diffs two tags. Established
# for the code-audit batches E/F on 2026-09-05 (they lived in CLAUDE_TMP first)
# and moved here so they survive a temp purge.
#
#   tools/diag/gate_refactor_identity.sh <suite> run <tag>
#   tools/diag/gate_refactor_identity.sh <suite> compare <tagA> <tagB>
#
# Suites:
#   demux    tools/ttcut-demux on four fixtures (MPEG-2, H.264 and HEVC TS
#            made from the Tux cache, plus the two-segment VDR .rec): ES
#            output byte-identical, .info and logs identical after
#            normalisation. Uses the tree's ttcut-ac3fix/ttcut-audiofix.
#   pts      tools/ttcut-pts-analyze -v and quiet on the same fixtures.
#   harness  the audio/cut/abort harness matrix (test_anomalyscan,
#            test_audiorepair_cut, the *_abort harnesses): PASS lines and
#            exit codes; .err compared sorted, timing counters normalised.
#   cutter   tools/diag/test_audiocutter_paths on gate_ac3fix.sh's mixed.ac3
#            (five paths incl. the acmod-switch case): output files by MD5.
#   mpv      tools/diag/test_mpv_loadfile_args on a subtitle path carrying a
#            comma, a space, an umlaut, a quote and a bracket.
#   mtv      tools/test-videos/make_test_video.sh in a PRIVATE cache copy
#            (skip path for h264/fieldpic, encode path for mpeg2): logs and
#            project files. Never points the script at the real cache — that
#            directory is a symlink and `cp -a` would copy the link (2026-09-05).
#
# Build the harnesses first (`cmake --build build --target diag`); the binary
# under test is whatever the tree holds at `run` time, so capture `ref`
# before the change and `cand` after it.
#
# Compare rules: regular files byte for byte; *.log/*.info/*.out/*.err/*.txt/
# *.ttcut after normalisation (paths, "N ms", 0x addresses, UUIDs, clock
# times, the abort matrix's timing-dependent counters, mpv log lines of the
# nested MPEG-2 CutTask that race the process exit); *.err additionally
# sorted, because worker threads interleave their lines.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
C="$ROOT/tools/test-videos/cache"
WORK=/usr/local/src/CLAUDE_TMP/TTCut-ng/refactor-identity
FIX="$WORK/fixtures"
MIXED=/usr/local/src/CLAUDE_TMP/TTCut-ng/ac3fix-gate/fixtures/mixed.ac3
export PATH="$ROOT/tools/ttcut-ac3fix:$ROOT/tools/ttcut-audiofix:$PATH"
export QT_QPA_PLATFORM=offscreen
suite=${1:-}; mode=${2:-}
usage() { sed -n '2,36p' "$0"; exit 1; }
case "$suite" in demux|pts|harness|cutter|mpv|mtv) ;; *) usage;; esac
case "$mode" in run) tag=${3:-}; [ -n "$tag" ] || usage;; compare) a=${3:-}; b=${4:-}; [ -n "$a" ] && [ -n "$b" ] || usage;; *) usage;; esac
mkdir -p "$WORK"

norm() {   # norm <dir-to-mask>
  sed -E -e "s|$1|<OUT>|g" -e 's/[0-9]+ ?ms\b/N ms/g' -e 's/0x[0-9a-fA-F]+/0xX/g' \
      -e 's/[0-9]+\.[0-9]+ ?s\b/N s/g' -e '/^\[INFO\] +-rw/d' -e '/^# Generated:/d' \
      -e 's/\{[0-9a-f-]{36}\}/{UUID}/g' -e 's/\[[0-9]{2}:[0-9]{2}:[0-9]{2}\]/[T]/g' \
      -e 's/events=[0-9]+/events=N/g' -e 's/max [0-9]+%/max N%/g' \
      -e 's/(armedAtEvent|cancelDeliveredAtEvent)=[0-9]+/\1=N/g' -e 's/remaining tasks +[0-9]+/remaining tasks N/g' \
      -e '/^(finished|aborted) +"CutTask"/d' -e '/^QUuid\("\{UUID\}"\) +(total steps|finished) +[0-9]+$/d' \
      -e '/took|elapsed|Duration:|real|user|sys/d'
}
cmpdir() {   # cmpdir <a> <b>
  local a=$1 b=$2 rc=0 f rel na="$WORK/.n_a" nb="$WORK/.n_b"
  while IFS= read -r -d '' f; do rel=${f#$a/}
    case "$rel" in
      *.log|*.info|*.ttcut|*.txt|*.out|*.err)
        [ -f "$b/$rel" ] || { echo "MISSING in $b: $rel"; rc=1; continue; }
        norm "$a" < "$f" > "$na"; norm "$b" < "$b/$rel" > "$nb"
        case "$rel" in *.err) sort -o "$na" "$na"; sort -o "$nb" "$nb";; esac
        if diff -q "$na" "$nb" >/dev/null; then echo "same(norm) $rel"; else echo "DIFF(norm) $rel"; diff "$na" "$nb" | head -20; rc=1; fi ;;
      *) if [ -f "$b/$rel" ]; then if cmp -s "$f" "$b/$rel"; then echo "identical $rel"; else echo "DIFF(bytes) $rel"; rc=1; fi
         else echo "MISSING in $b: $rel"; rc=1; fi ;;
    esac
  done < <(find "$a" -type f ! -name '.*' -print0 | sort -z)
  while IFS= read -r -d '' f; do rel=${f#$b/}; [ -e "$a/$rel" ] || { echo "EXTRA in $b: $rel"; rc=1; }; done < <(find "$b" -type f ! -name '.*' -print0)
  rm -f "$na" "$nb"; return $rc
}

# Fixtures are generated once and reused, so every tag sees identical inputs.
# Delete WORK/fixtures to regenerate — never between `run ref` and `run cand`.
make_fixtures() {
  mkdir -p "$FIX"
  # 30 s of the PAL Tux stream as one TS: -fflags +genpts gives the raw ES the
  # timestamps the mpegts muxer insists on (same trick as make_test_video.sh).
  [ -s "$FIX/tux_mpeg2.ts" ] || ffmpeg -y -v error -fflags +genpts -i "$C/tux_mpeg2_576i_pal_test.m2v" -i "$C/tux_mpeg2_576i_pal_test.mp2" \
      -map 0:v -map 1:a -c copy -t 30 -f mpegts "$FIX/tux_mpeg2.ts" || return 1
  # The H.264/HEVC fixtures are transcodes of that TS: a raw .264/.265 from the
  # cache cannot be remuxed into a TS (no timestamps), and the demux gate only
  # needs a valid TS per codec, not a particular encode.
  [ -s "$FIX/tux_h264.ts" ] || ffmpeg -y -v error -i "$FIX/tux_mpeg2.ts" -map 0:v -map 0:a -c:v libx264 -preset ultrafast -g 25 -bf 2 \
      -c:a ac3 -b:a 192k -metadata:s:a:0 language=deu -f mpegts "$FIX/tux_h264.ts" || return 1
  [ -s "$FIX/tux_hevc.ts" ] || ffmpeg -y -v error -i "$FIX/tux_mpeg2.ts" -map 0:v -map 0:a -c:v libx265 -preset ultrafast -x265-params log-level=error \
      -c:a ac3 -b:a 192k -metadata:s:a:0 language=deu -f mpegts "$FIX/tux_hevc.ts" || return 1
  # Subtitle path with every character that has bitten a comma-separated or
  # quoted option list: comma, space, umlaut, double quote, bracket.
  local awk="$FIX/kömma, tést"; mkdir -p "$awk"
  [ -s "$awk/a,b\"c]d_deu.srt" ] || cp "$C/tux_h264_1080p_progressive_test.srt" "$awk/a,b\"c]d_deu.srt" || return 1
  # gate_ac3fix.sh owns mixed.ac3 (stereo + 5.1 + stereo + junk) and generates
  # it on its first run; the cutter suite borrows it.
  [ -s "$MIXED" ] || { echo "FAIL: $MIXED missing - run tools/diag/gate_ac3fix.sh run ref once"; return 1; }
}
REC="$C/tux_mpeg2_576i_multifile_test.rec"
r() {   # r <out> <name> <cmd...>  -> <out>/<name>.out/.err with rc appended
  local out=$1 name=$2; shift 2
  "$@" > "$out/$name.out" 2> "$out/$name.err"; echo "rc=$?" >> "$out/$name.out"; echo "$name $(tail -n1 "$out/$name.out")"
}

run_demux() {
  local out=$1 ts n
  for ts in "$FIX/tux_mpeg2.ts" "$FIX/tux_h264.ts" "$FIX/tux_hevc.ts"; do n=$(basename "$ts" .ts); mkdir -p "$out/$n"
    "$ROOT/tools/ttcut-demux/ttcut-demux" -e "$ts" "$out/$n" > "$out/$n.log" 2>&1; echo "rc=$?" >> "$out/$n.log"; done
  mkdir -p "$out/multi"; "$ROOT/tools/ttcut-demux/ttcut-demux" -e -n multi "$REC/00001.ts" "$out/multi" > "$out/multi.log" 2>&1; echo "rc=$?" >> "$out/multi.log"
  grep -h "^rc=" "$out"/*.log | tr '\n' ' '; echo
}
run_pts() {
  local out=$1 P="$ROOT/tools/ttcut-pts-analyze/ttcut-pts-analyze" ts n
  for ts in "$FIX/tux_mpeg2.ts" "$FIX/tux_h264.ts" "$FIX/tux_hevc.ts" "$REC/00001.ts"; do n=$(basename "$(dirname "$ts")")_$(basename "$ts" .ts)
    "$P" -v "$ts" > "$out/$n.out" 2> "$out/$n.err"; echo "rc=$?" >> "$out/$n.out"
    "$P" "$ts" > "$out/$n.quiet.out" 2>&1; echo "rc=$?" >> "$out/$n.quiet.out"; done
  "$P" --help > "$out/help.out" 2>&1; "$P" > "$out/noarg.out" 2>&1; echo "rc=$?" >> "$out/noarg.out"
}
run_harness() {
  local out=$1 D="$ROOT/tools/diag" p
  local W="$out/work"
  local V264="$C/tux_h264_1080p_progressive_test.264" A264="$C/tux_h264_1080p_progressive_test.ac3"
  local M2V="$C/tux_mpeg2_576i_pal_test.m2v" MP2="$C/tux_mpeg2_576i_pal_test.mp2"
  mkdir -p "$W"/{ao_none,ao_audio,ao_mux,h26x_none,h26x_video,h26x_audio,h26x_mux,m2_none,m2_audio,m2_video,m2_mux,seq,pv_none,pv_video,pv_audio,pv_fail,stale} /usr/local/src/CLAUDE_TMP/TTCut-ng/cut-abort
  r "$out" anomalyscan "$D/test_anomalyscan"
  r "$out" audiorepair_cut "$D/test_audiorepair_cut"
  r "$out" audiocut_abort "$D/test_audiocut_abort" "$A264"
  for p in none audio mux; do r "$out" audioonlycut_$p "$D/test_audioonlycut_abort" "$V264" "$A264" "$W/ao_$p" $p; done
  for p in none video audio mux; do r "$out" h26xcut_$p "$D/test_h26xcut_abort" "$V264" "$A264" "$W/h26x_$p" $p; done
  r "$out" mkvmux_abort "$D/test_mkvmux_abort" "$V264" "$A264" 50
  for p in none audio video mux; do r "$out" mpeg2cut_$p "$D/test_mpeg2cut_abort" "$M2V" "$MP2" "$W/m2_$p" $p; done
  r "$out" cutsequence_abort "$D/test_cutsequence_abort" "$V264" "$A264" "$M2V" "$MP2" "$W/seq"
  for p in none video audio fail; do r "$out" previewcut_$p "$D/test_previewcut_abort" "$V264" "$A264" "$W/pv_$p" $p; done
  r "$out" pool_abort "$D/test_pool_abort"
  r "$out" stale_abort "$D/test_stale_abort" "$M2V" "$MP2" "$W/stale"
  r "$out" abort_after_finish "$D/test_abort_after_finish"
  r "$out" smartcut_abort "$D/test_smartcut_abort" "$V264" 50
  rm -rf "$W"
}
run_cutter() { local out=$1; r "$out" cutter "$ROOT/tools/diag/test_audiocutter_paths" "$MIXED" "$out/work"; }
run_mpv()    { local out=$1; r "$out" mpv "$ROOT/tools/diag/test_mpv_loadfile_args" "$FIX/tux_h264.ts" "$FIX/kömma, tést/a,b\"c]d_deu.srt"; }
run_mtv() {
  local out=$1 REAL; REAL=$(readlink -f "$C"); mkdir -p "$out/cache"
  [ -L "$out/cache" ] && { echo "FAIL: private cache is a symlink"; return 1; }
  case "$(readlink -f "$out/cache")" in "$REAL"*) echo "FAIL: private cache resolves into the real cache"; return 1;; esac
  sed -e "s|^OUTDIR=.*|OUTDIR=\"$out/cache\"|" -e "s|^SCRIPTDIR=.*|SCRIPTDIR=\"$ROOT/tools/test-videos\"|" "$ROOT/tools/test-videos/make_test_video.sh" > "$out/mtv.sh"
  grep -q "^OUTDIR=\"$out/cache\"" "$out/mtv.sh" || { echo "FAIL: OUTDIR not rewritten"; return 1; }
  local f; for f in tux_h264_1080p_progressive_test.264 tux_h264_1080p_progressive_test.ac3 tux_h264_1080i_mbaff_test.264 tux_h264_1080i_mbaff_test.ac3 \
           tux_h264_1080i_paff_test.264 tux_h264_1080i_paff_test.ac3 tux_mpeg2_576i_fieldpic_test.m2v tux_mpeg2_576i_fieldpic_test.mp2 tux_logo.png tux_main.png; do
    cp "$REAL/$f" "$out/cache/$f"; done
  sleep 1; touch "$out/cache"/*   # newer than the script copy -> the skip path runs
  local arg; for arg in h264 mpeg2_576i_fieldpic mpeg2; do bash "$out/mtv.sh" $arg > "$out/run-$arg.log" 2>&1; echo "rc=$?" >> "$out/run-$arg.log"; done
  mkdir -p "$out/ttcut"; cp "$out/cache"/*.ttcut "$out/ttcut/" 2>/dev/null; ls -1 "$out/cache" > "$out/cache-listing.txt"; rm -rf "$out/cache" "$out/mtv.sh"
  for f in "$out"/run-*.log; do echo "$(basename "$f"): $(tail -n1 "$f")"; done
}

if [ "$mode" = run ]; then
  make_fixtures || exit 1
  out="$WORK/$suite-$tag"; rm -rf "$out"; mkdir -p "$out"
  "run_$suite" "$out"
  echo "captured $(find "$out" -type f | wc -l) files in $out"
else
  cmpdir "$WORK/$suite-$a" "$WORK/$suite-$b" | grep -v "^identical\|^same"; s=${PIPESTATUS[0]}
  [ "$s" = 0 ] && echo "GATE $suite PASS: $a == $b" || echo "GATE $suite FAIL: $a != $b"; exit "$s"
fi
