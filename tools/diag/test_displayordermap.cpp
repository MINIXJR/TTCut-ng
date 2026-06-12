// test_displayordermap — unit harness for the DPB-bumping display-rank
// algorithm with synthetic POC sequences. Exits 0 on success, 1 on failure.
//
// Build:
//   g++ -O2 -std=gnu++17 -fPIC -I../.. $(pkg-config --cflags Qt5Core) \
//       -o test_displayordermap test_displayordermap.cpp \
//       ../../obj/ttdisplayordermap.o ../../obj/ttmessagelogger.o \
//       $(pkg-config --libs Qt5Core) -lpthread

#include <cstdio>
#include "../../avstream/ttdisplayordermap.h"

static int failures = 0;

static void check(const char* name, const QVector<int>& got, const QVector<int>& expected)
{
    if (got == expected) { printf("PASS  %s\n", name); return; }
    failures++;
    printf("FAIL  %s\n  got:      ", name);
    for (int v : got) printf("%d ", v);
    printf("\n  expected: ");
    for (int v : expected) printf("%d ", v);
    printf("\n");
}

int main()
{
    // Case 1: identity — no reorder (poc strictly increasing, e.g. P-only)
    {
        QVector<TTPocEntry> e;
        for (int i = 0; i < 6; ++i) e.append({2 * i, i == 0});
        check("identity", TTDisplayOrderMap::displayRanksFromPoc(e),
              {0, 1, 2, 3, 4, 5});
    }

    // Case 2: classic IBBP — decode I0 P3 B1 B2 P6 B4 B5 (poc = 2*display)
    // decode order:   I(0) P(6) B(2) B(4) P(12) B(8) B(10)
    // display ranks:  0    3    1    2    6     4    5
    {
        QVector<TTPocEntry> e = {
            {0, true}, {6, false}, {2, false}, {4, false},
            {12, false}, {8, false}, {10, false},
        };
        check("IBBP", TTDisplayOrderMap::displayRanksFromPoc(e),
              {0, 3, 1, 2, 6, 4, 5});
    }

    // Case 3: IDR flush with POC reset — second IDR restarts poc at 0.
    // decode: I(0) P(4) B(2) | IDR(0) P(4) B(2)
    // ranks:  0    2    1      3      5    4
    {
        QVector<TTPocEntry> e = {
            {0, true}, {4, false}, {2, false},
            {0, true}, {4, false}, {2, false},
        };
        check("IDR flush", TTDisplayOrderMap::displayRanksFromPoc(e),
              {0, 2, 1, 3, 5, 4});
    }

    // Case 4: open-GOP — non-IDR I with leading B that displays BEFORE it.
    // decode: I(0) P(4) B(2) I(8) B(6)   — B(6) follows I(8) in decode but
    // ranks:  0    2    1    4    3        displays before it. NO flush at I(8).
    {
        QVector<TTPocEntry> e = {
            {0, true}, {4, false}, {2, false}, {8, false}, {6, false},
        };
        check("open GOP", TTDisplayOrderMap::displayRanksFromPoc(e),
              {0, 2, 1, 4, 3});
    }

    // Case 5: MBAFF fixture window (real POCs from MBAFF.264 AU 36378-36392,
    // measured 2026-06-12; relative ranks must reproduce the measured display
    // order).
    // decode AUs:  I(3344) B(3340) B(3336) B(3338) B(3342) P(3350) B(3346)
    //              B(3348) I(3352) P(3360) B(3356) B(3354) B(3358)
    // local ranks: 4       2       0       1       3       7       5
    //              6       8       12      10      9       11
    {
        QVector<TTPocEntry> e = {
            {3344, false}, {3340, false}, {3336, false}, {3338, false},
            {3342, false}, {3350, false}, {3346, false}, {3348, false},
            {3352, false}, {3360, false}, {3356, false}, {3354, false},
            {3358, false},
        };
        check("MBAFF window", TTDisplayOrderMap::displayRanksFromPoc(e),
              {4, 2, 0, 1, 3, 7, 5, 6, 8, 12, 10, 9, 11});
    }

    // Case 6: build() must produce inverse mapping displayToDecode
    {
        TTDisplayOrderMap map;
        QVector<TTPocEntry> e = {
            {0, true}, {6, false}, {2, false}, {4, false},
        };
        map.build(e);
        bool ok = map.isValid() && map.count() == 4
               && map.decodeToDisplay(1) == 3
               && map.displayToDecode(3) == 1
               && map.displayToDecode(0) == 0
               && map.displayToDecode(1) == 2;
        if (ok) printf("PASS  build/inverse\n");
        else { failures++; printf("FAIL  build/inverse\n"); }
    }

    // Case 7: buildFromRanks rejects malformed input (duplicate + OOB rank)
    {
        TTDisplayOrderMap dup;
        dup.buildFromRanks({0, 0, 2, 3});
        TTDisplayOrderMap oob;
        oob.buildFromRanks({0, 1, 4, 3});
        if (!dup.isValid() && !oob.isValid())
            printf("PASS  buildFromRanks/reject\n");
        else { failures++; printf("FAIL  buildFromRanks/reject\n"); }
    }

    printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
