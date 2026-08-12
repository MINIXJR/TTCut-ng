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

// Build a 1280x720 grayscale frame: `leftBar` black columns on the left,
// `rightBar` on the right, the rest filled with `centreLuma`.
static QImage makeFrameLR(int leftBar, int rightBar, int centreLuma)
{
    QImage img(1280, 720, QImage::Format_Grayscale8);
    img.fill(0);
    for (int row = 0; row < img.height(); ++row) {
        uchar* line = img.scanLine(row);
        for (int col = leftBar; col < img.width() - rightBar; ++col)
            line[col] = (uchar)centreLuma;
    }
    return img;
}

// Symmetric shorthand: the same bar width on both sides.
static QImage makeFrame(int barWidth, int centreLuma)
{
    return makeFrameLR(barWidth, barWidth, centreLuma);
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

    // --- plausible bar width ----------------------------------------------
    // Nominal 4:3-in-16:9 bar at 1280 wide is w/8 = 160 px; the limit is
    // 1.5x that = 240 px. Values measured on two recordings, see
    // classifyAspectSample().

    // Real geometry from "1994x05_-_Flemming_...", bars 3 px apart.
    check(classifyAspectSample(makeFrameLR(177, 174, 128), 20) == TTAspectSample::Pillarbox,
          "177/174 px bars (real 4:3 section) -> Pillarbox");

    // Genuine bleed inside a real 4:3 segment: left bar stays put, dark
    // picture content widens the right one. Must still count as pillarbox.
    check(classifyAspectSample(makeFrameLR(161, 212, 128), 20) == TTAspectSample::Pillarbox,
          "161/212 px bars (measured bleed in a real segment) -> Pillarbox");

    // Exactly at the limit, and one past it.
    check(classifyAspectSample(makeFrameLR(161, 240, 128), 20) == TTAspectSample::Pillarbox,
          "161/240 px bars (bar == limit) -> Pillarbox");
    check(classifyAspectSample(makeFrameLR(161, 241, 128), 20) == TTAspectSample::NoStatement,
          "161/241 px bars (one past the limit) -> NoStatement");

    // The night-scene frames that produced the false 4:3 segment. Both bars
    // clear the 10 % minimum, so only the width limit rejects them.
    check(classifyAspectSample(makeFrameLR(137, 276, 128), 20) == TTAspectSample::NoStatement,
          "137/276 px bars (dark scene, narrowest offending bar) -> NoStatement");
    check(classifyAspectSample(makeFrameLR(318, 211, 128), 20) == TTAspectSample::NoStatement,
          "318/211 px bars (dark scene) -> NoStatement");
    check(classifyAspectSample(makeFrameLR(167, 384, 128), 20) == TTAspectSample::NoStatement,
          "167/384 px bars (dark scene) -> NoStatement");
    check(classifyAspectSample(makeFrameLR(391, 364, 128), 20) == TTAspectSample::NoStatement,
          "391/364 px bars (dark scene) -> NoStatement");
    check(classifyAspectSample(makeFrameLR(544, 185, 128), 20) == TTAspectSample::NoStatement,
          "544/185 px bars (dark scene) -> NoStatement");

    // An over-wide bar must NOT break a candidate run: the same bleed occurs
    // inside genuine 4:3 material (175/472 measured), where treating it as
    // NoPillarbox tore one segment into three.
    check(classifyAspectSample(makeFrameLR(175, 472, 128), 20) == TTAspectSample::NoStatement,
          "175/472 px bars (bleed inside a real segment) -> NoStatement, not NoPillarbox");
}

