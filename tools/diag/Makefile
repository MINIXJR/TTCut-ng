# Diagnostic tools for TTCut-ng. Built on demand via `make diag` from the
# repo root, or directly via `make` here. Each tool links against
# obj/ttnaluparser.o produced by the main build, so a regular `make` must
# have run at least once. Sources stay tracked, binaries are gitignored.

QT_INC := $(shell pkg-config --cflags Qt5Core)
QT_LIB := $(shell pkg-config --libs Qt5Core)

NALU_OBJ      := ../../obj/ttnaluparser.o
DISPMAP_OBJS  := ../../obj/ttdisplayordermap.o ../../obj/ttmessagelogger.o

CXXFLAGS ?= -O2 -Wall -fPIC -std=gnu++17 -I../.. $(QT_INC)
LDLIBS   := $(QT_LIB) -lpthread

TOOLS := check_idr test_nalu_parser test_au_types test_displayordermap

.PHONY: all clean
all: $(TOOLS)

check_idr test_nalu_parser test_au_types: %: %.cpp $(NALU_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $< $(NALU_OBJ) $(LDLIBS)

test_displayordermap: test_displayordermap.cpp $(DISPMAP_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(DISPMAP_OBJS) $(LDLIBS)

$(NALU_OBJ):
	@echo "Missing $(NALU_OBJ) — run \`make\` in the repo root first." >&2
	@exit 1

$(DISPMAP_OBJS):
	@echo "Missing $@ — run \`make\` in the repo root first." >&2
	@exit 1

clean:
	rm -f $(TOOLS)
