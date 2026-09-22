#!/bin/bash
# run-gates.sh - builds and runs every self-verdicting harness and gate script
# that needs nothing beyond the repository fixtures (the Tux set in
# tools/test-videos/cache, tools/testdata, synthetic files), one PASS/FAIL/SKIP
# line per gate with its run time. Quality roadmap step 1
# (docs/quality-roadmap.md): run before every merge to master and in the
# release skill (step 5).
#
#   tools/diag/run-gates.sh              all tiers
#   tools/diag/run-gates.sh --quick      tier unit only (no video fixtures)
#   tools/diag/run-gates.sh --list       print the table and exit
#   tools/diag/run-gates.sh --no-build   skip the cmake step
#   tools/diag/run-gates.sh NAME...      only these gates (names from --list)
#
# Tiers: unit = synthetic input or none, seconds each; tux = Tux fixtures,
# up to minutes each; san = sanitizer builds (ThreadSanitizer, AddressSanitizer).
#
# Verdict = exit code of the gate: 0 PASS; 77 SKIP (a prerequisite is missing,
# the reason is the first line of the log); 124 or 137 TIMEOUT; anything else
# FAIL. Exit code of the whole run: 0 all PASS, 1 at least one FAIL or
# TIMEOUT, 2 no FAIL but at least one SKIP.
#
# Every gate runs with QT_QPA_PLATFORM=offscreen, a private XDG_CONFIG_HOME and
# XDG_CACHE_HOME and a work directory of its own under the run directory
# (RUN, below): the user's TTCut-ng.conf is neither read nor written, a stored
# TempDirPath cannot leak in, and the log file a harness inspects is its own.
# The CMake targets a gate needs are built first, so no stale binary from an
# earlier session is ever run (tools/diag binaries are gitignored and
# invisible to git status).
#
# Deliberately NOT in the table:
#   need a display (refuse offscreen): test_dialog_then_cut,
#     test_filedialog_proxy, test_mainwindow_then_cut, test_preview_then_cut,
#     test_pulse_animation, test_pulse_stylesheet, test_repairdialog_mpv_lifecycle
#   compare two builds, need a baseline: gate_cut_identity.sh,
#     gate_refactor_identity.sh, gate_ac3fix.sh
#   need material outside the repository: gate_audiofix.sh (NAS corpus, no skip
#     path), gate_h264_leading.sh corpus cases, test_wrapper_map and
#     test_stillframe (expectations frozen on corpus files), test_aspectscan
#     (no Tux fixture has an aspect switch), test_auto_anomaly_scan_trigger
#     project-markers (needs a project with an AudioAnomaly marker)
#   need material outside the repository, measured 2026-09-11 on the Tux set:
#     test_playback_mux_async (its header asks for a mux of several seconds;
#     on the 33 MB fixture the dialog never showed and progress stayed at -1,
#     3 of 18 checks failed), test_search_cancel (search mode decodes only
#     keyframes without DPB prefill, one packet with no cancel poll inside;
#     on the Tux fixtures such a decode takes 3-30 ms, the 40 % trigger lands
#     after it and a frame is produced; documented run: Moon-Crash 5000/77777),
#     test_h264_leading on the PAFF fixture (compares the raw AU map with
#     decoder frames, 6000 field AUs against 3000 frames - the gate covers
#     progressive and MBAFF only, as its header says)
#   helper missing: gate_hevc_align.sh (dump_img was never checked in)
#   report only, no verdict: test_nalu_parser, test_au_types, probe_copystart,
#     test_startcode_scan, test_esinfo_dump, test_frameindex_dump,
#     test_probe_video, test_rawmap, test_pillarbox, test_stilldisplay,
#     test_feed_decode, test_streampoint_order,
#     test_slider_decode_cost, test_mpeg2_seek, test_window_jump,
#     test_mpeg2_cutout, test_audio_header_strings, test_directed_search,
#     test_framesearch_progress, test_cutprogress, test_audiocut,
#     test_audioprogress, test_mkvmux, test_smartcut_seam (gated through
#     gate_h264_seam.sh instead), test_hevc_seam (needs a smart-cut output),
#     bench_playback_mux, repro_playback_mkv, dump_mpeg2_fields
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
D="$ROOT/tools/diag"
CACHE="$ROOT/tools/test-videos/cache"
TESTDATA="$ROOT/tools/testdata"
GATES_ROOT=/usr/local/src/CLAUDE_TMP/TTCut-ng/gates

