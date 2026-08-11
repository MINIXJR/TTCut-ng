// Engine-level abort harness: no GUI, no pool.
// Usage: test_smartcut_abort <src.264|src.265> <fps>
//
// Eight poll points were added across TTESSmartCut and TTNaluParser
// (checkAbort() calls in smartCutFrames()'s entry check and segment loop,
// streamCopyFrames()'s per-frame path AND its chunked bulk-write path,
// decodeFramesIntoList(), runEncodePass(), flushEncoder(); plus
// TTNaluParser::parseFile()'s per-NAL-unit poll, reached via
// TTESSmartCut::initialize() forwarding checkAbort() through
// TTNaluParser::setAbortCallback()). This harness aims to reach every one
// of them at least once. Reachability notes below record where that could
// not be done cleanly, and why -- rather than leaving a silent gap.
//
// Cases:
//   (a) pre-run abort -> entry-check poll (smartCutFrames() top)
//   (b) H.264 only: abort on the FIRST "Processing segment" message during
//       a long stream-copy -> streamCopyFrames() per-frame poll. H.265 is
//       skipped here (with an explicit printed reason, not a silent no-op):
//       streamCopyFrames()'s per-frame path only runs when needsPatching is
//       true, and every needsPatching condition (reorder-frame SPS patch,
//       frame_num delta, MMCO neutralization) is H.264-specific. H.265
//       stream-copy always takes the bulk mmap-write fast path instead,
//       which has no poll point at all -- that gap is intentional and out
//       of scope here (see the "bulk path has no poll" note in the code
//       map; a later task adds chunked copying with its own poll).
//   (c) flag-lifetime contract: on ONE engine instance, requestAbort() then
//       initialize() then a clean run -> proves initialize() clears
//       mAbortRequested (not that a fresh engine starts at false, which
//       proves nothing).
//   (d) abort inside the encode loop (runEncodePass) after it has already
//       sent+freed a 10-frame progress batch -> runEncodePass() poll. This
//       is also the double-free regression case: before the companion
//       ownership fix, this exact scenario produced a SIGABRT ("corrupted
//       size vs. prev_size while consolidating" / "double free or
//       corruption (out)") on both codecs (verified by temporarily
//       reverting just that fix and rerunning this case).
//   (e) multi-segment cut, abort armed on the LAST "Processing segment 1/2"
//       message of segment 1 -> segment-loop poll (smartCutFrames(), the
//       one poll point with its own outFile.close(); every other poll's
//       cleanup is the pre-existing "if (!processSegment(...)) { close();
//       return false; }" one level up). Segment 2 must never be observed.
//   (f) single segment sized so exactly 40 frames (a multiple of the
//       10-frame progress batch) are selected for encoding -> the
//       runEncodePass loop's last iteration coincides with its last
//       progress message, so arming abort there lets runEncodePass finish
//       normally and lands the abort inside flushEncoder's drain loop
//       instead -> flushEncoder() poll.
//   (g) H.264 only: a cut-out that forces a tail re-encode after the head
//       re-encode + stream-copy, aborting on the LAST stream-copy progress
//       message so decodeFramesIntoList() sees the flag on its very first
//       iteration for the TAIL's decode -> decodeFramesIntoList() poll.
//       H.265: no cut-out range explored in this file's construction
//       produced an observable tail phase on the H.265 test source (same
//       search strategy, single re-encode + bulk copy every time) -- not
//       proven reachable for H.265 specifically. decodeFramesIntoList()'s
//       checkAbort() is shared, codec-agnostic code (not an H.264-only
//       branch), so case (g) on H.264 is accepted as sufficient evidence
//       the poll itself is correct; this note exists so that fact is
//       stated, not silently assumed.
//   (h) H.265 only: a full-file keep (cuts {0, frameCount-1}) -> the
//       chunked bulk-write path in streamCopyFrames() (8 MB chunks), abort
//       armed on the FIRST "Processing segment" message. H.264 is skipped
//       here (explicit reason printed): on these test sources needsPatching
//       is unconditionally true for H.264 pure stream-copy (mReorderDelay >
//       0, passed as patchReorderFrames, forces the per-frame path already
//       covered by case (b)) -- measured via TTSettings::logSmartCut(), the
//       bulk mmap-write branch is simply unreachable for H.264 on this
//       source, same asymmetry case (b) already documents in the other
//       direction. Confirmed reachable for H.265: the full-file copy is
//       ~71 MB / 8 MB chunks -> 9 "Processing segment" messages observed
//       before this case existed (measured with a throwaway probe, not
//       committed) -- arming on the first leaves ~8 more chunks unwritten,
//       so a completed run would prove nothing.
//   (i) TTNaluParser::parseFile()'s per-NAL-unit poll, reached through
//       TTESSmartCut::initialize(). Armed via the "Parsing ES file..."
//       progressChanged message, which fires synchronously in the same
//       thread immediately BEFORE mParser.parseFile() begins -- so the
//       requestAbort() call lands before the scan loop's first iteration,
//       and parseFile() returns false on its very first poll. This proves
//       the callback wiring end-to-end (TTNaluParser::setAbortCallback(),
//       initialize() returning false without calling setError(), correct
//       wasAborted()/lastError() attribution) but does NOT exercise
//       "aborted after N NAL units already scanned" specifically: these
//       test sources parse in under 50ms (measured), too fast for a
//       background-thread race to land reliably mid-scan without flakiness,
//       and unlike the frame loops, the parse loop has no per-N-unit
//       progress signal to hook a later arm point on. The poll is the same
//       unconditional per-iteration check regardless of when the flag
//       becomes true, so this is accepted as sufficient evidence -- stated
//       explicitly rather than silently assumed, consistent with the case
//       (g) reasoning above.
//
// Cut ranges are mid-stream (cut-in NOT at frame 0): a full-file keep on
// these sources resolves as a single bulk stream-copy with no poll point
// to abort at on H.265 before case (h) was added (frame 0 is already a
// valid keyframe on both; H.264 never reaches the bulk path here at all,
// see case (h)'s note). Frame 50 is
// ALSO nominally a keyframe on both sources (I-frame every 50 frames,
// confirmed via ffprobe) -- cutting there still forces re-encode, but for
// a different, verified reason: the debug log ("logSmartCut") shows
// "Cut-in at non-IDR I-frame <N> - re-encode needed for IDR boundary" --
// these are Open-GOP sources (non-IDR I-slices / HEVC CRA), and Smart Cut
// forces re-encode at ANY cut landing on such a frame to guarantee a clean
// IDR/CRA at the segment boundary, regardless of keyframe alignment.
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <cstdio>
#include "../../extern/ttessmartcut.h"

