#-------------------------------------------------------------------------------
# SPDX-License-Identifier: GPL-3.0-or-later
#
# TTCut-ng - frame-accurate video cutter
# Copyright (c) 2024-2026 MINIXJR
# Originally TTCut (c) 2003-2010 B. Altendorf / TriTime
#
# Free software under the GNU GPL v3 or later - see the LICENSE file.
#-------------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)

# build-package.sh greps the version out of this line - keep it single-line.
project(ttcut-ng VERSION 0.82.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The qmake build used -std=c++17 (no GNU extensions), not the CMake default
# gnu++17.
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Self-healing compile_commands.json symlink: recreated on every reconfigure
# so a stale or missing link at the project root never has to be fixed by
# hand (clangd reads it from there).
if(CMAKE_EXPORT_COMPILE_COMMANDS)
  file(REMOVE ${CMAKE_SOURCE_DIR}/compile_commands.json)
  file(CREATE_LINK ${CMAKE_BINARY_DIR}/compile_commands.json
       ${CMAKE_SOURCE_DIR}/compile_commands.json SYMBOLIC)
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# NDEBUG stripping (below) only keeps assert() from <cassert> alive in
# RelWithDebInfo - it does NOT reach Q_ASSERT. Qt6::Core appends QT_NO_DEBUG
# for every non-Debug config regardless of NDEBUG, and Q_ASSERT keys off
# QT_NO_DEBUG, not NDEBUG. QT_FORCE_ASSERTS (in add_compile_definitions
# below) is Qt's supported override to keep Q_ASSERT active despite that.
string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
string(REPLACE "-DNDEBUG" "" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
# The .ui files live in ui/, sources include "ui_xxx.h" unqualified.
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_SOURCE_DIR}/ui)

add_compile_options(-Wall -Wextra)
add_compile_definitions(
  _FILE_OFFSET_BITS=64
  UNIX
  APP_VERSION="${PROJECT_VERSION}"
  # API deprecated before Qt 6.7 is compiled out - keeps the tree clean
  # against future deprecations.
  QT_DISABLE_DEPRECATED_BEFORE=0x060700
  # Qt's supported override to keep Q_ASSERT active in non-Debug configs -
  # see the comment above the NDEBUG stripping for why NDEBUG alone does not.
  QT_FORCE_ASSERTS
)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Widgets Gui Xml OpenGL OpenGLWidgets)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVLIBS REQUIRED IMPORTED_TARGET
  libavformat libavcodec libavutil libswscale libavfilter libswresample)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
pkg_check_modules(MPEG2 REQUIRED IMPORTED_TARGET libmpeg2 libmpeg2convert)

