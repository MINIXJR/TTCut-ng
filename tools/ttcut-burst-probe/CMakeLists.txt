# Standalone probe for TTAudioCutter::detectBurst. Deliberately
# EXCLUDE_FROM_ALL: it recompiles ttffmpegwrapper.cpp, which would slow
# every application build for a tool used during verification. Build with:
#   cmake --build build --target ttcut-burst-probe
#
# core+gui: ttffmpegwrapper.h includes <QImage> (QtGui) at the header level,
# so QtGui is needed to compile it even though no QtGui *symbols* are
# linked. QtWidgets stays out.

set(ROOT ${CMAKE_SOURCE_DIR})

add_executable(ttcut-burst-probe EXCLUDE_FROM_ALL
  main.cpp
  ${ROOT}/extern/ttffmpegwrapper.cpp
  ${ROOT}/extern/ttavutil.cpp
  ${ROOT}/extern/ttaudiocutter.cpp
  ${ROOT}/avstream/ttdisplayordermap.cpp
  ${ROOT}/avstream/ttesinfo.cpp
  ${ROOT}/avstream/ttnaluparser.cpp
  ${ROOT}/common/ttmessagelogger.cpp
  ${ROOT}/common/ttsettings.cpp)

set_target_properties(ttcut-burst-probe PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories(ttcut-burst-probe PRIVATE ${ROOT})
target_link_libraries(ttcut-burst-probe PRIVATE
  Qt6::Core Qt6::Gui Threads::Threads PkgConfig::AVLIBS)
