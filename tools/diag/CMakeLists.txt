# Diagnostic tools for TTCut-ng. Built on demand:
#   cmake --build build --target diag            (the default TOOLS set)
#   cmake --build build --target diag-abort      (the cut-abort harnesses)
#   cmake --build build --target test_mkvmux     (material-dependent tools)
#
# Each tool compiles its own curated source list instead of linking the full
# ttcut-core archive. This is deliberate (inherited from the deleted
# Makefile): a new GUI/mpv dependency creeping into the cut path must
# surface as an undefined-reference link error here instead of being
# silently satisfied by a full archive.

set(ROOT ${CMAKE_SOURCE_DIR})

set(NALU_FULL_SRC
  ${ROOT}/avstream/ttnaluparser.cpp
  ${ROOT}/common/ttsettings.cpp
  ${ROOT}/common/ttmessagelogger.cpp)

set(DISPMAP_SRC
  ${ROOT}/avstream/ttdisplayordermap.cpp
  ${ROOT}/common/ttmessagelogger.cpp)

set(WRAPPER_SRC
  ${DISPMAP_SRC}
  ${ROOT}/extern/ttffmpegwrapper.cpp
  ${ROOT}/extern/ttavutil.cpp
  ${ROOT}/extern/ttaudiocutter.cpp
  ${ROOT}/extern/ttframeindexer.cpp
  ${ROOT}/common/ttsettings.cpp
  ${ROOT}/avstream/ttesinfo.cpp
  ${ROOT}/avstream/ttnaluparser.cpp)

set(STILLFRAME_SRC
  ${ROOT}/extern/ttessmartcut.cpp
  ${ROOT}/extern/tthevcseam.cpp
  ${ROOT}/common/ttsettings.cpp
  ${ROOT}/avstream/ttesinfo.cpp
  ${ROOT}/avstream/ttnaluparser.cpp
  ${ROOT}/avstream/ttdisplayordermap.cpp
  ${ROOT}/common/ttmessagelogger.cpp
  ${ROOT}/common/ttcalibrationstore.cpp)

set(SEAM_SRC ${STILLFRAME_SRC} ${ROOT}/extern/ttffmpegwrapper.cpp ${ROOT}/extern/ttavutil.cpp ${ROOT}/extern/ttaudiocutter.cpp ${ROOT}/extern/ttframeindexer.cpp)

set(MKVMUX_SRC ${SEAM_SRC} ${ROOT}/extern/ttmkvmergeprovider.cpp)

set(FILEBUF_SRC
  ${ROOT}/avstream/ttfilebuffer.cpp
  ${ROOT}/common/ttmessagelogger.cpp
  # ttfilebuffer.cpp throws TTIOException, whose base class TTException has
  # out-of-line ctor/dtor/typeinfo - without this the only user of this list
  # (test_startcode_scan) fails to LINK, which in turn breaks the documented
  # `cmake --build build --target diag` build instruction for every tool.
  ${ROOT}/common/ttexception.cpp
  ${ROOT}/common/ttsettings.cpp)

set(ESINFO_SRC
  ${ROOT}/avstream/ttesinfo.cpp
  ${ROOT}/common/ttsettings.cpp
  ${ROOT}/common/ttmessagelogger.cpp)

set(PROGEST_SRC
  ${ROOT}/common/ttprogressestimator.cpp
  ${ROOT}/common/ttcalibrationstore.cpp)

set(POOLABORT_SRC
  ${ROOT}/common/ttthreadtaskpool.cpp
  ${ROOT}/common/ttthreadtask.cpp
  ${ROOT}/common/istatusreporter.cpp
  ${ROOT}/common/ttmessagelogger.cpp
  ${ROOT}/common/ttexception.cpp
  ${ROOT}/common/ttsettings.cpp)