static void testReasons()
{
    TTAspectReason why = TTAspectReason::None;

    // Genuine pillarbox: no rejection reason.
    check(classifyAspectSample(makeFrame(160, 128), 20, &why) == TTAspectSample::Pillarbox
          && why == TTAspectReason::None,
          "pillarbox -> reason None");

    // No bars at all.
    check(classifyAspectSample(makeFrame(0, 128), 20, &why) == TTAspectSample::NoPillarbox
          && why == TTAspectReason::NoBars,
          "no bars -> reason NoBars");

    // Measured on a real recording: dark night scene, both bars beyond the
    // 1.5x limit (maxBar = 1280*3/16 = 240).
    check(classifyAspectSample(makeFrameLR(318, 211, 128), 20, &why) == TTAspectSample::NoStatement
          && why == TTAspectReason::BarsTooWide,
          "318/211 px bars -> reason BarsTooWide");

    // Valid bar geometry, but the centre is as dark as a black frame.
    check(classifyAspectSample(makeFrame(160, 20), 20, &why) == TTAspectSample::NoStatement
          && why == TTAspectReason::CentreTooDark,
          "valid bars with dark centre -> reason CentreTooDark");

    // Null image carries nothing at all.
    check(classifyAspectSample(QImage(), 20, &why) == TTAspectSample::NoStatement
          && why == TTAspectReason::Unusable,
          "null image -> reason Unusable");

    // The pointer stays optional: the existing call form must still compile
    // and behave identically.
    check(classifyAspectSample(makeFrame(160, 128), 20) == TTAspectSample::Pillarbox,
          "two-argument call form still works");
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

static void testDiscardedCandidates()
{
    // 500 frames of hysteresis == 10 s at 50 fps, as in the shipped scan.
    TTAspectHysteresis h(500);
    TTAspectTransition t{};
    TTAspectCandidate  c{};

    // Baseline: 16:9 confirmed.
    h.feed(0, TTAspectSample::NoPillarbox, t);
    check(!h.takeDiscardedCandidate(c),
          "baseline sample discards nothing");

    // A 4:3pb run that dies after 200 frames (4 s) - too short for 500.
    h.feed(1000, TTAspectSample::Pillarbox, t);
    h.feed(1100, TTAspectSample::Pillarbox, t);
    h.feed(1200, TTAspectSample::Pillarbox, t);
    check(!h.takeDiscardedCandidate(c),
          "a running candidate is not reported while it still lives");

    h.feed(1300, TTAspectSample::NoPillarbox, t);   // breaks the run
    check(h.takeDiscardedCandidate(c),
          "the broken candidate is reported");
    check(c.firstFrame == 1000, "discarded candidate starts at 1000");
    check(c.toPillarbox, "discarded candidate was heading for 4:3pb");
    check(c.heldFrames == 200, "held 1000..1200 == 200 frames, not up to 1300");

    check(!h.takeDiscardedCandidate(c),
          "taking the candidate clears it");

    // Noise inside the already confirmed state is not a challenge and must
    // stay silent.
    h.feed(1400, TTAspectSample::Pillarbox, t);
    h.feed(1500, TTAspectSample::NoPillarbox, t);
    check(h.takeDiscardedCandidate(c),
          "a one-sample challenge is recorded too");
    check(c.firstFrame == 1400, "one-sample challenge starts at 1400");
    check(c.toPillarbox, "one-sample challenge was heading for 4:3pb");
    check(c.heldFrames == 0, "a one-sample run held 0 frames");
    h.feed(1600, TTAspectSample::NoPillarbox, t);
    check(!h.takeDiscardedCandidate(c),
          "samples matching the confirmed state discard nothing");

    // A candidate that holds long enough still fires a transition, and does
    // not additionally report itself as discarded.
    TTAspectHysteresis h2(500);
    TTAspectTransition t2{};
    TTAspectCandidate  c2{};
    h2.feed(0, TTAspectSample::NoPillarbox, t2);
    h2.feed(2000, TTAspectSample::Pillarbox, t2);
    const bool fired = h2.feed(2600, TTAspectSample::Pillarbox, t2);
    check(fired, "a candidate held for 600 frames confirms the transition");
    check(!h2.takeDiscardedCandidate(c2),
          "a confirmed candidate is not also reported as discarded");
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    testClassifier();
    testReasons();
    testHysteresis();
    testDiscardedCandidates();
    printf("%s (%d failures)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures);
    return gFailures == 0 ? 0 : 1;
}