V264="$CACHE/tux_h264_1080p_progressive_test.264"   # 1080p50 progressive
A264="$CACHE/tux_h264_1080p_progressive_test.ac3"   # 192 kbit/s, 768 B frames
SRT="$CACHE/tux_h264_1080p_progressive_test.srt"
PRJ264="$CACHE/tux_h264_1080p_progressive_test.ttcut"
MBAFF="$CACHE/tux_h264_1080i_mbaff_test.264"
PAFF="$CACHE/tux_h264_1080i_paff_test.264"
H265="$CACHE/tux_hevc4k_cra_test.265"
M2V="$CACHE/tux_mpeg2_576i_pal_test.m2v"
M2VFP="$CACHE/tux_mpeg2_576i_fieldpic_test.m2v"        # field pictures every 50 frames
MP2="$CACHE/tux_mpeg2_576i_pal_test.mp2"            # 192 kbit/s, 576 B frames
DEMUX="$ROOT/tools/ttcut-demux/ttcut-demux"
AUDIOFIX="$ROOT/tools/ttcut-audiofix/ttcut-audiofix"

# ---- table: name | tier | timeout s | cmake targets (- = none) ---------------
# The gate function is gate_<name>; W (work dir) is set and current when it runs.
GATES='
displayordermap        unit  120  test_displayordermap
leadingclass           unit  120  test_leadingclass
analysislog            unit  120  test_analysislog
aspectdetect           unit  120  test_aspectdetect
progressestimator      unit  120  test_progressestimator
streampoint_anomaly    unit  120  test_streampoint_anomaly
esinfo                 unit  120  test_esinfo
audiofix_esinfo        unit  120  test_audiofix_esinfo
mpeg2order             unit  120  test_mpeg2order
quickjump_thumbheight  unit  120  test_quickjump_thumbheight
window_geometry        unit  120  test_window_geometry
container_sync         unit  120  test_container_sync
settings_cancel        unit  120  test_settings_cancel
analysis_task_lifetime unit  120  test_analysis_task_lifetime
streampoint_model_time unit  120  test_streampoint_model_time
project_load_rejected  unit  120  test_project_load_rejected
cut_range_check        unit  120  test_cut_range_check
cut_job_ownership      unit  120  test_cut_job_ownership
aspect_window          unit  120  test_aspect_window
aspect_hint            unit  300  test_aspect_hint
aspect_autocut         unit  300  -
exit_cancel            tux   300  test_exit_cancel
exit_discard           tux   300  test_exit_cancel
exit_savefail          tux   300  test_exit_cancel
pool_abort             unit  120  test_pool_abort
abort_after_finish     unit  120  test_abort_after_finish
cutlist_minsize        unit  120  test_cutlist_minsize
output_name            unit  120  test_output_name
progressbar_reshow     unit  120  test_progressbar_reshow
silence_unavailable    unit  120  test_silence_unavailable
anomalyscan            unit  300  test_anomalyscan
audiorepair            unit  300  test_audiorepair
audiorepair_cut        unit  300  test_audiorepair_cut
repairdialog_model     unit  300  test_repairdialog_model
demux_zonesync         unit  300  -
demux_gapsync          unit  300  -
ffmpeg_edge_packets    unit  120  -
audiofix_edge_ac3      unit  120  -
audiofix_edge_mp2      unit  120  -
h264_leading           tux   600  test_h264_leading
sar                    tux   120  test_sar
decode_cancel          tux   300  test_decode_cancel
decode_cancel_yuv      tux   300  test_decode_cancel_yuv
adopt_paff             tux   300  test_adopt_paff
index_bundle_adopt     tux   300  test_index_bundle_adopt
aspectscan_mpeg2       tux   300  test_aspectscan_mpeg2
seqheader_missing      tux   300  test_seqheader_missing
headerlist_eof         tux   300  test_headerlist_eof
segshape               tux   600  test_segshape
h264_seam              tux   600  test_smartcut_seam
acmod_majority         tux   300  test_acmod_majority
hint_column            tux   300  test_hint_column
audiocutter_paths      tux   300  test_audiocutter_paths
mpv_loadfile_args      tux   300  test_mpv_loadfile_args
mpv_channels           unit  300  test_mpv_channels
subtitle_delay         tux   600  test_subtitle_delay
audiorepair_persist    tux   600  test_audiorepair_persist
audio_order_reset      tux   600  test_audio_order_reset
anomaly_trigger_video  tux   600  test_auto_anomaly_scan_trigger
anomaly_trigger_project tux  600  test_auto_anomaly_scan_trigger
anomaly_trigger_abort  tux   600  test_auto_anomaly_scan_trigger
cut_outcome            tux   600  test_cut_outcome
partial_track          tux   600  test_partial_track
project_roundtrip_264  tux   600  test_project_roundtrip
project_roundtrip_m2v  tux   600  test_project_roundtrip
open_track_failure     tux   600  test_open_track_failure
extra_index_rank       tux   300  test_extra_index_rank
stale_abort            tux   600  test_stale_abort
audiocut_abort         tux   300  test_audiocut_abort
audioonlycut_none      tux   600  test_audioonlycut_abort
audioonlycut_audio     tux   600  test_audioonlycut_abort
audioonlycut_mux       tux   600  test_audioonlycut_abort
h26xcut_none           tux   900  test_h26xcut_abort
h26xcut_video          tux   900  test_h26xcut_abort
h26xcut_audio          tux   900  test_h26xcut_abort
h26xcut_mux            tux   900  test_h26xcut_abort
mkvmux_abort           tux   300  test_mkvmux_abort
mpeg2cut_none          tux   900  test_mpeg2cut_abort
mpeg2cut_audio         tux   900  test_mpeg2cut_abort
mpeg2cut_video         tux   900  test_mpeg2cut_abort
mpeg2cut_mux           tux   900  test_mpeg2cut_abort
cutsequence_abort      tux   900  test_cutsequence_abort
previewcut_none        tux   600  test_previewcut_abort
previewcut_video       tux   600  test_previewcut_abort
previewcut_audio       tux   600  test_previewcut_abort
previewcut_fail        tux   600  test_previewcut_abort
preview_clip_h264      tux   600  test_preview_clip
preview_clip_mpeg2     tux   600  test_preview_clip
smartcut_abort_h264    tux   600  test_smartcut_abort
smartcut_abort_hevc    tux   600  test_smartcut_abort
encode_tempdir         tux   900  test_mpeg2cut_abort
pool_crossthread       san   900  -
task_cleanup_order     san   600  -
'