set(TTCUT_HEADERS
  common/ttcut.h
  common/ttsettings.h
  common/ttthreadtask.h
  common/ttthreadtaskpool.h
  common/ttmessagelogger.h
  common/ttavlog.h
  common/ttexception.h
  common/istatusreporter.h
  common/ttcalibrationstore.h
  common/ttprogressestimator.h
  data/ttaspectdetect.h
  data/ttanalysislog.h
  data/ttaudiolist.h
  data/ttsubtitlelist.h
  data/ttcutlist.h
  data/ttmarkerlist.h
  data/ttstreampoint.h
  data/ttstreampointmodel.h
  data/ttstreampoint_videoworker.h
  data/ttstreampoint_audioworker.h
  data/ttsearchtask.h
  data/ttsearchtask_blackframe.h
  data/ttsearchtask_scenechange.h
  data/ttsearchtask_logo.h
  data/ttsearchtask_aspectscan.h
  data/ttaudioanomalyscantask.h
  data/ttcutparameter.h
  data/ttmuxlistdata.h
  data/ttavdata.h
  data/ttavlist.h
  data/ttcutprojectdata.h
  data/ttopenvideotask.h
  data/ttopenaudiotask.h
  data/ttopensubtitletask.h
  data/ttcutvideotask.h
  data/ttcutpreviewtask.h
  data/tth26xcuttask.h
  data/ttaudioonlycuttask.h
  data/ttmuxtask.h
  data/ttframesearchtask.h
  data/ttlogodetector.h
  avstream/ttcommon.h
  avstream/ttac3audioheader.h
  avstream/ttac3audiostream.h
  avstream/ttaudioheaderlist.h
  avstream/ttavheader.h
  avstream/ttavstream.h
  avstream/ttavtypes.h
  avstream/ttfilebuffer.h
  avstream/ttheaderlist.h
  avstream/ttmpeg2videoheader.h
  avstream/ttmpeg2videostream.h
  avstream/tth26xvideostream.h
  avstream/tth264videoheader.h
  avstream/tth264videostream.h
  avstream/tth265videoheader.h
  avstream/tth265videostream.h
  avstream/ttmpegaudioheader.h
  avstream/ttmpegaudiostream.h
  avstream/ttvideoheaderlist.h
  avstream/ttesinfo.h
  avstream/ttnaluparser.h
  avstream/ttdisplayordermap.h
  avstream/ttsrtsubtitlestream.h
  avstream/ttsubtitleheaderlist.h
  avstream/ttvideoindexlist.h
  mpeg2decoder/ttmpeg2decoder.h
  mpeg2window/ttmpeg2window2.h
  extern/imuxprovider.h
  extern/ttencodeparameter.h
  extern/tttranscode.h
  extern/ttmplexprovider.h
  extern/ttffmpegwrapper.h
  extern/ttessmartcut.h
  extern/tthevcseam.h
  extern/ttaudiorepair.h
  extern/ttmkvmergeprovider.h
  gui/ttcutsettingsmuxer.h
  gui/ttcutsettingsencoder.h
  gui/ttcutsettingsencoderdefaults.h
  gui/ttcutsettingsdlg.h
  gui/ttcutsettingsnavigation.h
  gui/ttcutsettingssearch.h
  gui/ttcutsettingsstreampoints.h
  gui/ttcutsettingsaudio.h
  gui/ttcutsettingspaths.h
  gui/ttcutsettingslogging.h
  gui/ittmpvbackend.h
  gui/ttmpvrenderwidget.h
  gui/ttmpvlibbackend.h
  gui/ttmpvwrapper.h
  gui/ttaudiotreeview.h
  gui/ttvideotreeview.h
  gui/ttsubtitletreeview.h
  gui/ttstreampointwidget.h
  gui/ttaudiorepairdialog.h
  gui/ttcutoutframe.h
  gui/ttcurrentframe.h
  gui/ttcutframenavigation.h
  gui/ttnavigatordisplay.h
  gui/ttstreamnavigator.h
  gui/ttcuttreeview.h
  gui/ttprogressbar.h
  gui/ttcutpreview.h
  gui/ttcutaboutdlg.h
  gui/ttcutavcutdlg.h
  gui/ttgotoframedialog.h
  gui/ttcutmainwindow.h
  gui/ttquickjumpmodel.h
  gui/ttquickjumpdelegate.h
  gui/ttquickjumpworker.h
  gui/ttquickjumpdialog.h
  gui/ttwindowgeometry.h
)

