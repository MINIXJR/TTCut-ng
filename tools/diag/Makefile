# Diagnostic tools for TTCut-ng. Built on demand via `make diag` from the
# repo root, or directly via `make` here. Each tool links against
# obj/ttnaluparser.o produced by the main build, so a regular `make` must
# have run at least once. Sources stay tracked, binaries are gitignored.

QT_INC := -I/usr/include/x86_64-linux-gnu/qt5 \
          -I/usr/include/x86_64-linux-gnu/qt5/QtCore
QT_LIB := -lQt5Core

NALU_OBJ := ../../obj/ttnaluparser.o

CXXFLAGS ?= -O2 -Wall -fPIC -std=gnu++17 -I../.. $(QT_INC)
LDLIBS   := $(QT_LIB) -lpthread

TOOLS := check_idr test_nalu_parser test_au_types

.PHONY: all clean
all: $(TOOLS)

$(TOOLS): %: %.cpp $(NALU_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $< $(NALU_OBJ) $(LDLIBS)

$(NALU_OBJ):
	@echo "Missing $(NALU_OBJ) — run \`make\` in the repo root first." >&2
	@exit 1

clean:
	rm -f $(TOOLS)