# ---- helpers for the gate functions -----------------------------------------
need() { local f; for f in "$@"; do [ -e "$f" ] || { echo "SKIP: missing $f"; exit 77; }; done; }
need_bin() { local b="$D/$1"; [ -x "$b" ] || { echo "SKIP: not built: $b"; exit 77; }; }

# stereo + 5.1 + stereo at one frame size plus four junk bytes; the recipe of
# gate_ac3fix.sh, kept identical so both see the same acmod switches.
make_mixed_ac3() {
  local out=$1 t; t=$(mktemp -d)
  ffmpeg -y -v error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=8" \
      -ac 2 -c:a ac3 -b:a 448k "$t/stereo.ac3" || return 1
  ffmpeg -y -v error -f lavfi -i "sine=frequency=330:sample_rate=48000:duration=4" \
      -af "pan=5.1|FL=c0|FR=c0|FC=c0|LFE=c0|BL=c0|BR=c0" \
      -c:a ac3 -b:a 448k "$t/s51.ac3" || return 1
  { cat "$t/stereo.ac3" "$t/s51.ac3" "$t/stereo.ac3"; printf 'junk'; } > "$out"
  rm -rf "$t"
}

# 12 s MPEG-2 with open GOPs (-bf 2: from the second GOP on every I-picture
# has two leading B-pictures) whose sequence headers 10..14 are patched to
# 16:9; the rest stays 4:3. $2 receives "A B", the display positions of the
# first and last 16:9 picture, computed from GOP base + temporal_reference -
# an oracle that does not go through TTCut's index list.
make_aspect_m2v() {
  local out=$1 expect=$2
  ffmpeg -y -v error -f lavfi -i "testsrc2=size=720x576:rate=25" -t 12 \
      -c:v mpeg2video -pix_fmt yuv420p -aspect 4:3 -b:v 4M -bf 2 -g 12 \
      -sc_threshold 1000000000 -f mpeg2video "$out.src" || return 1
  python3 - "$out.src" "$out" "$expect" <<'PY' || return 1
import sys
d = bytearray(open(sys.argv[1], 'rb').read())
PATCH = range(10, 15)
k = pos = 0
while (i := d.find(b'\x00\x00\x01\xb3', pos)) >= 0:
    if k in PATCH:
        d[i + 7] = (d[i + 7] & 0x0F) | 0x30      # aspect_ratio_information = 3
    k += 1
    pos = i + 4
assert k > max(PATCH), f"only {k} sequence headers"
open(sys.argv[2], 'wb').write(d)
pos = pic = base = 0
ar = None
wide = []
while (i := d.find(b'\x00\x00\x01', pos)) >= 0 and i + 6 <= len(d):
    c = d[i + 3]
    if c == 0xB3:
        ar = d[i + 7] >> 4
    elif c == 0xB8:
        base = pic
    elif c == 0x00:
        tref = (d[i + 4] << 2) | (d[i + 5] >> 6)
        if ar == 3:
            wide.append(base + tref)
        pic += 1
    pos = i + 3
wide.sort()
assert wide and wide == list(range(wide[0], wide[-1] + 1)), "16:9 run not contiguous"
open(sys.argv[3], 'w').write(f"{wide[0]} {wide[-1]}\n")
PY
  rm -f "$out.src"
}