set(TTCUT_CORE_SOURCES
  common/ttcut.cpp
  common/ttsettings.cpp
  common/ttthreadtask.cpp
  common/ttthreadtaskpool.cpp
  common/ttmessagelogger.cpp
  common/ttavlog.cpp
  common/ttexception.cpp
  common/istatusreporter.cpp
  common/ttcalibrationstore.cpp
  common/ttprogressestimator.cpp
  data/ttaspectdetect.cpp
  data/ttanalysislog.cpp
  data/ttaudiolist.cpp
  data/ttsubtitlelist.cpp
  data/ttcutlist.cpp
  data/ttmarkerlist.cpp
  data/ttstreampoint.cpp
  data/ttstreampointmodel.cpp
  data/ttstreampoint_videoworker.cpp
  data/ttstreampoint_audioworker.cpp
  data/ttsearchtask.cpp
  data/ttsearchtask_blackframe.cpp
  data/ttsearchtask_scenechange.cpp
  data/ttsearchtask_logo.cpp
  data/ttsearchtask_aspectscan.cpp
  data/ttaudioanomalyscantask.cpp
  data/ttcutparameter.cpp
  data/ttmuxlistdata.cpp
  data/ttavdata.cpp
  data/ttavlist.cpp
  data/ttcutprojectdata.cpp
  data/ttopenvideotask.cpp
  data/ttopenaudiotask.cpp
  data/ttopensubtitletask.cpp
  data/ttcutvideotask.cpp
  data/ttcutpreviewtask.cpp
  data/tth26xcuttask.cpp
  data/ttaudioonlycuttask.cpp
  data/ttmuxtask.cpp
  data/ttframesearchtask.cpp
  data/ttlogodetector.cpp
  avstream/ttcommon.cpp
  avstream/ttac3audioheader.cpp
  avstream/ttac3audiostream.cpp
  avstream/ttaudioheaderlist.cpp
  avstream/ttavheader.cpp
  avstream/ttavstream.cpp
  avstream/ttavtypes.cpp
  avstream/ttfilebuffer.cpp
  avstream/ttheaderlist.cpp
  avstream/ttmpeg2videoheader.cpp
  avstream/ttmpeg2videostream.cpp
  avstream/tth26xvideostream.cpp
  avstream/tth264videoheader.cpp
  avstream/tth264videostream.cpp
  avstream/tth265videoheader.cpp
  avstream/tth265videostream.cpp
  avstream/ttmpegaudioheader.cpp
  avstream/ttmpegaudiostream.cpp
  avstream/ttvideoheaderlist.cpp
  avstream/ttesinfo.cpp
  avstream/ttnaluparser.cpp
  avstream/ttdisplayordermap.cpp
  avstream/ttsrtsubtitlestream.cpp
  avstream/ttsubtitleheaderlist.cpp
  avstream/ttvideoindexlist.cpp
  mpeg2decoder/ttmpeg2decoder.cpp
  mpeg2window/ttmpeg2window2.cpp
  extern/tttranscode.cpp
  extern/ttmplexprovider.cpp
  extern/ttffmpegwrapper.cpp
  extern/ttessmartcut.cpp
  extern/tthevcseam.cpp
  extern/ttaudiorepair.cpp
  extern/ttmkvmergeprovider.cpp
  gui/ttcutsettingsmuxer.cpp
  gui/ttcutsettingsencoder.cpp
  gui/ttcutsettingsencoderdefaults.cpp
  gui/ttcutsettingsdlg.cpp
  gui/ttcutsettingsnavigation.cpp
  gui/ttcutsettingssearch.cpp
  gui/ttcutsettingsstreampoints.cpp
  gui/ttcutsettingsaudio.cpp
  gui/ttcutsettingspaths.cpp
  gui/ttcutsettingslogging.cpp
  gui/ttmpvrenderwidget.cpp
  gui/ttmpvlibbackend.cpp
  gui/ttmpvwrapper.cpp
  gui/ttaudiotreeview.cpp
  gui/ttvideotreeview.cpp
  gui/ttsubtitletreeview.cpp
  gui/ttstreampointwidget.cpp
  gui/ttaudiorepairdialog.cpp
  gui/ttcutoutframe.cpp
  gui/ttcurrentframe.cpp
  gui/ttcutframenavigation.cpp
  gui/ttnavigatordisplay.cpp
  gui/ttstreamnavigator.cpp
  gui/ttcuttreeview.cpp
  gui/ttcentredtitlestyle.cpp
  gui/ttprogressbar.cpp
  gui/ttcutpreview.cpp
  gui/ttcutaboutdlg.cpp
  gui/ttcutavcutdlg.cpp
  gui/ttgotoframedialog.cpp
  gui/ttcutmainwindow.cpp
  gui/ttquickjumpmodel.cpp
  gui/ttquickjumpdelegate.cpp
  gui/ttquickjumpworker.cpp
  gui/ttquickjumpdialog.cpp
  gui/ttwindowgeometry.cpp
)