# The curated MPEG-2 cut-path link set (Makefile MPEG2CUT_OBJS). Pulls NO
# GUI/mpv sources - that absence IS the guard.
set(MPEG2CUT_SRC
  ${ROOT}/common/istatusreporter.cpp
  ${ROOT}/avstream/ttaudioheaderlist.cpp
  ${ROOT}/avstream/ttavheader.cpp
  ${ROOT}/avstream/ttavstream.cpp
  ${ROOT}/avstream/ttcommon.cpp
  ${ROOT}/avstream/ttcutparameter.cpp
  ${ROOT}/common/ttexception.cpp
  ${ROOT}/avstream/ttfilebuffer.cpp
  ${ROOT}/avstream/ttheaderlist.cpp
  ${ROOT}/common/ttmessagelogger.cpp
  ${ROOT}/mpeg2decoder/ttmpeg2decoder.cpp
  ${ROOT}/avstream/ttmpeg2videoheader.cpp
  ${ROOT}/avstream/ttmpeg2videostream.cpp
  ${ROOT}/common/ttsettings.cpp
  ${ROOT}/extern/tttranscode.cpp
  ${ROOT}/avstream/ttvideoheaderlist.cpp
  ${ROOT}/avstream/ttvideoindexlist.cpp)

set(ASPECTSCAN_SRC
  ${ROOT}/data/ttsearchtask_aspectscan.cpp
  ${ROOT}/data/ttsearchtask.cpp
  ${ROOT}/data/ttaspectdetect.cpp
  ${ROOT}/data/ttanalysislog.cpp
  ${ROOT}/data/ttstreampoint.cpp
  ${ROOT}/common/ttthreadtask.cpp
  ${ROOT}/avstream/ttvideoindexlist.cpp
  ${ROOT}/avstream/ttvideoheaderlist.cpp
  ${ROOT}/avstream/ttheaderlist.cpp
  ${ROOT}/avstream/ttavheader.cpp
  ${ROOT}/avstream/ttmpeg2videoheader.cpp
  ${ROOT}/common/ttexception.cpp
  ${ROOT}/avstream/ttfilebuffer.cpp
  ${ROOT}/avstream/ttcommon.cpp
  ${ROOT}/common/istatusreporter.cpp
  ${ROOT}/mpeg2decoder/ttmpeg2decoder.cpp
  ${WRAPPER_SRC})

set(MPEG2ASPECTSCAN_SRC
  ${MPEG2CUT_SRC}
  ${ROOT}/data/ttsearchtask_aspectscan.cpp
  ${ROOT}/data/ttsearchtask.cpp
  ${ROOT}/data/ttaspectdetect.cpp
  ${ROOT}/data/ttanalysislog.cpp
  ${ROOT}/data/ttstreampoint.cpp
  ${ROOT}/common/ttthreadtask.cpp
  ${ROOT}/avstream/ttdisplayordermap.cpp
  ${ROOT}/extern/ttffmpegwrapper.cpp
  ${ROOT}/extern/ttavutil.cpp
  ${ROOT}/extern/ttaudiocutter.cpp
  ${ROOT}/extern/ttframeindexer.cpp
  ${ROOT}/avstream/ttesinfo.cpp
  ${ROOT}/avstream/ttnaluparser.cpp)

set(ANOMALYSCAN_SRC
  ${ROOT}/data/ttaudioanomalyscantask.cpp
  ${ROOT}/data/ttstreampoint.cpp
  ${ROOT}/common/ttthreadtask.cpp
  ${ROOT}/common/ttmessagelogger.cpp
  ${ROOT}/common/ttsettings.cpp
  ${ROOT}/common/istatusreporter.cpp
  ${ROOT}/common/ttexception.cpp)

set(STREAMPOINTORDER_SRC
  ${MPEG2CUT_SRC}
  ${ROOT}/data/ttstreampoint_videoworker.cpp
  ${ROOT}/data/ttstreampoint.cpp
  ${ROOT}/data/ttanalysislog.cpp
  ${ROOT}/common/ttthreadtask.cpp)

set(MKVORDER_SRC
  ${ROOT}/extern/ttmkvmergeprovider.cpp
  ${ROOT}/common/ttcut.cpp
  ${WRAPPER_SRC})

set(WINDOWJUMP_SRC
  ${MPEG2CUT_SRC}
  ${ROOT}/mpeg2window/ttmpeg2window2.cpp
  ${ROOT}/common/ttcut.cpp
  ${ROOT}/extern/ttffmpegwrapper.cpp
  ${ROOT}/extern/ttavutil.cpp
  ${ROOT}/extern/ttaudiocutter.cpp
  ${ROOT}/extern/ttframeindexer.cpp
  ${ROOT}/avstream/ttdisplayordermap.cpp
  ${ROOT}/avstream/ttesinfo.cpp
  ${ROOT}/avstream/ttnaluparser.cpp
  ${ROOT}/avstream/ttsubtitleheaderlist.cpp
  ${ROOT}/avstream/tth26xvideostream.cpp)