# Two-track MPEG-2 project with a cut, as test_audio_order_reset.cpp documents.
make_two_track_project() {
  local out=$1
  cp "$MP2" "$W/tux_audio_eng.mp2"; cp "$MP2" "$W/tux_audio_deu.mp2"
  cat > "$out" <<PRJ
<!DOCTYPE TTCut-Projectfile>
<TTCut-Projectfile>
 <Version>1.0</Version>
 <Video>
  <Order>0</Order>
  <Name>$M2V</Name>
  <Audio><Order>0</Order><Name>$W/tux_audio_eng.mp2</Name><Language>eng</Language></Audio>
  <Audio><Order>1</Order><Name>$W/tux_audio_deu.mp2</Name><Language>deu</Language></Audio>
  <Cut><Order>0</Order><CutIn>200</CutIn><CutOut>400</CutOut></Cut>
 </Video>
</TTCut-Projectfile>
PRJ
}

# ---- tier unit ---------------------------------------------------------------
gate_displayordermap()       { "$D/test_displayordermap"; }
gate_leadingclass()          { "$D/test_leadingclass"; }
gate_analysislog()           { "$D/test_analysislog"; }
gate_aspectdetect()          { "$D/test_aspectdetect"; }
gate_progressestimator()     { "$D/test_progressestimator"; }
gate_streampoint_anomaly()   { "$D/test_streampoint_anomaly"; }
gate_esinfo()                { "$D/test_esinfo"; }
gate_audiofix_esinfo()       { "$D/test_audiofix_esinfo"; }
gate_mpeg2order()            { "$D/test_mpeg2order"; }
gate_quickjump_thumbheight() { "$D/test_quickjump_thumbheight"; }
gate_window_geometry()       { "$D/test_window_geometry"; }
gate_container_sync()        { "$D/test_container_sync"; }
gate_settings_cancel()       { "$D/test_settings_cancel"; }
gate_analysis_task_lifetime() { "$D/test_analysis_task_lifetime"; }
gate_streampoint_model_time() { "$D/test_streampoint_model_time"; }
gate_project_load_rejected() { "$D/test_project_load_rejected" "$W"; }
gate_cut_range_check() { "$D/test_cut_range_check" "$W"; }
gate_cut_job_ownership() { "$D/test_cut_job_ownership" "$W"; }
gate_exit_cancel()           { need "$V264"; "$D/test_exit_cancel" "$V264" cancel; }
gate_exit_discard()          { need "$V264"; "$D/test_exit_cancel" "$V264" discard; }
gate_exit_savefail()         { need "$V264"; "$D/test_exit_cancel" "$V264" savefail; }
gate_pool_abort()            { "$D/test_pool_abort"; }
gate_abort_after_finish()    { "$D/test_abort_after_finish"; }
gate_cutlist_minsize()       { "$D/test_cutlist_minsize"; }
gate_output_name()           { "$D/test_output_name"; }
gate_progressbar_reshow()    { "$D/test_progressbar_reshow"; }
gate_silence_unavailable()   { "$D/test_silence_unavailable"; }
gate_anomalyscan()           { "$D/test_anomalyscan"; }
gate_audiorepair()           { "$D/test_audiorepair"; }
gate_audiorepair_cut()       { "$D/test_audiorepair_cut"; }
gate_repairdialog_model()    { need "$TESTDATA/tux_test.264" "$TESTDATA/tux_test.ac3"; "$D/test_repairdialog_model"; }
gate_demux_zonesync()        { need "$DEMUX"; "$D/gate_demux_zonesync.sh"; }
gate_demux_gapsync()         { need "$DEMUX"; "$D/gate_demux_gapsync.sh"; }
gate_ffmpeg_edge_packets()   { need "$DEMUX"; "$D/gate_ffmpeg_edge_packets.sh" "$DEMUX"; }
gate_audiofix_edge_ac3()     { need "$AUDIOFIX" "$A264"; "$D/gate_audiofix_edge.sh" "$AUDIOFIX" "$A264" 768; }
gate_audiofix_edge_mp2()     { need "$AUDIOFIX" "$MP2";  "$D/gate_audiofix_edge.sh" "$AUDIOFIX" "$MP2" 576; }

# ---- tier tux ----------------------------------------------------------------
# Only the progressive fixture has a documented drop count (0, IDR start); the
# other three still assert displayToDecode(0) and the prefix alignment.
gate_h264_leading()  { need "$V264" "$MBAFF" "$H265"
                       "$D/gate_h264_leading.sh" "$D/test_h264_leading" "$V264:0" "$MBAFF" "$H265"; }
