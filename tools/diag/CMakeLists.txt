# Diagnostic tools for TTCut-ng. Built on demand:
#   cmake --build build --target diag            (the default TOOLS set)
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
  ${ROOT}/common/ttmessagelogger.cpp)

set(SEAM_SRC ${STILLFRAME_SRC} ${ROOT}/extern/ttffmpegwrapper.cpp)

set(MKVMUX_SRC ${SEAM_SRC} ${ROOT}/extern/ttmkvmergeprovider.cpp)

set(FILEBUF_SRC
  ${ROOT}/avstream/ttfilebuffer.cpp
  ${ROOT}/common/ttmessagelogger.cpp
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
  ${ROOT}/data/ttcutparameter.cpp
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
  ${ROOT}/data/ttstreampoint.cpp
  ${ROOT}/common/ttthreadtask.cpp
  ${ROOT}/avstream/ttdisplayordermap.cpp
  ${ROOT}/extern/ttffmpegwrapper.cpp
  ${ROOT}/avstream/ttesinfo.cpp
  ${ROOT}/avstream/ttnaluparser.cpp)

set(STREAMPOINTORDER_SRC
  ${MPEG2CUT_SRC}
  ${ROOT}/data/ttstreampoint_videoworker.cpp
  ${ROOT}/data/ttstreampoint.cpp
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

# --- default TOOLS set (built by the `diag` umbrella target) ---
diag_tool(test_nalu_parser        SOURCES ${NALU_FULL_SRC})
diag_tool(test_au_types           SOURCES ${NALU_FULL_SRC})
diag_tool(probe_copystart         SOURCES ${NALU_FULL_SRC})
diag_tool(test_displayordermap AV SOURCES ${DISPMAP_SRC})
diag_tool(test_leadingclass    AV SOURCES ${DISPMAP_SRC})
diag_tool(test_h264_leading    AV SOURCES ${DISPMAP_SRC})
diag_tool(test_wrapper_map     AV SOURCES ${WRAPPER_SRC})
diag_tool(test_pillarbox       AV SOURCES ${WRAPPER_SRC})
diag_tool(test_stilldisplay    AV SOURCES ${WRAPPER_SRC})
diag_tool(test_startcode_scan     SOURCES ${FILEBUF_SRC})
diag_tool(test_esinfo             SOURCES ${ESINFO_SRC})
diag_tool(test_hevc_seam          SOURCES ${ROOT}/extern/tthevcseam.cpp)
diag_tool(test_aspectdetect       SOURCES ${ROOT}/data/ttaspectdetect.cpp)
diag_tool(test_aspectscan  AV MPEG2 SOURCES ${ASPECTSCAN_SRC})
diag_tool(test_aspectscan_mpeg2 AV MPEG2 SOURCES ${MPEG2ASPECTSCAN_SRC})
diag_tool(test_pool_abort         SOURCES ${POOLABORT_SRC})
diag_tool(test_streampoint_order AV MPEG2 SOURCES ${STREAMPOINTORDER_SRC})
diag_tool(test_mpeg2_seek  AV MPEG2 SOURCES ${MPEG2CUT_SRC})
diag_tool(test_window_geometry WIDGETS SOURCES ${ROOT}/gui/ttwindowgeometry.cpp)
diag_tool(test_progressestimator  SOURCES ${PROGEST_SRC})

# --- MATERIAL_TOOLS (on demand, need a specific recording) ---
diag_tool(test_audiocut        AV SOURCES ${WRAPPER_SRC})
diag_tool(test_audioprogress   AV SOURCES ${WRAPPER_SRC})
diag_tool(test_rawmap          AV SOURCES ${WRAPPER_SRC})
diag_tool(test_adopt_paff      AV SOURCES ${WRAPPER_SRC})
diag_tool(test_sar             AV SOURCES ${WRAPPER_SRC})
diag_tool(test_stillframe      AV SOURCES ${STILLFRAME_SRC})
diag_tool(test_segshape        AV SOURCES ${STILLFRAME_SRC})
diag_tool(test_cutprogress     AV SOURCES ${STILLFRAME_SRC})
diag_tool(test_smartcut_seam   AV SOURCES ${SEAM_SRC})
diag_tool(test_mkvmux          AV SOURCES ${MKVMUX_SRC})
diag_tool(test_feed_decode     AV SOURCES ${NALU_FULL_SRC})
diag_tool(test_mpeg2_cutout    AV MPEG2 SOURCES ${MPEG2CUT_SRC})
diag_tool(test_extra_index_rank AV MPEG2 SOURCES ${MPEG2CUT_SRC})
diag_tool(dump_mpeg2_fields    AV MPEG2 SOURCES ${MPEG2CUT_SRC})
diag_tool(test_mpeg2order      AV WIDGETS SOURCES ${MKVORDER_SRC})
diag_tool(test_window_jump     AV MPEG2 WIDGETS SOURCES ${WINDOWJUMP_SRC})

# test_pool_crossthread and test_task_cleanup_order are NOT CMake targets:
# their gate scripts compile them with a sanitizer themselves.

add_custom_target(diag DEPENDS
  test_nalu_parser test_au_types test_displayordermap test_wrapper_map
  test_stilldisplay test_leadingclass test_h264_leading probe_copystart
  test_startcode_scan test_esinfo test_hevc_seam test_aspectdetect
  test_aspectscan test_aspectscan_mpeg2 test_pillarbox test_pool_abort
  test_streampoint_order test_mpeg2_seek test_window_geometry
  test_progressestimator)
