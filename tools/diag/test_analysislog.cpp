// Acceptance harness for TTAnalysisLog. Pure data in, pure verdict out —
// no GUI, no task pool, no video file.
// Build via `cmake --build build --target test_analysislog`.
#include <QCoreApplication>
#include <QStringList>
#include <cstdio>

#include "data/ttanalysislog.h"

static int gFailures = 0;

static void check(bool ok, const char* what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) gFailures++;
}

static void testCap()
{
    QStringList out;
    TTAnalysisLog log([&out](const QString& s) { out.append(s); }, 20);

    for (int i = 0; i < 25; ++i)
        log.event(QString("event %1").arg(i));

    check(out.size() == 20, "25 events with cap 20 -> 20 lines");
    check(log.suppressed() == 5, "25 events with cap 20 -> suppressed() == 5");
    check(out.first() == "event 0", "the FIRST events survive, not the last");
    check(out.last() == "event 19", "line 20 is the last one kept");
}

static void testLineIsNotCapped()
{
    QStringList out;
    TTAnalysisLog log([&out](const QString& s) { out.append(s); }, 2);

    log.event("a"); log.event("b"); log.event("c");   // 3rd is swallowed
    log.line("summary");

    check(out.size() == 3, "line() passes the cap (2 events + 1 line)");
    check(out.last() == "summary", "line() text arrives verbatim");
    check(log.suppressed() == 1, "only event() counts as suppressed");
}

static void testResetCap()
{
    QStringList out;
    TTAnalysisLog log([&out](const QString& s) { out.append(s); }, 1);

    log.event("section1-a");
    log.event("section1-b");     // swallowed
    log.resetCap();
    log.event("section2-a");     // must pass again

    check(out.size() == 2, "resetCap() opens the cap for the next section");
    check(out.last() == "section2-a", "the second section's first event arrives");
    check(log.suppressed() == 0, "resetCap() also clears the suppressed counter");
}

static void testFormatPosition()
{
    check(ttFormatStreamPosition(49719, 50.0f) == QString("00:16:34 (frame 49719)"),
          "50 fps: frame 49719 -> 00:16:34");
    check(ttFormatStreamPosition(0, 50.0f) == QString("00:00:00 (frame 0)"),
          "frame 0 -> 00:00:00");
    check(ttFormatStreamPosition(1234, 0.0f) == QString("frame 1234"),
          "frame rate 0 -> no invented time");
    check(ttFormatStreamPosition(1234, -1.0f) == QString("frame 1234"),
          "negative frame rate -> no invented time");
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    testCap();
    testLineIsNotCapped();
    testResetCap();
    testFormatPosition();

    printf("%s\n", gFailures == 0 ? "ALL PASS" : "FAILURES");
    return gFailures == 0 ? 0 : 1;
}