gate_sar()           { need "$V264"; "$D/test_sar" "$V264" 1.0; }
gate_decode_cancel()     { need "$V264"; "$D/test_decode_cancel" "$V264" 1500; }
gate_decode_cancel_yuv() { need "$V264"; "$D/test_decode_cancel_yuv" "$V264" 1500; }
gate_adopt_paff()        { need "$PAFF"; "$D/test_adopt_paff" "$PAFF" 200; }
gate_index_bundle_adopt() { need "$PAFF"; "$D/test_index_bundle_adopt" "$PAFF" 200; }
# The Tux timeline has no aspect switch: the gate is "exactly 0 transitions".
gate_aspectscan_mpeg2()  { need "$M2V"; "$D/test_aspectscan_mpeg2" "$M2V" 2 0; }
gate_aspect_window() { make_aspect_m2v "$W/aspect.m2v" "$W/aspect.expect" || exit 1
  read -r A B < "$W/aspect.expect"
  "$D/test_aspect_window" "$W/aspect.m2v" "$A" "$B"; }
# The harness needs a header list WITHOUT a sequence header (what a run reads
# back after another instance overwrote its encode.m2v): the first GOP of the
# fixture, from its group_start_code up to the next sequence_header_code.
gate_seqheader_missing() { need "$M2V"
  python3 - "$M2V" "$W/noseq.m2v" <<'PY' || exit 1
import sys
d = open(sys.argv[1], 'rb').read(4_000_000)
gop = d.find(b'\x00\x00\x01\xb8'); seq = d.find(b'\x00\x00\x01\xb3', gop)
assert gop > 0 and seq > gop, (gop, seq)
open(sys.argv[2], 'wb').write(d[gop:seq])
PY
  "$D/test_seqheader_missing" "$W/noseq.m2v"; }
gate_headerlist_eof()    { need "$M2V"; "$D/gate_headerlist_eof.sh" "$D/test_headerlist_eof" "$M2V"; }
# Two segments across the BLUE/BLACK/RED boundaries at 30 s and 31 s (50 fps).
gate_segshape()  { need "$V264"; "$D/test_segshape" "$V264" 50 300 700 1450 1600; }
gate_h264_seam() { need "$V264"; "$D/gate_h264_seam.sh" "$D/test_smartcut_seam" "$V264" 300 700 50; }
gate_acmod_majority()    { make_mixed_ac3 "$W/mixed.ac3" || exit 1; "$D/test_acmod_majority" "$W/mixed.ac3"; }
# Playback channel layout: the mapping, plus a live libmpv run on the same
# mixed-acmod fixture that counts audio-output initialisations.
gate_mpv_channels()      { make_mixed_ac3 "$W/mixed.ac3" || exit 1; "$D/test_mpv_channels" "$W/mixed.ac3"; }
gate_hint_column()       { need "$V264"; make_mixed_ac3 "$W/mixed.ac3" || exit 1; "$D/test_hint_column" "$V264" "$W/mixed.ac3" "$W/hint"; }
gate_aspect_hint() { make_aspect_m2v "$W/aspect.m2v" "$W/aspect.expect" || exit 1
  read -r A B < "$W/aspect.expect"
  "$D/test_aspect_hint" "$W/aspect.m2v" "$A" "$B" "$W/hint"; }
# --auto-cut on a 4:3-start cut: the aspect warning must reach the log and the
# run must end (a modal dialog would hang it until the timeout). LC_ALL=C.UTF-8
# because the translator follows the system locale; the log file is on by
# default and lives below XDG_CACHE_HOME, which the runner points into $W.
gate_aspect_autocut() { make_aspect_m2v "$W/aspect.m2v" "$W/aspect.expect" || exit 1
  read -r A B < "$W/aspect.expect"
  ffmpeg -y -v error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=12" \
      -c:a mp2 -b:a 192k "$W/aspect.mp2" || exit 1
  mkdir -p "$W/out"
  cat > "$W/aspect.ttcut" <<PRJ
<!DOCTYPE TTCut-Projectfile>
<TTCut-Projectfile>
 <Version>1.0</Version>
 <Video>
  <Order>0</Order>
  <Name>$W/aspect.m2v</Name>
  <Audio>
   <Order>0</Order>
   <Name>$W/aspect.mp2</Name>
   <Language>deu</Language>
  </Audio>
  <Cut>
   <Order>0</Order>
   <CutIn>$((A - 1))</CutIn>
   <CutOut>$((B - 5))</CutOut>
  </Cut>
 </Video>
</TTCut-Projectfile>
PRJ
  LC_ALL=C.UTF-8 "$ROOT/build/ttcut-ng" --project "$W/aspect.ttcut" --auto-cut "$W/out/aspect.mkv"
  echo "auto-cut exit code $?"
  local log="$XDG_CACHE_HOME/ttcut-ng/logfile.log"
  grep -F "Cut 1: starts in 4:3, the cut is 16:9 from frame $A" "$log" \
    || { echo "FAIL: no aspect warning in $log"; exit 1; }
  grep -F "cut warning(s) - proceeding (auto-cut)" "$log" \
    || { echo "FAIL: no auto-cut summary line in $log"; exit 1; }
  # The requested output decides container and place: aspect.mkv must be an
  # MKV right there, the intermediate ES must carry its extension, and
  # nothing may land in HOME (the mplex default directory).
  local fmt; fmt=$(ffprobe -v error -show_entries format=format_name -of default=nw=1:nk=1 "$W/out/aspect.mkv" 2>/dev/null)
  [ "${fmt%%,*}" = matroska ] || { echo "FAIL: out/aspect.mkv is not an MKV (format '$fmt')"; ls -la "$W/out"; exit 1; }
  [ ! -e "$W/out/aspect" ] || { echo "FAIL: intermediate ES written without extension"; exit 1; }
  [ -z "$(ls -A "$HOME")" ] || { echo "FAIL: files written to HOME:"; ls -la "$HOME"; exit 1; }
  echo "PASS: aspect warning logged, aspect.mkv written, HOME untouched"; }