# diag_tool(<name> [AV] [MPEG2] [WIDGETS] SOURCES <curated list>)
# Base link set mirrors the Makefile's LDLIBS: Qt6Core + Qt6Gui + pthread.
function(diag_tool name)
  cmake_parse_arguments(DT "AV;MPEG2;WIDGETS" "" "SOURCES" ${ARGN})
  set(srcs ${DT_SOURCES})
  if(srcs)
    list(REMOVE_DUPLICATES srcs)
  endif()
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp ${srcs})
  set_target_properties(${name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  target_include_directories(${name} PRIVATE ${ROOT})
  target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Gui Threads::Threads)
  if(DT_AV)
    target_link_libraries(${name} PRIVATE PkgConfig::AVLIBS)
  endif()
  if(DT_MPEG2)
    target_link_libraries(${name} PRIVATE PkgConfig::MPEG2)
  endif()
  if(DT_WIDGETS)
    target_link_libraries(${name} PRIVATE Qt6::Widgets)
  endif()
endfunction()

set(AUDIOREPAIR_SRC
  ${ROOT}/extern/ttaudiorepair.cpp)

set(SILENCE_SRC
  ${ROOT}/data/ttstreampoint_audioworker.cpp
  ${ROOT}/data/ttstreampoint.cpp
  ${ROOT}/data/ttanalysislog.cpp
  ${ROOT}/common/ttthreadtask.cpp
  ${ROOT}/common/ttmessagelogger.cpp
  ${ROOT}/common/ttsettings.cpp
  ${ROOT}/common/istatusreporter.cpp
  ${ROOT}/avstream/ttavheader.cpp
  ${ROOT}/avstream/ttheaderlist.cpp
  ${ROOT}/avstream/ttaudioheaderlist.cpp
  ${ROOT}/avstream/ttac3audioheader.cpp
  ${ROOT}/avstream/ttcommon.cpp
  ${ROOT}/common/ttexception.cpp
  ${ROOT}/avstream/ttfilebuffer.cpp)

# --- default TOOLS set (built by the `diag` umbrella target) ---
diag_tool(test_nalu_parser        SOURCES ${NALU_FULL_SRC})
diag_tool(test_au_types           SOURCES ${NALU_FULL_SRC})
diag_tool(probe_copystart         SOURCES ${NALU_FULL_SRC})
diag_tool(test_displayordermap AV SOURCES ${DISPMAP_SRC})
diag_tool(test_leadingclass    AV SOURCES ${DISPMAP_SRC})
diag_tool(test_h264_leading    AV SOURCES ${DISPMAP_SRC})
diag_tool(test_wrapper_map     AV SOURCES ${WRAPPER_SRC})
diag_tool(test_slider_decode_cost AV SOURCES)
target_link_libraries(test_slider_decode_cost PRIVATE ttcut-core)
diag_tool(test_pillarbox       AV SOURCES ${WRAPPER_SRC})
diag_tool(test_stilldisplay    AV SOURCES ${WRAPPER_SRC})
diag_tool(test_startcode_scan     SOURCES ${FILEBUF_SRC})
diag_tool(test_esinfo             SOURCES ${ESINFO_SRC})
diag_tool(test_audiofix_esinfo    SOURCES ${ESINFO_SRC})
diag_tool(test_esinfo_dump        SOURCES ${ESINFO_SRC})
diag_tool(test_hevc_seam          SOURCES ${ROOT}/extern/tthevcseam.cpp)
diag_tool(test_audiorepair     AV SOURCES ${AUDIOREPAIR_SRC})
# Drives TTAVData::cutAudioTracks() with a real TTAVItem/TTAudioStream (the
# cut-segment-boundary error path needs the actual per-track loop, not just
# TTFFmpegWrapper::cutAudioStream in isolation), so it needs the whole core
# library (same rationale as test_subtitle_delay).
diag_tool(test_audiorepair_cut AV WIDGETS SOURCES)
target_link_libraries(test_audiorepair_cut PRIVATE ttcut-core)
diag_tool(test_aspectdetect       SOURCES ${ROOT}/data/ttaspectdetect.cpp)
diag_tool(test_analysislog        SOURCES ${ROOT}/data/ttanalysislog.cpp)
diag_tool(test_streampoint_anomaly SOURCES ${ROOT}/data/ttstreampoint.cpp)
diag_tool(test_silence_unavailable AV SOURCES ${SILENCE_SRC})
diag_tool(test_aspectscan  AV MPEG2 SOURCES ${ASPECTSCAN_SRC})
diag_tool(test_anomalyscan AV SOURCES ${ANOMALYSCAN_SRC})
diag_tool(test_aspectscan_mpeg2 AV MPEG2 SOURCES ${MPEG2ASPECTSCAN_SRC})
diag_tool(test_pool_abort         SOURCES ${POOLABORT_SRC})
diag_tool(test_abort_after_finish SOURCES ${POOLABORT_SRC})
diag_tool(test_streampoint_order AV MPEG2 SOURCES ${STREAMPOINTORDER_SRC})
diag_tool(test_mpeg2_seek  AV MPEG2 SOURCES ${MPEG2CUT_SRC})
diag_tool(test_seqheader_missing AV MPEG2 SOURCES ${MPEG2CUT_SRC})
diag_tool(test_headerlist_eof   AV MPEG2 SOURCES ${MPEG2CUT_SRC})
diag_tool(test_window_geometry WIDGETS SOURCES ${ROOT}/gui/ttwindowgeometry.cpp)
diag_tool(test_progressestimator  SOURCES ${PROGEST_SRC})

# --- MATERIAL_TOOLS (on demand, need a specific recording) ---
diag_tool(test_audiocut        AV SOURCES ${WRAPPER_SRC})
diag_tool(test_frameindex_dump AV SOURCES ${WRAPPER_SRC})
diag_tool(test_audiocut_abort  AV SOURCES ${WRAPPER_SRC})
diag_tool(test_audioprogress   AV SOURCES ${WRAPPER_SRC})
diag_tool(test_rawmap          AV SOURCES ${WRAPPER_SRC})
diag_tool(test_adopt_paff      AV SOURCES ${WRAPPER_SRC})
diag_tool(test_index_bundle_adopt AV SOURCES ${WRAPPER_SRC})
diag_tool(test_sar             AV SOURCES ${WRAPPER_SRC})
diag_tool(test_decode_cancel   AV SOURCES ${WRAPPER_SRC})
diag_tool(test_stillframe      AV SOURCES ${STILLFRAME_SRC})
diag_tool(test_segshape        AV SOURCES ${STILLFRAME_SRC})
diag_tool(test_cutprogress     AV SOURCES ${STILLFRAME_SRC})
diag_tool(test_smartcut_seam   AV SOURCES ${SEAM_SRC})
diag_tool(test_smartcut_abort  AV SOURCES ${SEAM_SRC})
diag_tool(test_mkvmux          AV SOURCES ${MKVMUX_SRC})
diag_tool(bench_playback_mux   AV SOURCES ${MKVMUX_SRC})
diag_tool(test_mkvmux_abort    AV SOURCES ${MKVMUX_SRC})
diag_tool(test_feed_decode     AV SOURCES ${NALU_FULL_SRC})
diag_tool(test_mpeg2_cutout    AV MPEG2 SOURCES ${MPEG2CUT_SRC})
diag_tool(test_extra_index_rank AV MPEG2 SOURCES ${MPEG2CUT_SRC})
diag_tool(dump_mpeg2_fields    AV MPEG2 SOURCES ${MPEG2CUT_SRC})
diag_tool(test_mpeg2order      AV WIDGETS SOURCES ${MKVORDER_SRC})
# The one tool that links the whole ttcut-core instead of a curated list. It
# drives TTH26xCutTask through TTAVData::onDoCut(), and TTAVData already pulls
# in QMessageBox, the mplex provider and every open/cut task - a curated list
# for it would be the core library minus nothing, so the "GUI creep shows up as
# an undefined reference" guard cannot apply here anyway.
diag_tool(test_h26xcut_abort   AV WIDGETS SOURCES)
target_link_libraries(test_h26xcut_abort PRIVATE ttcut-core)
# Same rationale as test_h26xcut_abort: drives TTAVData::onDoCut()'s H.26x
# branch too (the entry point this harness checks the Exit/lastCutError
# ordering on), so it needs the whole core library rather than a curated list.
diag_tool(test_cut_outcome     AV WIDGETS SOURCES)
diag_tool(test_partial_track     AV WIDGETS SOURCES)
target_link_libraries(test_partial_track PRIVATE ttcut-core)
# Drives TTAVData::cutSubtitleTracks() and planAudioCut() directly, so it
# needs the whole core library (same rationale as test_h26xcut_abort).
diag_tool(test_subtitle_delay    AV WIDGETS SOURCES)
target_link_libraries(test_subtitle_delay PRIVATE ttcut-core)
target_link_libraries(test_cut_outcome PRIVATE ttcut-core)
# Drives TTAVData::openAVStreams()/writeProjectFile()/readProjectFile() end to
# end (the .ttcut <Repair> round trip), so it needs the whole core library
# too (same rationale as test_subtitle_delay).
diag_tool(test_audiorepair_persist AV WIDGETS SOURCES)
target_link_libraries(test_audiorepair_persist PRIVATE ttcut-core)
# Drives TTAudioRepairDialog (the repair dialog itself, Task 7) against a
# real TTAVData-opened AVItem, so it needs the whole core library too (same
# rationale as test_audiorepair_persist).
diag_tool(test_repairdialog_model AV WIDGETS SOURCES)
target_link_libraries(test_repairdialog_model PRIVATE ttcut-core)
# Review fix 2 round 2: proves TTAudioRepairDialog's mpv-Play-button error-
# popup gate across two real loads plus a provoked load failure. Needs a
# real GL-capable platform for mpv's render context (same reason
# test_mainwindow_then_cut refuses offscreen) - deliberately NOT in the
# `diag` DEPENDS list below, same as that harness.
diag_tool(test_repairdialog_mpv_lifecycle AV WIDGETS SOURCES)
target_link_libraries(test_repairdialog_mpv_lifecycle PRIVATE ttcut-core)
# Same rationale as test_h26xcut_abort: drives TTAVData::onDoCut()'s MPEG-2
# branch (TTCutVideoTask on the pool, mplex for the mux stage), so it needs
# the whole core library, not a curated source list.
diag_tool(test_mpeg2cut_abort  AV MPEG2 WIDGETS SOURCES)
target_link_libraries(test_mpeg2cut_abort PRIVATE ttcut-core)
# Same rationale as test_h26xcut_abort: drives TTAudioOnlyCutTask through
# TTAVData::onDoCut(audioOnly=true), so it needs the whole core library too.
diag_tool(test_audioonlycut_abort AV WIDGETS SOURCES)
target_link_libraries(test_audioonlycut_abort PRIVATE ttcut-core)
# Same rationale as test_h26xcut_abort: drives TTCutPreviewTask through
# TTAVData::doCutPreview() (a different entry point than onDoCut(), but the
# same TTAVData/pool/QMessageBox dependency chain), so it needs the whole
# core library too.
diag_tool(test_previewcut_abort AV WIDGETS SOURCES)
target_link_libraries(test_previewcut_abort PRIVATE ttcut-core)
# Same rationale again, plus both codec branches in one process: it drives
# onDoCut() for H.26x AND MPEG-2 and doCutPreview(), five operations on one
# long-lived TTAVData.
diag_tool(test_cutsequence_abort AV MPEG2 WIDGETS SOURCES)
target_link_libraries(test_cutsequence_abort PRIVATE ttcut-core)
diag_tool(test_window_jump     AV MPEG2 WIDGETS SOURCES ${WINDOWJUMP_SRC})
# Drives TTProgressBar's status sink directly (dialog re-show guard). Links
# ttcut-core rather than a curated list because the dialog's header includes
# the AUTOUIC-generated ui_ttprogressform.h, and that generated include
# directory is exported by the ttcut-core target (see the root CMakeLists).
diag_tool(test_progressbar_reshow WIDGETS SOURCES)
diag_tool(test_filedialog_proxy   WIDGETS SOURCES)
diag_tool(test_dialog_then_cut    AV WIDGETS SOURCES)
diag_tool(test_preview_then_cut   AV WIDGETS SOURCES)
diag_tool(test_mainwindow_then_cut AV WIDGETS SOURCES)
diag_tool(test_audio_order_reset  AV MPEG2 WIDGETS SOURCES)
diag_tool(test_quickjump_thumbheight AV MPEG2 WIDGETS SOURCES)
diag_tool(test_auto_anomaly_scan_trigger AV MPEG2 WIDGETS SOURCES)
diag_tool(test_stale_abort        AV WIDGETS SOURCES)
diag_tool(test_framesearch_progress AV WIDGETS SOURCES)
diag_tool(test_directed_search AV WIDGETS SOURCES)
diag_tool(test_audio_header_strings AV WIDGETS SOURCES)
diag_tool(test_pulse_animation    WIDGETS SOURCES)
diag_tool(test_cutlist_minsize    AV WIDGETS SOURCES)
diag_tool(test_output_name        AV WIDGETS SOURCES)
diag_tool(test_pulse_stylesheet   WIDGETS SOURCES
  ${ROOT}/gui/ttcentredtitlestyle.cpp
  ${ROOT}/common/ttmessagelogger.cpp
  ${ROOT}/common/ttsettings.cpp)
target_link_libraries(test_stale_abort PRIVATE ttcut-core)
target_link_libraries(test_framesearch_progress PRIVATE ttcut-core)
target_link_libraries(test_directed_search PRIVATE ttcut-core)
target_link_libraries(test_audio_header_strings PRIVATE ttcut-core)
target_link_libraries(test_dialog_then_cut PRIVATE ttcut-core)
target_link_libraries(test_preview_then_cut PRIVATE ttcut-core)
target_link_libraries(test_mainwindow_then_cut PRIVATE ttcut-core)
target_link_libraries(test_audio_order_reset PRIVATE ttcut-core)
target_link_libraries(test_quickjump_thumbheight PRIVATE ttcut-core)
target_link_libraries(test_auto_anomaly_scan_trigger PRIVATE ttcut-core)
target_link_libraries(test_cutlist_minsize PRIVATE ttcut-core)
target_link_libraries(test_output_name PRIVATE ttcut-core)
target_link_libraries(test_progressbar_reshow PRIVATE ttcut-core)

# test_pool_crossthread and test_task_cleanup_order are NOT CMake targets:
# their gate scripts compile them with a sanitizer themselves.

add_custom_target(diag DEPENDS
  test_nalu_parser test_au_types test_displayordermap test_wrapper_map
  test_stilldisplay test_leadingclass test_h264_leading probe_copystart
  test_startcode_scan test_esinfo test_audiofix_esinfo test_hevc_seam test_aspectdetect
  test_analysislog test_streampoint_anomaly test_silence_unavailable test_aspectscan test_aspectscan_mpeg2
  test_anomalyscan
  test_pillarbox test_pool_abort
  test_streampoint_order test_mpeg2_seek test_seqheader_missing test_headerlist_eof test_window_geometry
  test_progressestimator test_subtitle_delay test_audiorepair_persist test_audiorepair
  test_audiorepair_cut test_repairdialog_model)

# --- the cut-abort harness suite -------------------------------------------
# One command builds every harness the abort verification matrix runs, so a
# matrix run can never test binaries left over from an earlier session. That
# is not a hypothetical: diag_tool() writes its executables into this SOURCE
# directory, .gitignore hides them (tools/diag/test_*) and the repository sets
# status.showUntrackedFiles=no - a stale binary is invisible to `git status`
# and runs happily. Naming the seven (now ten) targets by hand on the build
# line already cost one full matrix run.
#
# A target of its own so the abort matrix has one short name to build, but NOT
# a separate island: `diag` depends on it below, so `--target diag` really does
# build everything. The two names exist for different jobs - `diag` links
# curated per-tool source lists so that a new GUI/mpv dependency in the cut
# path shows up as an undefined reference (see the file header), while seven of
# these ten link the whole ttcut-core and cannot provide that guard. Keeping
# them in their own target preserves the distinction (and lets a matrix run
# rebuild just them); making `diag` depend on it means nobody has to know the
# second name to get a complete build. The price is the longer `diag` build -
# a deliberate trade.
add_custom_target(diag-abort DEPENDS
  test_smartcut_abort test_audiocut_abort test_mkvmux_abort
  test_h26xcut_abort test_mpeg2cut_abort test_audioonlycut_abort
  test_previewcut_abort test_cutsequence_abort test_progressbar_reshow
  test_cut_outcome)

add_dependencies(diag diag-abort)
