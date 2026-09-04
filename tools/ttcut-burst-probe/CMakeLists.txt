# Standalone probe for TTAudioCutter::detectBurst. Deliberately
# EXCLUDE_FROM_ALL so a tool used only during verification doesn't slow
# every application build. Build with:
#   cmake --build build --target ttcut-burst-probe
#
# core-only: main.cpp uses TTAudioCutter (extern/ttaudiocutter.h) directly,
# not TTFFmpegWrapper, so the source list stops at TTAudioCutter's own
# dependencies (ttavutil, the logger, settings) and no QtGui pull-in
# (ttffmpegwrapper.h's <QImage> include) applies here.

set(ROOT ${CMAKE_SOURCE_DIR})

add_executable(ttcut-burst-probe EXCLUDE_FROM_ALL
  main.cpp
  ${ROOT}/extern/ttaudiocutter.cpp
  ${ROOT}/avstream/ttavutil.cpp
  ${ROOT}/common/ttmessagelogger.cpp
  ${ROOT}/common/ttsettings.cpp)

set_target_properties(ttcut-burst-probe PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories(ttcut-burst-probe PRIVATE ${ROOT})
target_link_libraries(ttcut-burst-probe PRIVATE
  Qt6::Core Threads::Threads PkgConfig::AVLIBS)