gate_audiocutter_paths() { make_mixed_ac3 "$W/mixed.ac3" || exit 1; "$D/test_audiocutter_paths" "$W/mixed.ac3" "$W/out"; }
# A subtitle path with a comma, a space and an umlaut (reference_mpv_loadfile_comma).
gate_mpv_loadfile_args() { need "$V264" "$SRT"; mkdir -p "$W/kömma, tést"; cp "$SRT" "$W/kömma, tést/a,b_deu.srt"
                           "$D/test_mpv_loadfile_args" "$V264" "$W/kömma, tést/a,b_deu.srt"; }
gate_subtitle_delay()      { need "$A264"; "$D/test_subtitle_delay" "$A264" "$W"; }
gate_audiorepair_persist() { need "$TESTDATA/tux_test.ttcut"; "$D/test_audiorepair_persist" "$W"; }
gate_audio_order_reset()   { need "$M2V" "$MP2" "$SRT"; make_two_track_project "$W/roundtrip.ttcut"
                             "$D/test_audio_order_reset" "$W/roundtrip.ttcut" "$SRT" "$W"; }
gate_anomaly_trigger_video()   { need "$V264"; "$D/test_auto_anomaly_scan_trigger" video "$V264" "$W"; }
gate_anomaly_trigger_project() { need "$PRJ264"; cp "$PRJ264" "$W/p.ttcut"; "$D/test_auto_anomaly_scan_trigger" project-clean "$W/p.ttcut" "$W"; }
gate_anomaly_trigger_abort()   { need "$V264"; "$D/test_auto_anomaly_scan_trigger" project-abort-then-video "$V264" "$W"; }
gate_cut_outcome()   { need "$V264" "$A264"; "$D/test_cut_outcome" "$V264" "$A264" "$W"; }
gate_partial_track() { need "$V264" "$A264"; "$D/test_partial_track" "$V264" "$A264" "$W"; }
gate_project_roundtrip_264() { need "$PRJ264"; "$D/test_project_roundtrip" "$PRJ264" "$W" rt264; }
gate_project_roundtrip_m2v() { need "$M2V" "$MP2"; make_two_track_project "$W/rt-two-track.ttcut"
                               "$D/test_project_roundtrip" "$W/rt-two-track.ttcut" "$W" rtm2v; }
