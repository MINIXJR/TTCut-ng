// Acceptance harness for the aspect-format classifier and its hysteresis.
// Pure data in, pure verdict out — no decoder, no video file, no threading.
// Build via `make test_aspectdetect` in tools/diag.
#include <QCoreApplication>
#include <QImage>
#include <cstdio>

#include "data/ttaspectdetect.h"

static int gFailures = 0;

static void check(bool ok, const char* what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) gFailures++;
}

// Build a 1280x720 grayscale frame: `barWidth` black columns on each side,
// the rest filled with `centreLuma`.
static QImage makeFrame(int barWidth, int centreLuma)
{
    QImage img(1280, 720, QImage::Format_Grayscale8);
    img.fill(0);
    for (int row = 0; row < img.height(); ++row) {
        uchar* line = img.scanLine(row);
        for (int col = barWidth; col < img.width() - barWidth; ++col)
            line[col] = (uchar)centreLuma;
    }
    return img;
}

static void testClassifier()
{
    // 160 px bars, bright centre: the reported material's geometry.
    check(classifyAspectSample(makeFrame(160, 128), 20) == TTAspectSample::Pillarbox,
          "160 px bars with bright centre -> Pillarbox");

    // No bars at all.
    check(classifyAspectSample(makeFrame(0, 128), 20) == TTAspectSample::NoPillarbox,
          "full-width picture -> NoPillarbox");

    // Bars below the 10 % minimum (128 px for 1280).
    check(classifyAspectSample(makeFrame(100, 128), 20) == TTAspectSample::NoPillarbox,
          "100 px bars (below 10 % minimum) -> NoPillarbox");

    // Completely black frame: bars meet in the middle, centre is black.
    check(classifyAspectSample(makeFrame(640, 0), 20) == TTAspectSample::NoStatement,
          "all-black frame -> NoStatement");

    // Wide bars but a dark centre — still a black frame, no aspect information.
    check(classifyAspectSample(makeFrame(160, 5), 20) == TTAspectSample::NoStatement,
          "160 px bars with black centre -> NoStatement");

    // A null image cannot be classified.
    check(classifyAspectSample(QImage(), 20) == TTAspectSample::NoStatement,
          "null image -> NoStatement");
}

static void testHysteresis()
{
    // 500 frames of hysteresis == 10 s at 50 fps.
    {
        TTAspectHysteresis h(500);
        TTAspectTransition t{};
        bool fired = false;

        // Baseline: NoPillarbox samples every 50 frames up to 950.
        for (int pos = 0; pos <= 950; pos += 50)
            if (h.feed(pos, TTAspectSample::NoPillarbox, t)) fired = true;
        check(!fired, "constant baseline reports no transition");

        // Pillarbox from frame 1000 on.
        int firedAt = -1;
        for (int pos = 1000; pos <= 2000; pos += 50) {
            if (h.feed(pos, TTAspectSample::Pillarbox, t)) { firedAt = pos; break; }
        }
        check(firedAt == 1500, "transition confirmed 500 frames after the run started");
        check(t.firstFrame == 1000, "reported position is the first frame of the run");
        check(t.toPillarbox, "direction is 16:9 -> 4:3pb");
    }

    // A short flicker must not confirm anything.
    {
        TTAspectHysteresis h(500);
        TTAspectTransition t{};
        bool fired = false;
        for (int pos = 0; pos <= 950; pos += 50)
            if (h.feed(pos, TTAspectSample::NoPillarbox, t)) fired = true;
        if (h.feed(1000, TTAspectSample::Pillarbox, t)) fired = true;
        for (int pos = 1050; pos <= 2000; pos += 50)
            if (h.feed(pos, TTAspectSample::NoPillarbox, t)) fired = true;
        check(!fired, "single-sample flicker reports no transition");
    }

    // NoStatement samples are ignored: they neither reset nor advance the run.
    {
        TTAspectHysteresis h(500);
        TTAspectTransition t{};
        for (int pos = 0; pos <= 950; pos += 50) h.feed(pos, TTAspectSample::NoPillarbox, t);
        h.feed(1000, TTAspectSample::Pillarbox, t);
        for (int pos = 1050; pos <= 1450; pos += 50) h.feed(pos, TTAspectSample::NoStatement, t);
        bool fired = h.feed(1500, TTAspectSample::Pillarbox, t);
        check(fired && t.firstFrame == 1000,
              "NoStatement samples do not reset the candidate run");
    }

    // The very first usable sample only establishes the baseline.
    {
        TTAspectHysteresis h(500);
        TTAspectTransition t{};
        bool fired = false;
        for (int pos = 0; pos <= 2000; pos += 50)
            if (h.feed(pos, TTAspectSample::Pillarbox, t)) fired = true;
        check(!fired, "initial state is not reported as a transition");
    }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    testClassifier();
    testHysteresis();
    printf("%s (%d failures)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures);
    return gFailures == 0 ? 0 : 1;
}
