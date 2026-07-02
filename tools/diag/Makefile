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
                   ../../obj/ttsettings.o ../../obj/moc_ttsettings.o \
                   ../../obj/ttesinfo.o ../../obj/ttnaluparser.o \
                   ../../obj/ttdisplayordermap.o ../../obj/ttmessagelogger.o

CXXFLAGS ?= -O2 -Wall -fPIC -std=gnu++17 -I../.. $(QT_INC)
LDLIBS   := $(QT_LIB) -lpthread

AVLIBS   := -lavformat -lavcodec -lavutil -lswscale -lswresample

TOOLS := check_idr test_nalu_parser test_au_types test_displayordermap test_wrapper_map test_stilldisplay test_leadingclass

NALU_FULL_OBJS := $(NALU_OBJ) ../../obj/ttsettings.o ../../obj/moc_ttsettings.o ../../obj/ttmessagelogger.o

.PHONY: all clean
all: $(TOOLS)

check_idr test_nalu_parser test_au_types: %: %.cpp $(NALU_FULL_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(NALU_FULL_OBJS) $(LDLIBS)

test_leadingclass: test_leadingclass.cpp $(DISPMAP_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(DISPMAP_OBJS) $(LDLIBS) $(AVLIBS)

test_displayordermap: test_displayordermap.cpp $(DISPMAP_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(DISPMAP_OBJS) $(LDLIBS) $(AVLIBS)

test_wrapper_map: test_wrapper_map.cpp $(WRAPPER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(WRAPPER_OBJS) $(LDLIBS) $(AVLIBS)

test_stilldisplay: test_stilldisplay.cpp $(WRAPPER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(WRAPPER_OBJS) $(LDLIBS) $(AVLIBS)

test_stillframe: test_stillframe.cpp $(STILLFRAME_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(STILLFRAME_OBJS) $(LDLIBS) $(AVLIBS)

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