set(TTCUT_RESOURCES
  ui/audiofilelistwidget.qrc
  ui/videofilelistwidget.qrc
  ui/subtitlefilelistwidget.qrc
  ui/cutoutframewidget.qrc
  ui/currentframewidget.qrc
  ui/ttcutframenavigationwidget.qrc
  ui/cutlistwidget.qrc
  ui/ttcutsettingsmuxer.qrc
  ui/ttsettingsdialog.qrc
  ui/previewwidget.qrc
  ui/avcutdialog.qrc
  ui/mainwindow.qrc
)

add_library(ttcut-core STATIC ${TTCUT_HEADERS} ${TTCUT_CORE_SOURCES})
target_include_directories(ttcut-core PUBLIC ${CMAKE_SOURCE_DIR})
# gui/ttcutmainwindow.h (compiled into both ttcut-core and, via
# gui/ttcutmain.cpp, the ttcut-ng executable) includes the AUTOUIC-generated
# ui_ttcutmainwindow.h. AUTOUIC only adds its output directory to the owning
# target's own private include path, so without this the executable target
# cannot find it. Exposing it PUBLIC is the standard way to let a target's
# AUTOUIC output reach consumers that link against it.
target_include_directories(ttcut-core PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/ttcut-core_autogen/include)
# Several .ui files promote custom widgets with a header path one directory
# level above their own file, e.g. currentframewidget.ui declares
# "../mpeg2window/ttmpeg2window2.h" - written assuming the generated
# ui_xxx.h sits directly in a single subdirectory of the repo root, which is
# how qmake's UI_DIR (ui_h/) worked. AUTOUIC instead places generated
# headers under build/ttcut-core_autogen/include/, two levels deeper, so the
# quoted "../..." include fails to resolve relative to the generated file
# itself. GCC/Clang then fall back to the quoted-include search path, so
# adding ui/ (the same one-level-below-root depth as the old UI_DIR) here
# lets "ui/../mpeg2window/..." (and "../gui/...") resolve exactly as it did
# under qmake. PUBLIC because ui_ttcutmainwindow.h (with its own "../gui/..."
# includes) is pulled in by gui/ttcutmain.cpp on the ttcut-ng executable too.
target_include_directories(ttcut-core PUBLIC ${CMAKE_SOURCE_DIR}/ui)
target_link_libraries(ttcut-core PUBLIC
  Qt6::Core Qt6::Widgets Qt6::Gui Qt6::Xml Qt6::OpenGL Qt6::OpenGLWidgets
  PkgConfig::AVLIBS PkgConfig::MPV PkgConfig::MPEG2
  Threads::Threads)

# .qrc files sit on the executable, not the static lib - resources inside a
# static library need Q_INIT_RESOURCE calls to survive the linker.
add_executable(ttcut-ng gui/ttcutmain.cpp ${TTCUT_RESOURCES})
target_link_libraries(ttcut-ng PRIVATE ttcut-core)

# Standalone C tools. Built with the default target (the qmake build did
# this via QMAKE_POST_LINK). Binaries land in their source directories -
# debian/rules installs from there and the user copies them manually.
add_executable(ttcut-pts-analyze tools/ttcut-pts-analyze/ttcut-pts-analyze.c)
set_target_properties(ttcut-pts-analyze PROPERTIES
  C_STANDARD 17 C_STANDARD_REQUIRED ON
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/ttcut-pts-analyze)

add_executable(ttcut-ac3fix tools/ttcut-ac3fix/ttcut-ac3fix.c)
set_target_properties(ttcut-ac3fix PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/ttcut-ac3fix)

add_executable(ttcut-audiofix tools/ttcut-audiofix/ttcut-audiofix.c)
set_target_properties(ttcut-audiofix PROPERTIES
  C_STANDARD 99
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/ttcut-audiofix)

add_subdirectory(tools/diag)
add_subdirectory(tools/ttcut-burst-probe)