static int fail(const char* what) { fprintf(stderr, "FAIL: %s\n", what); return 1; }

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) { fprintf(stderr, "usage: %s <es> <fps>\n", argv[0]); return 2; }
    const QString src = argv[1];
    const double fps = QString(argv[2]).toDouble();
    const bool isH264 = QFileInfo(src).suffix().contains("264", Qt::CaseInsensitive);
    const QString out = "/usr/local/src/CLAUDE_TMP/TTCut-ng/cut-abort/abort_test_out."
                        + QFileInfo(src).suffix();

    // (a) pre-run abort -> entry-check poll
    {
        TTESSmartCut sc;
        if (!sc.initialize(src, fps)) return fail("initialize (a)");
        QList<QPair<int,int>> cuts{{0, sc.frameCount() - 1}};
        sc.requestAbort();
        if (sc.smartCutFrames(out, cuts)) return fail("(a) run succeeded despite pre-run abort");
        if (!sc.wasAborted()) return fail("(a) wasAborted not set");
        if (!sc.lastError().contains("aborted")) return fail("(a) lastError() does not contain \"aborted\"");
    }

    // (b) H.264 only: mid-copy abort -> streamCopyFrames() per-frame poll.
    // See the file header for why H.265 cannot reach this poll here.
    if (isH264) {
        TTESSmartCut sc;
        if (!sc.initialize(src, fps)) return fail("initialize (b)");
        int fc = sc.frameCount();
        if (fc <= 800) return fail("(b) source too short for the {50,800} range");
        QList<QPair<int,int>> cuts{{50, 800}};
        bool armed = false;
        QObject::connect(&sc, &TTESSmartCut::progressChanged,
                         [&](int, const QString& msg) {
            // Arm on the FIRST such message: many more stream-copy
            // iterations remain after it, so the abort must land inside
            // streamCopyFrames()'s own per-frame poll, not at a boundary.
            if (!armed && msg.contains("Processing segment")) {
                armed = true;
                sc.requestAbort();
            }
        });
        bool result = sc.smartCutFrames(out, cuts);
        if (!armed) return fail("(b) never reached streamCopyFrames progress");
        if (result) return fail("(b) run succeeded despite mid-copy abort");
        if (!sc.wasAborted()) return fail("(b) wasAborted not set");
        if (!sc.lastError().contains("aborted")) return fail("(b) lastError() does not contain \"aborted\"");
        // A partial artifact must exist (the QFile is opened before any
        // poll point can fire). It's guaranteed closed by the time we get
        // here regardless -- outFile is a stack QFile local to
        // smartCutFrames() whose destructor runs on every return path, so
        // there is nothing further to probe for from outside the call.
        if (!QFile::exists(out)) return fail("(b) no partial output written before abort");
        QFile::remove(out);
    } else {
        fprintf(stderr, "SKIP (b): H.265 stream-copy has no per-frame poll "
                         "point to reach (bulk-copy fast path, see header)\n");
    }

    // (c) flag-lifetime contract: requestAbort() BEFORE initialize() must be
    // discarded by initialize()'s clear, not carried into the next run. A
    // fresh engine (member initializer already false) would pass this test
    // even if the clear in initialize() were deleted -- so reuse ONE engine
    // instance and actually exercise the clear.
    {
        TTESSmartCut sc;
        if (!sc.initialize(src, fps)) return fail("initialize (c) first");
        sc.requestAbort();
        // Do NOT run smartCutFrames() here: this exercises the SAME engine
        // being reused for a second operation, exactly as the flag-lifetime
        // rule describes ("every owner creates a fresh engine per
        // operation" is the *usual* case; this proves the rule holds even
        // if that usual case is violated).
        if (!sc.initialize(src, fps)) return fail("initialize (c) second");
        QList<QPair<int,int>> cuts{{0, qMin(199, sc.frameCount() - 1)}};
        if (!sc.smartCutFrames(out, cuts)) return fail("(c) clean run failed after re-initialize");
        if (sc.wasAborted()) return fail("(c) wasAborted set despite initialize() clearing the flag");
        QFile::remove(out);
    }

    // (d) crash regression: abort specifically on the first progress message
    // that comes from inside runEncodePass ("Encoding segment..."), not just
    // the first progressChanged of any kind. This pins the exact precondition
    // that used to SIGABRT: emitCutProgress there only fires every 10 sent
    // frames, so by the time this fires, >=10 frames have already been
    // through the loop's success path (sent to the encoder and freed).
    {
        TTESSmartCut sc;
        if (!sc.initialize(src, fps)) return fail("initialize (d)");
        int fc = sc.frameCount();
        if (fc <= 800) return fail("(d) source too short for the {50,800} range");
        QList<QPair<int,int>> cuts{{50, 800}};
        bool hitEncodeProgress = false;
        QObject::connect(&sc, &TTESSmartCut::progressChanged,
                         [&](int, const QString& msg) {
            if (msg.contains("Encoding segment")) {
                hitEncodeProgress = true;
                sc.requestAbort();
            }
        });
        bool result = sc.smartCutFrames(out, cuts);
        if (!hitEncodeProgress) return fail("(d) never reached runEncodePass progress");
        if (result) return fail("(d) run succeeded despite mid-encode abort");
        if (!sc.wasAborted()) return fail("(d) wasAborted not set");
        if (!sc.lastError().contains("aborted")) return fail("(d) lastError() does not contain \"aborted\"");
        QFile::remove(out);
    }

    // (e) multi-segment cut, abort armed on the LAST "Processing segment
    // 1/2" message of segment 1 -> segment-loop poll (its own
    // outFile.close(), never exercised by any single-segment case above).
    // Segment 1's cut-out (95 for H.264, 90 for H.265) is chosen so the
    // per-frame stream-copy path emits EXACTLY ONE "Processing segment 1/2"
    // message for that segment (structurally determined by frame counts,
    // not by timing) -- so "arm on the first one" and "arm on the last one"
    // are the same message, with no ambiguity about which occurrence to
    // pick. Segment 2 must never be observed if this poll works.
    {
        TTESSmartCut sc;
        if (!sc.initialize(src, fps)) return fail("initialize (e)");
        int fc = sc.frameCount();
        int seg1End = isH264 ? 95 : 90;
        if (fc <= 1800) return fail("(e) source too short for the multi-segment range");
        QList<QPair<int,int>> cuts{{50, seg1End}, {1000, 1800}};
        bool armed = false;
        bool sawSegment2 = false;
        QObject::connect(&sc, &TTESSmartCut::progressChanged,
                         [&](int, const QString& msg) {
            if (msg.contains("2/2")) sawSegment2 = true;
            if (!armed && msg.contains("Processing segment 1/2")) {
                armed = true;
                sc.requestAbort();
            }
        });
        bool result = sc.smartCutFrames(out, cuts);
        if (!armed) return fail("(e) never reached segment 1's stream-copy progress");
        if (sawSegment2) return fail("(e) segment 2 started despite inter-segment abort");
        if (result) return fail("(e) run succeeded despite inter-segment abort");
        if (!sc.wasAborted()) return fail("(e) wasAborted not set");
        if (!sc.lastError().contains("aborted")) return fail("(e) lastError() does not contain \"aborted\"");
        QFile::remove(out);
    }

    // (f) flushEncoder() poll: cuts{50,89} selects exactly 40 frames for
    // encoding on both test sources (a multiple of the 10-frame progress
    // batch in runEncodePass), so arming on the LAST such message lets
    // runEncodePass finish its loop normally (no more iterations left to
    // catch the flag) and the abort is caught in flushEncoder's drain loop
    // instead -- the only poll point reachable between "last frame sent"
    // and "reencodeFrames' own post-encode progress message". Confirmed by
    // construction: no "Processing segment"/"Cut complete" message is ever
    // observed after arming.
    {
        TTESSmartCut sc;
        if (!sc.initialize(src, fps)) return fail("initialize (f)");
        int fc = sc.frameCount();
        if (fc <= 89) return fail("(f) source too short for the {50,89} range");
        QList<QPair<int,int>> cuts{{50, 89}};
        int encodeMsgCount = 0;
        bool sawFurtherProgress = false;
        QObject::connect(&sc, &TTESSmartCut::progressChanged,
                         [&](int, const QString& msg) {
            if (msg.contains("Encoding segment")) {
                encodeMsgCount++;
                if (encodeMsgCount == 4) { sc.requestAbort(); return; }
            }
            if (encodeMsgCount >= 4 && !msg.contains("Encoding segment")) sawFurtherProgress = true;
        });
        bool result = sc.smartCutFrames(out, cuts);
        if (encodeMsgCount < 4) return fail("(f) never reached the 4th runEncodePass progress message");
        if (sawFurtherProgress) return fail("(f) saw progress after the armed message (flushEncoder not reached?)");
        if (result) return fail("(f) run succeeded despite abort armed before flushEncoder");
        if (!sc.wasAborted()) return fail("(f) wasAborted not set");
        if (!sc.lastError().contains("aborted")) return fail("(f) lastError() does not contain \"aborted\"");
        QFile::remove(out);
    }

    // (g) H.264 only: decodeFramesIntoList() poll, via a tail re-encode's
    // decode phase. See the file header for the H.265 reachability note.
    if (isH264) {
        TTESSmartCut sc;
        if (!sc.initialize(src, fps)) return fail("initialize (g)");
        int fc = sc.frameCount();
        if (fc <= 770) return fail("(g) source too short for the {50,770} range");
        QList<QPair<int,int>> cuts{{50, 770}};
        int processingMsgCount = 0;
        bool armed = false;
        bool sawTailEncode = false;
        QObject::connect(&sc, &TTESSmartCut::progressChanged,
                         [&](int, const QString& msg) {
            if (msg.contains("Processing segment")) {
                processingMsgCount++;
                // The 14th "Processing segment" message is the last one
                // before the tail re-encode's own "Encoding segment"
                // messages resume (measured: 14 head/copy-phase messages,
                // then 2 tail-phase "Encoding segment" messages, then 1
                // final "Processing segment", then "Cut complete").
                if (processingMsgCount == 14) { armed = true; sc.requestAbort(); return; }
            }
            if (armed && msg.contains("Encoding segment")) sawTailEncode = true;
        });
        bool result = sc.smartCutFrames(out, cuts);
        if (!armed) return fail("(g) never reached the 14th stream-copy progress message");
        if (sawTailEncode) return fail("(g) tail re-encode's own progress fired (decodeFramesIntoList not reached?)");
        if (result) return fail("(g) run succeeded despite abort armed before the tail decode");
        if (!sc.wasAborted()) return fail("(g) wasAborted not set");
        if (!sc.lastError().contains("aborted")) return fail("(g) lastError() does not contain \"aborted\"");
        QFile::remove(out);
    } else {
        fprintf(stderr, "SKIP (g): no H.265 cut-out range in this file's "
                         "construction produced an observable tail re-encode "
                         "phase (see header note)\n");
    }

    // (h) H.265 only: chunked bulk-write poll in streamCopyFrames(). See the
    // file header for why H.264 cannot reach the bulk path on this source.
    if (!isH264) {
        TTESSmartCut sc;
        if (!sc.initialize(src, fps)) return fail("initialize (h)");
        int fc = sc.frameCount();
        QList<QPair<int,int>> cuts{{0, fc - 1}};  // full-file keep -> single bulk stream-copy
        bool armed = false;
        int processingMsgCount = 0;
        QObject::connect(&sc, &TTESSmartCut::progressChanged,
                         [&](int, const QString& msg) {
            if (msg.contains("Processing segment")) {
                processingMsgCount++;
                // Arm on the FIRST message: ~9 chunks are expected for this
                // source's full-file size at 8 MB/chunk (measured), so
                // several chunks remain unwritten when armed here -- a
                // completed run would prove nothing.
                if (!armed) { armed = true; sc.requestAbort(); }
            }
        });
        bool result = sc.smartCutFrames(out, cuts);
        if (!armed) return fail("(h) never reached streamCopyFrames bulk-write progress");
        if (result) return fail("(h) run succeeded despite mid-chunk abort");
        // Exactly one message: checkAbort() at the top of the NEXT chunk
        // iteration must catch the flag before that chunk is written or
        // its own progress emitted -- proves the loop stopped at the first
        // opportunity, not merely "eventually".
        if (processingMsgCount != 1)
            return fail("(h) expected exactly one chunk progress message before abort took effect");
        if (!sc.wasAborted()) return fail("(h) wasAborted not set");
        if (!sc.lastError().contains("aborted")) return fail("(h) lastError() does not contain \"aborted\"");
        if (!QFile::exists(out)) return fail("(h) no partial output written before abort");
        QFile::remove(out);
    } else {
        fprintf(stderr, "SKIP (h): H.264 stream-copy never reaches the bulk "
                         "mmap-write path on this source (mReorderDelay > 0 "
                         "forces needsPatching -> per-frame path, already "
                         "covered by case (b); see header note)\n");
    }

    // (i) TTNaluParser::parseFile()'s per-NAL-unit poll, reached through
    // TTESSmartCut::initialize(). See the file header for why this proves
    // the wiring, not a genuine "mid-scan" abort.
    {
        TTESSmartCut sc;
        bool armed = false;
        QObject::connect(&sc, &TTESSmartCut::progressChanged,
                         [&](int, const QString& msg) {
            if (!armed && msg.contains("Parsing ES file")) {
                armed = true;
                sc.requestAbort();
            }
        });
        bool result = sc.initialize(src, fps);
        if (!armed) return fail("(i) never reached the \"Parsing ES file...\" message");
        if (result) return fail("(i) initialize() succeeded despite abort armed before parseFile()");
        if (!sc.wasAborted()) return fail("(i) wasAborted not set");
        if (!sc.lastError().contains("aborted")) return fail("(i) lastError() does not contain \"aborted\"");
    }

    printf("PASS\n");
    return 0;
}
