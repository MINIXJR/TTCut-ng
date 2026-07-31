# Diagnostic tools for TTCut-ng. Built on demand via `make diag` from the
# repo root, or directly via `make` here. Each tool links against
# obj/ttnaluparser.o produced by the main build, so a regular `make` must
# have run at least once. Sources stay tracked, binaries are gitignored.

QT_INC := $(shell pkg-config --cflags Qt5Core Qt5Gui)
QT_LIB := $(shell pkg-config --libs Qt5Core Qt5Gui)

NALU_OBJ      := ../../obj/ttnaluparser.o
DISPMAP_OBJS  := ../../obj/ttdisplayordermap.o ../../obj/ttmessagelogger.o

# Objects needed for the TTFFmpegWrapper integration test (Part 2)
WRAPPER_OBJS  := $(DISPMAP_OBJS) \
                 ../../obj/ttffmpegwrapper.o ../../obj/moc_ttffmpegwrapper.o \
                 ../../obj/ttsettings.o ../../obj/moc_ttsettings.o \
                 ../../obj/ttesinfo.o ../../obj/ttnaluparser.o

# Objects needed for the smart-cut still-frame acceptance test (Task 5)
STILLFRAME_OBJS := ../../obj/ttessmartcut.o ../../obj/moc_ttessmartcut.o \
                   ../../obj/tthevcseam.o \
                   ../../obj/ttsettings.o ../../obj/moc_ttsettings.o \
                   ../../obj/ttesinfo.o ../../obj/ttnaluparser.o \
                   ../../obj/ttdisplayordermap.o ../../obj/ttmessagelogger.o

CXXFLAGS ?= -O2 -Wall -fPIC -std=gnu++17 -I../.. $(QT_INC)
LDLIBS   := $(QT_LIB) -lpthread

AVLIBS   := -lavformat -lavcodec -lavutil -lswscale -lswresample

TOOLS := check_idr test_nalu_parser test_au_types test_displayordermap test_wrapper_map test_stilldisplay test_leadingclass test_h264_leading probe_copystart test_startcode_scan test_esinfo test_hevc_seam test_aspectdetect test_aspectscan test_aspectscan_mpeg2 test_pillarbox test_pool_abort test_streampoint_order test_mpeg2_seek

NALU_FULL_OBJS := $(NALU_OBJ) ../../obj/ttsettings.o ../../obj/moc_ttsettings.o ../../obj/ttmessagelogger.o

FILEBUF_OBJS := ../../obj/ttfilebuffer.o ../../obj/ttmessagelogger.o ../../obj/ttsettings.o ../../obj/moc_ttsettings.o

# Objects needed for the TTESInfo demux-repair field parsing test
ESINFO_OBJS := ../../obj/ttesinfo.o ../../obj/ttsettings.o ../../obj/moc_ttsettings.o \
               ../../obj/ttmessagelogger.o

# Objects needed for the HEVC seam bitstream machinery test
HEVCSEAM_OBJS := ../../obj/tthevcseam.o

.PHONY: all clean
all: $(TOOLS)