gate_open_track_failure()  { need "$M2V" "$MP2"; "$D/test_open_track_failure" "$M2V" "$MP2" "$W"; }
gate_extra_index_rank()    { need "$M2VFP"; "$D/test_extra_index_rank" "$M2VFP" 4; }
gate_stale_abort()   { need "$M2V" "$MP2"; "$D/test_stale_abort" "$M2V" "$MP2" "$W"; }
# The abort matrix, phase by phase (same invocations as gate_refactor_identity.sh's
# harness suite). Several of these write into a fixed CLAUDE_TMP/cut-abort path.
gate_audiocut_abort()    { need "$A264"; mkdir -p "$GATES_ROOT/../cut-abort"; "$D/test_audiocut_abort" "$A264"; }
gate_audioonlycut_none()  { need "$V264" "$A264"; "$D/test_audioonlycut_abort" "$V264" "$A264" "$W" none; }
gate_audioonlycut_audio() { need "$V264" "$A264"; "$D/test_audioonlycut_abort" "$V264" "$A264" "$W" audio; }
gate_audioonlycut_mux()   { need "$V264" "$A264"; "$D/test_audioonlycut_abort" "$V264" "$A264" "$W" mux; }
gate_h26xcut_none()  { need "$V264" "$A264"; "$D/test_h26xcut_abort" "$V264" "$A264" "$W" none; }
gate_h26xcut_video() { need "$V264" "$A264"; "$D/test_h26xcut_abort" "$V264" "$A264" "$W" video; }
gate_h26xcut_audio() { need "$V264" "$A264"; "$D/test_h26xcut_abort" "$V264" "$A264" "$W" audio; }
gate_h26xcut_mux()   { need "$V264" "$A264"; "$D/test_h26xcut_abort" "$V264" "$A264" "$W" mux; }
gate_mkvmux_abort()  { need "$V264" "$A264"; mkdir -p "$GATES_ROOT/../cut-abort"; "$D/test_mkvmux_abort" "$V264" "$A264" 50; }
gate_mpeg2cut_none()  { need "$M2V" "$MP2"; "$D/test_mpeg2cut_abort" "$M2V" "$MP2" "$W" none; }
gate_mpeg2cut_audio() { need "$M2V" "$MP2"; "$D/test_mpeg2cut_abort" "$M2V" "$MP2" "$W" audio; }
gate_mpeg2cut_video() { need "$M2V" "$MP2"; "$D/test_mpeg2cut_abort" "$M2V" "$MP2" "$W" video; }
gate_mpeg2cut_mux()   { need "$M2V" "$MP2"; "$D/test_mpeg2cut_abort" "$M2V" "$MP2" "$W" mux; }
gate_cutsequence_abort() { need "$V264" "$A264" "$M2V" "$MP2"; "$D/test_cutsequence_abort" "$V264" "$A264" "$M2V" "$MP2" "$W"; }
gate_previewcut_none()  { need "$V264" "$A264"; "$D/test_previewcut_abort" "$V264" "$A264" "$W" none; }
gate_previewcut_video() { need "$V264" "$A264"; "$D/test_previewcut_abort" "$V264" "$A264" "$W" video; }
# Holds the preview dialog's single-clip rebuild against the clip the preview
# TASK produced for the same cut - the two must not drift apart. Both codec
# branches, because they share the fragments but not the audio cut.
gate_preview_clip_h264()  { need "$V264" "$A264"; "$D/test_preview_clip" "$V264" "$A264" "$W"; }
gate_preview_clip_mpeg2() { need "$M2V" "$MP2";   "$D/test_preview_clip" "$M2V" "$MP2" "$W"; }
gate_previewcut_audio() { need "$V264" "$A264"; "$D/test_previewcut_abort" "$V264" "$A264" "$W" audio; }
gate_previewcut_fail()  { need "$V264" "$A264"; "$D/test_previewcut_abort" "$V264" "$A264" "$W" fail; }
gate_smartcut_abort_h264() { need "$V264"; mkdir -p "$GATES_ROOT/../cut-abort"; "$D/test_smartcut_abort" "$V264" 50; }
gate_smartcut_abort_hevc() { need "$H265"; mkdir -p "$GATES_ROOT/../cut-abort"; "$D/test_smartcut_abort" "$H265" 50; }
gate_encode_tempdir() { need "$M2V" "$MP2"; "$D/gate_encode_tempdir.sh"; }

# ---- tier san ----------------------------------------------------------------
gate_pool_crossthread() { "$D/gate_pool_crossthread.sh"; }
# Built by hand with AddressSanitizer, the recipe at the bottom of the harness.
gate_task_cleanup_order() {
  local moc mocdir; moc="$(pkg-config --variable=libexecdir Qt6Core)/moc"; mocdir="$W/moc"; mkdir -p "$mocdir"
  # -p gives moc an absolute include prefix: the work directory lies on
  # another mount, so the relative path moc computes from there does not resolve.
  local h; for h in ttthreadtask ttthreadtaskpool ttsettings istatusreporter; do
    "$moc" -p "$ROOT/common" "$ROOT/common/$h.h" -o "$mocdir/moc_$h.cpp" || exit 1; done
  "$moc" -p "$D" "$D/test_task_cleanup_order.cpp" -o "$mocdir/moc_test_task_cleanup_order.cpp" || exit 1
  g++ -g -O1 -fsanitize=address -fno-omit-frame-pointer -fPIC -std=gnu++17 \
      -I"$ROOT" -I"$mocdir" $(pkg-config --cflags Qt6Core) \
      -o "$W/test_task_cleanup_order" "$D/test_task_cleanup_order.cpp" \
      "$ROOT"/common/{ttthreadtask,ttthreadtaskpool,ttmessagelogger,ttexception,ttsettings,istatusreporter}.cpp \
      "$mocdir"/moc_{ttthreadtask,ttthreadtaskpool,ttsettings,istatusreporter}.cpp \
      $(pkg-config --libs Qt6Core) -lpthread || exit 1
  "$W/test_task_cleanup_order"
}

