// test_wrapper_map — verifies the display-order map against empirically
// measured ground truth (test_parser_poc run, 2026-06-12).
// Part 1 (Task 2): TTDisplayOrderMap::buildFromFile on MBAFF.264.
// Part 2 (Task 3) adds TTFFmpegWrapper integration asserts.
//
// Build:
//   g++ -O2 -std=gnu++17 -fPIC -I../.. $(pkg-config --cflags Qt5Core) \
//       -o test_wrapper_map test_wrapper_map.cpp \
//       ../../obj/ttdisplayordermap.o ../../obj/ttmessagelogger.o \
//       $(pkg-config --libs Qt5Core) -lpthread \
//       -lavformat -lavcodec -lavutil

#include <cstdio>
#include "../../avstream/ttdisplayordermap.h"

static int failures = 0;

static void expectEq(const char* name, int got, int expected)
{
    if (got == expected) { printf("PASS  %s = %d\n", name, got); return; }
    failures++;
    printf("FAIL  %s: got %d, expected %d\n", name, got, expected);
}

int main(int argc, char** argv)
{
    const char* mbaff = (argc > 1) ? argv[1]
        : "/media/Daten/Video_Tmp/ProjectX_Temp/MBAFF.264";

    TTDisplayOrderMap map = TTDisplayOrderMap::buildFromFile(mbaff);
    if (!map.isValid()) { printf("FAIL  buildFromFile invalid\n"); return 1; }

    expectEq("count", map.count(), 162530);
    // Ground truth from full-file decode comparison (0 mismatches):
    expectEq("displayToDecode(36384)", map.displayToDecode(36384), 36385);
    expectEq("displayToDecode(36386)", map.displayToDecode(36386), 36386);
    expectEq("displayToDecode(36385)", map.displayToDecode(36385), 36383);
    expectEq("decodeToDisplay(36378)", map.decodeToDisplay(36378), 36382);
    expectEq("decodeToDisplay(36385)", map.decodeToDisplay(36385), 36384);
    // Round-trip over a window
    for (int d = 36000; d < 36800; ++d)
        if (map.decodeToDisplay(map.displayToDecode(d)) != d) {
            failures++;
            printf("FAIL  round-trip at display %d\n", d);
            break;
        }

    printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