check_idr test_nalu_parser test_au_types probe_copystart: %: %.cpp $(NALU_FULL_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(NALU_FULL_OBJS) $(LDLIBS)

test_leadingclass: test_leadingclass.cpp $(DISPMAP_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(DISPMAP_OBJS) $(LDLIBS) $(AVLIBS)

test_displayordermap: test_displayordermap.cpp $(DISPMAP_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(DISPMAP_OBJS) $(LDLIBS) $(AVLIBS)

test_h264_leading: test_h264_leading.cpp $(DISPMAP_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(DISPMAP_OBJS) $(LDLIBS) $(AVLIBS)

test_wrapper_map: test_wrapper_map.cpp $(WRAPPER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(WRAPPER_OBJS) $(LDLIBS) $(AVLIBS)

test_pillarbox: test_pillarbox.cpp $(WRAPPER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(WRAPPER_OBJS) $(LDLIBS) $(AVLIBS)

# test_aspectdetect: pure-data acceptance for the aspect classifier + hysteresis
ASPECTDETECT_OBJS := ../../obj/ttaspectdetect.o

# test_pool_abort: regression harness for TTThreadTaskPool::onUserAbortRequest
POOLABORT_OBJS := ../../obj/ttthreadtaskpool.o ../../obj/moc_ttthreadtaskpool.o \
                  ../../obj/ttthreadtask.o ../../obj/moc_ttthreadtask.o \
                  ../../obj/istatusreporter.o ../../obj/moc_istatusreporter.o \
                  ../../obj/ttmessagelogger.o ../../obj/ttexception.o \
                  ../../obj/ttsettings.o ../../obj/moc_ttsettings.o

test_pool_abort: test_pool_abort.cpp $(POOLABORT_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(POOLABORT_OBJS) $(LDLIBS)

test_aspectdetect: test_aspectdetect.cpp $(ASPECTDETECT_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(ASPECTDETECT_OBJS) $(LDLIBS)

# test_aspectscan: end-to-end acceptance for TTAspectScanTask on real material.
# Pulls in the search-task base (which references TTMpeg2Decoder) and therefore
# libmpeg2.
#
# Deviation from the brief's list: ttavtypes.o was dropped and ttheaderlist.o
# added. ttavtypes.cpp is a factory (TTAudioType/TTVideoType::create*Stream())
# that #includes every concrete AV stream header, so pulling in ttavtypes.o
# drags in TTAC3AudioStream, TTMPEGAudioStream, TTMpeg2VideoStream,
# TTH264VideoStream, TTH265VideoStream and TTSrtSubtitleStream (and their own
# transitive dependents) purely for its unused factory code - the only symbol
# this link set actually references from TTAVTypes is the AVStreamType enum
# (a compile-time value, no object code). Verified via
# `nm -u` on every other object in this list: the sole TTAVTypes-related
# undefined symbol is the TTSearchTask constructor's own parameter type.
# ttheaderlist.o is added instead because ttvideoheaderlist.o (already listed)
# needs TTHeaderList's out-of-line members.
ASPECTSCAN_OBJS := ../../obj/ttsearchtask_aspectscan.o \
                   ../../obj/moc_ttsearchtask_aspectscan.o \
                   ../../obj/ttsearchtask.o ../../obj/moc_ttsearchtask.o \
                   ../../obj/ttaspectdetect.o \
                   ../../obj/ttstreampoint.o \
                   ../../obj/ttthreadtask.o ../../obj/moc_ttthreadtask.o \
                   ../../obj/ttvideoindexlist.o ../../obj/ttvideoheaderlist.o \
                   ../../obj/ttheaderlist.o \
                   ../../obj/ttavheader.o ../../obj/ttmpeg2videoheader.o \
                   ../../obj/ttexception.o \
                   ../../obj/ttfilebuffer.o ../../obj/ttcommon.o \
                   ../../obj/istatusreporter.o ../../obj/moc_istatusreporter.o \
                   ../../obj/ttmpeg2decoder.o \
                   $(WRAPPER_OBJS)

test_aspectscan: test_aspectscan.cpp $(ASPECTSCAN_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(ASPECTSCAN_OBJS) $(LDLIBS) $(AVLIBS) -lmpeg2 -lmpeg2convert

test_startcode_scan: test_startcode_scan.cpp $(FILEBUF_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(FILEBUF_OBJS) $(LDLIBS)

test_esinfo: test_esinfo.cpp $(ESINFO_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(ESINFO_OBJS) $(LDLIBS)

test_hevc_seam: test_hevc_seam.cpp $(HEVCSEAM_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

test_audiocut: test_audiocut.cpp $(WRAPPER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(WRAPPER_OBJS) $(LDLIBS) $(AVLIBS)

test_stilldisplay: test_stilldisplay.cpp $(WRAPPER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(WRAPPER_OBJS) $(LDLIBS) $(AVLIBS)

test_rawmap: test_rawmap.cpp $(WRAPPER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(WRAPPER_OBJS) $(LDLIBS) $(AVLIBS)

dump_fieldcoded: dump_fieldcoded.cpp $(WRAPPER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(WRAPPER_OBJS) $(LDLIBS) $(AVLIBS)

test_adopt_paff: test_adopt_paff.cpp $(WRAPPER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(WRAPPER_OBJS) $(LDLIBS) $(AVLIBS)

test_stillframe: test_stillframe.cpp $(STILLFRAME_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(STILLFRAME_OBJS) $(LDLIBS) $(AVLIBS)

# test_smartcut_seam links the wrapper too (injectmap mode mirrors the app's
# display-map injection, required for PAFF ES). Union of STILLFRAME/WRAPPER
# objects without duplicates:
SEAM_OBJS := ../../obj/ttessmartcut.o ../../obj/moc_ttessmartcut.o \
             ../../obj/tthevcseam.o \
             ../../obj/ttffmpegwrapper.o ../../obj/moc_ttffmpegwrapper.o \
             ../../obj/ttsettings.o ../../obj/moc_ttsettings.o \
             ../../obj/ttesinfo.o ../../obj/ttnaluparser.o \
             ../../obj/ttdisplayordermap.o ../../obj/ttmessagelogger.o

test_smartcut_seam: test_smartcut_seam.cpp $(SEAM_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(SEAM_OBJS) $(LDLIBS) $(AVLIBS)

# test_mkvmux: smart-cut + app-faithful MKV mux (defect A: seam PTS analysis)
MKVMUX_OBJS := $(SEAM_OBJS) ../../obj/ttmkvmergeprovider.o \
               ../../obj/moc_ttmkvmergeprovider.o
test_mkvmux: test_mkvmux.cpp $(MKVMUX_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(MKVMUX_OBJS) $(LDLIBS) $(AVLIBS)

# test_feed_decode isolates the smart-cut decoder feed: parser AUs are fed to
# a decoder configured like TTESSmartCut::setupDecoder and every output
# frame's luma mean is printed (flat 128.0 = libav gray fill).
test_feed_decode: test_feed_decode.cpp $(NALU_FULL_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(NALU_FULL_OBJS) $(LDLIBS) $(AVLIBS)

# test_sar checks TTFFmpegWrapper::sampleAspectRatio on real material
# (Befund D acceptance: still-frame aspect correction).
test_sar: test_sar.cpp $(WRAPPER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(WRAPPER_OBJS) $(LDLIBS) $(AVLIBS)

test_segshape: test_segshape.cpp $(STILLFRAME_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(STILLFRAME_OBJS) $(LDLIBS) $(AVLIBS)

$(NALU_OBJ):
	@echo "Missing $(NALU_OBJ) — run \`make\` in the repo root first." >&2
	@exit 1

$(DISPMAP_OBJS):
	@echo "Missing $@ — run \`make\` in the repo root first." >&2
	@exit 1

clean:
	rm -f $(TOOLS)

# The MPEG-2 cut path (TTMpeg2VideoStream::cut + TTTranscodeProvider + the
# libmpeg2 decoder) needs exactly these objects — determined by linking the
# tools against an archive of every non-GUI object and reading which members the
# linker actually pulled. It pulls NO GUI/mpv objects, so the tools link against
# Qt5Core/Qt5Gui only (not Widgets/OpenGL/mpv). Curated instead of the old
# link-everything glob so a new GUI/mpv dependency in the cut path surfaces as
# an undefined-reference link error here instead of being silently masked.
MPEG2CUT_OBJS := ../../obj/istatusreporter.o ../../obj/moc_istatusreporter.o \
                 ../../obj/moc_ttavstream.o ../../obj/moc_ttmpeg2videostream.o \
                 ../../obj/moc_ttsettings.o ../../obj/moc_tttranscode.o \
                 ../../obj/ttaudioheaderlist.o ../../obj/ttavheader.o \
                 ../../obj/ttavstream.o ../../obj/ttcommon.o \
                 ../../obj/ttcutparameter.o ../../obj/ttexception.o \
                 ../../obj/ttfilebuffer.o ../../obj/ttheaderlist.o \
                 ../../obj/ttmessagelogger.o ../../obj/ttmpeg2decoder.o \
                 ../../obj/ttmpeg2videoheader.o ../../obj/ttmpeg2videostream.o \
                 ../../obj/ttsettings.o ../../obj/tttranscode.o \
                 ../../obj/ttvideoheaderlist.o ../../obj/ttvideoindexlist.o
MPEG2CUT_PKGS := Qt5Core Qt5Gui \
                 libavformat libavcodec libavutil libswscale libavfilter libswresample

test_mpeg2_cutout: test_mpeg2_cutout.cpp
	@test -f ../../obj/ttmpeg2videostream.o || { echo "Missing ../../obj/*.o — run \`make\` in the repo root first." >&2; exit 1; }
	$(CXX) -O2 -Wall -fPIC -std=gnu++17 -I../.. $(shell pkg-config --cflags $(MPEG2CUT_PKGS)) \
	    -o $@ $< $(MPEG2CUT_OBJS) $(shell pkg-config --libs $(MPEG2CUT_PKGS)) \
	    -lmpeg2 -lmpeg2convert -lpthread

# test_extra_index_rank: measures whether the field-pair extra indices
# (stored in bitstream order) drift against the display positions they are
# compared with in TTAVData::countExtraFramesBefore().
test_extra_index_rank: test_extra_index_rank.cpp
	@test -f ../../obj/ttmpeg2videostream.o || { echo "Missing ../../obj/*.o — run \`make\` in the repo root first." >&2; exit 1; }
	$(CXX) -O2 -Wall -fPIC -std=gnu++17 -I../.. $(shell pkg-config --cflags $(MPEG2CUT_PKGS)) \
	    -o $@ $< $(MPEG2CUT_OBJS) $(shell pkg-config --libs $(MPEG2CUT_PKGS)) \
	    -lmpeg2 -lmpeg2convert -lpthread

dump_mpeg2_fields: dump_mpeg2_fields.cpp
	@test -f ../../obj/ttmpeg2videostream.o || { echo "Missing ../../obj/*.o — run \`make\` in the repo root first." >&2; exit 1; }
	$(CXX) -O2 -Wall -fPIC -std=gnu++17 -I../.. $(shell pkg-config --cflags $(MPEG2CUT_PKGS)) \
	    -o $@ $< $(MPEG2CUT_OBJS) $(shell pkg-config --libs $(MPEG2CUT_PKGS)) \
	    -lmpeg2 -lmpeg2convert -lpthread

# test_aspectscan_mpeg2: MPEG-2 counterpart to test_aspectscan. Builds real
# header/index lists via TTMpeg2VideoStream (same objects as MPEG2CUT_OBJS,
# the curated MPEG-2 cut-path link set above) plus the TTAspectScanTask/
# TTSearchTask objects. Only the objects MPEG2CUT_OBJS doesn't already
# provide are added below (ttsettings.o/moc, ttmessagelogger.o etc. are
# shared - listing them twice would duplicate-define their symbols).
MPEG2ASPECTSCAN_OBJS := $(MPEG2CUT_OBJS) \
                 ../../obj/ttsearchtask_aspectscan.o ../../obj/moc_ttsearchtask_aspectscan.o \
                 ../../obj/ttsearchtask.o ../../obj/moc_ttsearchtask.o \
                 ../../obj/ttaspectdetect.o ../../obj/ttstreampoint.o \
                 ../../obj/ttthreadtask.o ../../obj/moc_ttthreadtask.o \
                 ../../obj/ttdisplayordermap.o \
                 ../../obj/ttffmpegwrapper.o ../../obj/moc_ttffmpegwrapper.o \
                 ../../obj/ttesinfo.o ../../obj/ttnaluparser.o

test_aspectscan_mpeg2: test_aspectscan_mpeg2.cpp $(MPEG2ASPECTSCAN_OBJS)
	@test -f ../../obj/ttmpeg2videostream.o || { echo "Missing ../../obj/*.o — run \`make\` in the repo root first." >&2; exit 1; }
	$(CXX) -O2 -Wall -fPIC -std=gnu++17 -I../.. $(shell pkg-config --cflags $(MPEG2CUT_PKGS)) \
	    -o $@ $< $(MPEG2ASPECTSCAN_OBJS) $(shell pkg-config --libs $(MPEG2CUT_PKGS)) \
	    -lmpeg2 -lmpeg2convert -lpthread

# test_streampoint_order: compares the bitstream-order picture counter that
# TTStreamPointVideoWorker reports against the display-order position the
# navigation actually uses, then runs the real worker as a gate. Needs the
# MPEG-2 header/index machinery plus the worker itself.
STREAMPOINTORDER_OBJS := $(MPEG2CUT_OBJS) \
                 ../../obj/ttstreampoint_videoworker.o \
                 ../../obj/moc_ttstreampoint_videoworker.o \
                 ../../obj/ttstreampoint.o \
                 ../../obj/ttthreadtask.o ../../obj/moc_ttthreadtask.o

test_streampoint_order: test_streampoint_order.cpp $(STREAMPOINTORDER_OBJS)
	@test -f ../../obj/ttmpeg2videostream.o || { echo "Missing ../../obj/*.o — run \`make\` in the repo root first." >&2; exit 1; }
	$(CXX) -O2 -Wall -fPIC -std=gnu++17 -I../.. $(shell pkg-config --cflags $(MPEG2CUT_PKGS)) \
	    -o $@ $< $(STREAMPOINTORDER_OBJS) $(shell pkg-config --libs $(MPEG2CUT_PKGS)) \
	    -lmpeg2 -lmpeg2convert -lpthread

MKVORDER_OBJS := ../../obj/ttmkvmergeprovider.o ../../obj/moc_ttmkvmergeprovider.o \
    ../../obj/ttcut.o $(WRAPPER_OBJS)

test_mpeg2order: test_mpeg2order.cpp $(MKVORDER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(MKVORDER_OBJS) $(LDLIBS) -lQt5Widgets $(AVLIBS)

test_cutprogress: test_cutprogress.cpp $(STILLFRAME_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(STILLFRAME_OBJS) $(LDLIBS) $(AVLIBS)

# test_mpeg2_seek: drives the MPEG-2 still-image path (moveToIndexPos +
# TTMpeg2Decoder::moveToFrameIndex) for a list of positions and reports the
# returned picture's checksum - the headless view of "the image does not
# change when I jump to a marker".
test_mpeg2_seek: test_mpeg2_seek.cpp $(MPEG2CUT_OBJS)
	@test -f ../../obj/ttmpeg2videostream.o || { echo "Missing ../../obj/*.o — run \`make\` in the repo root first." >&2; exit 1; }
	$(CXX) -O2 -Wall -fPIC -std=gnu++17 -I../.. $(shell pkg-config --cflags $(MPEG2CUT_PKGS)) \
	    -o $@ $< $(MPEG2CUT_OBJS) $(shell pkg-config --libs $(MPEG2CUT_PKGS)) \
	    -lmpeg2 -lmpeg2convert -lpthread

# test_window_jump: same jump as test_mpeg2_seek, but through the real display
# widget (TTMPEG2Window2), so the pixmap it produces can be compared. Needs
# Qt5Widgets and the widget's own dependencies; run with
# QT_QPA_PLATFORM=offscreen.
WINDOWJUMP_OBJS := $(MPEG2CUT_OBJS) \
                 ../../obj/ttmpeg2window2.o ../../obj/moc_ttmpeg2window2.o \
                 ../../obj/ttcut.o \
                 ../../obj/ttffmpegwrapper.o ../../obj/moc_ttffmpegwrapper.o \
                 ../../obj/ttdisplayordermap.o ../../obj/ttesinfo.o \
                 ../../obj/ttnaluparser.o ../../obj/ttsubtitleheaderlist.o \
                 ../../obj/tth26xvideostream.o ../../obj/moc_tth26xvideostream.o

test_window_jump: test_window_jump.cpp $(WINDOWJUMP_OBJS)
	@test -f ../../obj/ttmpeg2window2.o || { echo "Missing ../../obj/*.o — run \`make\` in the repo root first." >&2; exit 1; }
	$(CXX) -O2 -Wall -fPIC -std=gnu++17 -I../.. -I../../ui_h \
	    $(shell pkg-config --cflags $(MPEG2CUT_PKGS) Qt5Widgets) \
	    -o $@ $< $(WINDOWJUMP_OBJS) \
	    $(shell pkg-config --libs $(MPEG2CUT_PKGS) Qt5Widgets) \
	    -lmpeg2 -lmpeg2convert -lpthread