# ---- runner ------------------------------------------------------------------
table() { echo "$GATES" | awk 'NF==4'; }

if [ "${1:-}" = --exec ]; then          # child: one gate, environment already set
  cd "$W" || exit 1
  exec < /dev/null   # a gate must never read the runner's stdin (ffmpeg would take table lines as commands)
  "gate_$2"
  exit $?
fi

QUICK=0; BUILD=1; LIST=0; SELECT=()
for a in "$@"; do
  case "$a" in
    --quick) QUICK=1 ;; --no-build) BUILD=0 ;; --list) LIST=1 ;;
    --help|-h) sed -n '2,25p' "$0"; exit 0 ;;
    -*) echo "unknown option $a"; exit 2 ;;
    *) SELECT+=("$a") ;;
  esac
done

selected() {                            # name tier -> 0 if this gate runs
  local name=$1 tier=$2 s
  if [ ${#SELECT[@]} -gt 0 ]; then
    for s in "${SELECT[@]}"; do [ "$s" = "$name" ] && return 0; done; return 1
  fi
  [ $QUICK -eq 1 ] && [ "$tier" != unit ] && return 1
  return 0
}

if [ $LIST -eq 1 ]; then
  table | awk '{printf "%-24s %-5s %5ss  %s\n", $1, $2, $3, $4}'; exit 0
fi
for s in "${SELECT[@]}"; do
  table | awk -v n="$s" '$1==n{f=1} END{exit !f}' || { echo "unknown gate $s (see --list)"; exit 2; }
done

RUN="$GATES_ROOT/$(date +%Y%m%d-%H%M%S)"; mkdir -p "$RUN"
echo "run directory: $RUN"
# Build and gates run outside the terminal's cgroup memory limit where
# systemd-run is available (reference_konsole_memory_high_cgroup: a 1 GB
# memory.high slows a full build by a factor of five and a mux by 25).
runner=(); command -v systemd-run >/dev/null && runner=(systemd-run --user --scope -p MemoryHigh=infinity --quiet)

if [ $BUILD -eq 1 ]; then
  targets=$(table | while read -r n t s tg; do selected "$n" "$t" && [ "$tg" != - ] && echo "$tg"; done | sort -u | tr '\n' ' ')
  echo "building: ttcut-ng tools $targets"
  # Outside the terminal's cgroup memory limit where systemd-run is available
  # (reference_konsole_memory_high_cgroup: a 1 GB memory.high slows a full
  # build by a factor of five).
  { "${runner[@]}" cmake --build "$ROOT/build" && \
    { [ -z "$targets" ] || "${runner[@]}" cmake --build "$ROOT/build" --target $targets; }; } > "$RUN/build.log" 2>&1 \
    || { echo "BUILD FAILED - see $RUN/build.log"; tail -n 20 "$RUN/build.log"; exit 1; }
fi

pass=0; fail=0; skip=0
printf '%-8s %-24s %8s\n' VERDICT GATE TIME
while read -r name tier secs targets; do
  selected "$name" "$tier" || continue
  W="$RUN/work/$name"; mkdir -p "$W/xdg-config" "$W/xdg-cache" "$W/home"
  t0=$(date +%s.%N)
  # HOME too: TTSettings defaults the mplex output directory (and the last
  # directory) to QDir::homePath(), so harnesses that drive TTAVData without
  # setting it wrote their .mpg into the real home directory on every run.
  W="$W" QT_QPA_PLATFORM=offscreen HOME="$W/home" XDG_CONFIG_HOME="$W/xdg-config" XDG_CACHE_HOME="$W/xdg-cache" \
    "${runner[@]}" timeout -k 10 "$secs" "$0" --exec "$name" < /dev/null > "$RUN/$name.log" 2>&1
  rc=$?
  dt=$(awk -v a="$t0" -v b="$(date +%s.%N)" 'BEGIN{printf "%.1f", b-a}')
  case $rc in
    0)       v=PASS;    pass=$((pass+1)) ;;
    77)      v=SKIP;    skip=$((skip+1)) ;;
    124|137) v=TIMEOUT; fail=$((fail+1)) ;;
    *)       v=FAIL;    fail=$((fail+1)) ;;
  esac
  printf '%-8s %-24s %7ss' "$v" "$name" "$dt"
  case $v in
    SKIP) printf '  %s' "$(head -n1 "$RUN/$name.log")" ;;
    FAIL|TIMEOUT) printf '  rc=%s  %s' "$rc" "$RUN/$name.log" ;;
  esac
  echo
done < <(table)

echo "---- $pass PASS, $fail FAIL, $skip SKIP  (logs: $RUN)"
[ $fail -gt 0 ] && exit 1
[ $skip -gt 0 ] && exit 2
exit 0
