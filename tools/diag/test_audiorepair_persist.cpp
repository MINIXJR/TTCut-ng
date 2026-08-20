// Does an audio repair item planned on one track survive a .ttcut project
// save/reload round trip?
//
// TTAudioRepairItem (data/ttaudiorepairitem.h) describes one planned repair
// on one audio track (frame range + channel mask + method). This harness
// exercises the SAME write/read path the app uses (TTAVData::writeProjectFile
// / readProjectFile -> TTCutProjectData), matching the existing <Delay>
// mechanism (data/ttcutprojectdata.cpp, TTAVData::setPendingAudioDelay):
//
//   1. Open a real AV item (video+audio, async thread pool), attach two
//      TTAudioRepairItem entries, write the project, reload it into a fresh
//      TTAVData, and compare every field.
//   2. Load tools/testdata/tux_test.ttcut (no <Repair> element at all) and
//      confirm the repair list comes back empty.
//   3. Load a hand-written project where the <Audio> element carries both an
//      unrecognized child element and a <Repair> element, and confirm the
//      unknown element is ignored while the repair still loads.
//   4. Reorder regression (review finding, fix-round 1): load a two-track
//      project with a repair on track 0, call TTAVItem::onSwapAudioItems(0,1)
//      (the slot TTAudioTreeView::swapItems drives since v0.81.2), then
//      save+reload and confirm the repair is still attributed to the SAME
//      audio file, not to whatever track now sits at position 0.
//   5. Remove regression (same review finding): load a two-track project
//      with a repair on each track, call onRemoveAudioItem(0), and confirm
//      track 0's repair is gone while track 1's repair survives with its
//      trackIndex shifted from 1 to 0.
//   6. Load-time validation (task 8): a project whose <Repair> FrameTo lies
//      past the end of the audio file it references (e.g. the AC3 was
//      re-demuxed/replaced after saving) must not be applied blindly. The
//      item stays in the list (isEnabled()==false, never silently dropped)
//      and a warning is logged. tux_test.ac3 is real 192 kbit/s AC3 (768
//      bytes/frame, verified via ffprobe pkt_size -- NOT the 384 kbit/s
//      1536 B/frame some other corpus material uses), so this case also
//      covers a frame index near the middle of the file (frame 5475 of
//      5476) that a hardcoded-1536 assumption would wrongly flag as
//      out-of-bounds while the real 768 B/frame math says it's the last
//      valid frame -- see the fix-2 report appendix for the red/green proof
//      against the old constant.
//   7. Load-time validation, part 2 (final review M4/M5): a range whose last
//      frame is only PARTIALLY present in the file (checked on a copy of
//      tux_test.ac3 with a 400-byte partial-frame tail appended, which the
//      old `frameTo * frameBytes >= size` test let through), and ranges that
//      are structurally impossible (end before start, negative start) - all
//      disabled with a log line, none dropped.
//   8. StreamPoint AudioFrameFrom/AudioFrameTo round trip (residuals R4):
//      TTStreamPoint::setAudioFrameRange() is read (deserializeStreamPoints())
//      and written (serializeStreamPoints()), but until this case nothing
//      exercised the actual save->reload path for it. Writes one
//      AudioAnomaly point WITH an exact AC3 frame range and one ManualMarker
//      point WITHOUT one (the pre-existing-field / old-project path -
//      hasAudioFrameRange() must stay false and the point must still load
//      cleanly instead of tripping on the missing elements).
//
//   usage: test_audiorepair_persist <workdir>
//
// Build via `cmake --build build --target test_audiorepair_persist`.
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

#include "avstream/ttavstream.h"
#include "common/ttmessagelogger.h"
#include "data/ttaudiorepairitem.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"
#include "data/ttcutprojectdata.h"
#include "data/ttstreampoint.h"

static int gFailures = 0;

static void check(bool ok, const QString& what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) gFailures++;
}

static const QString kVideoFile =
    QStringLiteral("/usr/local/src/TTCut-ng/tools/testdata/tux_test.264");
static const QString kAudioFile =
    QStringLiteral("/usr/local/src/TTCut-ng/tools/testdata/tux_test.ac3");
static const QString kNoRepairProject =
    QStringLiteral("/usr/local/src/TTCut-ng/tools/testdata/tux_test.ttcut");

// --- Case 1: write with two repair items on track 0, reload, compare -------
static void testRoundTrip(const QString& workDir)
{
    const QString projectPath = QDir(workDir).absoluteFilePath("repair_roundtrip.ttcut");
    QFile::remove(projectPath);

    TTAVData avSrc;

    // openAVStreams is async; wait for the pool to drain before touching the
    // resulting TTAVItem.
    QEventLoop srcLoop;
    bool srcDone = false;
    QObject::connect(&avSrc, &TTAVData::threadPoolExit, [&]() {
        srcDone = true;
        srcLoop.quit();
    });
    avSrc.openAVStreams(kVideoFile);
    if (!srcDone) srcLoop.exec();

    check(avSrc.avCount() == 1, "source: one AV item opened");
    if (avSrc.avCount() != 1) return;

    TTAVItem* srcItem = avSrc.avItemAt(0);
    check(srcItem->audioCount() == 1, "source: one audio track opened");
    if (srcItem->audioCount() != 1) return;

    const TTAudioRepairItem itemA(0, 1900, 1938, 12, QStringLiteral("mute"));
    const TTAudioRepairItem itemB(0, 100, 105, 1); // default method
    srcItem->appendAudioRepair(itemA);
    srcItem->appendAudioRepair(itemB);
    check(srcItem->audioRepairList().size() == 2, "source: two repair items attached");

    avSrc.writeProjectFile(QFileInfo(projectPath), {}, TTLogoProjectData());
    check(QFileInfo::exists(projectPath), "project file written");

    // Sanity-check the raw XML actually carries the repair data (catches a
    // writer that silently drops the section while the reader independently
    // fails to notice).
    QFile raw(projectPath);
    QString xmlText;
    if (raw.open(QIODevice::ReadOnly | QIODevice::Text)) {
        xmlText = QString::fromUtf8(raw.readAll());
        raw.close();
    }
    check(xmlText.contains("<Repair>"), "written xml contains <Repair>");
    check(xmlText.contains("<FrameFrom>1900</FrameFrom>"), "written xml contains FrameFrom 1900");
    check(xmlText.contains("<FrameTo>1938</FrameTo>"), "written xml contains FrameTo 1938");
    check(xmlText.contains("<Channels>12</Channels>"), "written xml contains Channels 12");
    check(xmlText.contains("<Method>mute</Method>"), "written xml contains Method mute");
    check(xmlText.contains("<FrameFrom>100</FrameFrom>"), "written xml contains FrameFrom 100 (second item)");

    TTAVData avDst;
    QEventLoop dstLoop;
    bool dstDone = false;
    QObject::connect(&avDst, &TTAVData::readProjectFileFinished, [&](const QString&) {
        dstDone = true;
        dstLoop.quit();
    });
    QTimer::singleShot(20000, &dstLoop, &QEventLoop::quit);
    avDst.readProjectFile(QFileInfo(projectPath));
    if (!dstDone) dstLoop.exec();

    check(dstDone, "reload: readProjectFileFinished fired");
    check(avDst.avCount() == 1, "reload: one AV item loaded");
    if (avDst.avCount() != 1) return;

    TTAVItem* dstItem = avDst.avItemAt(0);
    check(dstItem->audioCount() == 1, "reload: one audio track loaded");

    QList<TTAudioRepairItem> repairs = dstItem->audioRepairList();
    check(repairs.size() == 2, QString("reload: two repair items present (got %1)").arg(repairs.size()));
    if (repairs.size() != 2) return;

    // Order of application isn't contractually fixed by the brief, so match
    // by frameFrom rather than assuming list order.
    const TTAudioRepairItem* rA = nullptr;
    const TTAudioRepairItem* rB = nullptr;
    for (const TTAudioRepairItem& r : repairs) {
        if (r.frameFrom() == 1900) rA = &r;
        if (r.frameFrom() == 100)   rB = &r;
    }
    check(rA != nullptr, "reload: repair A (frameFrom 1900) found");
    check(rB != nullptr, "reload: repair B (frameFrom 100) found");
    if (rA) {
        check(rA->trackIndex() == 0, "repair A trackIndex round-trips");
        check(rA->frameTo() == 1938, "repair A frameTo round-trips");
        check(rA->channelMask() == 12, "repair A channelMask round-trips");
        check(rA->method() == "mute", "repair A method round-trips");
        check(rA->isEnabled(), "repair A isEnabled defaults true after load");
    }
    if (rB) {
        check(rB->trackIndex() == 0, "repair B trackIndex round-trips");
        check(rB->frameTo() == 105, "repair B frameTo round-trips");
        check(rB->channelMask() == 1, "repair B channelMask round-trips");
        check(rB->method() == "silence-fade", "repair B method round-trips (default)");
    }
}

// --- Case 2: a project without any <Repair> element loads an empty list ----
static void testNoRepairElement()
{
    check(QFileInfo::exists(kNoRepairProject),
          "fixture: tux_test.ttcut exists (no <Repair> element)");
    if (!QFileInfo::exists(kNoRepairProject)) return;

    TTAVData avDst;
    QEventLoop loop;
    bool done = false;
    QObject::connect(&avDst, &TTAVData::readProjectFileFinished, [&](const QString&) {
        done = true;
        loop.quit();
    });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    avDst.readProjectFile(QFileInfo(kNoRepairProject));
    if (!done) loop.exec();

    check(done, "no-repair project: readProjectFileFinished fired");
    check(avDst.avCount() == 1, "no-repair project: one AV item loaded");
    if (avDst.avCount() != 1) return;

    TTAVItem* item = avDst.avItemAt(0);
    check(item->audioCount() == 1, "no-repair project: one audio track loaded");
    check(item->audioRepairList().isEmpty(),
          "no-repair project: audioRepairList() is empty");
}

// --- Case 3: unknown <Audio> child elements don't disturb Repair parsing ---
static void testUnknownAudioChildIgnored(const QString& workDir)
{
    const QString projectPath = QDir(workDir).absoluteFilePath("repair_unknown_child.ttcut");

    QFile f(projectPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        check(false, "unknown-child fixture: could not write project file");
        return;
    }
    QTextStream out(&f);
    out << "<!DOCTYPE TTCut-Projectfile>\n"
           "<TTCut-Projectfile>\n"
           " <Version>1.0</Version>\n"
           " <Video>\n"
           "  <Order>0</Order>\n"
           "  <Name>" << kVideoFile << "</Name>\n"
           "  <Audio>\n"
           "   <Order>0</Order>\n"
           "   <Name>" << kAudioFile << "</Name>\n"
           "   <SomeFutureElement>ignored</SomeFutureElement>\n"
           "   <Repair>\n"
           "    <FrameFrom>100</FrameFrom>\n"
           "    <FrameTo>120</FrameTo>\n"
           "    <Channels>3</Channels>\n"
           "    <Method>silence-fade</Method>\n"
           "   </Repair>\n"
           "  </Audio>\n"
           " </Video>\n"
           "</TTCut-Projectfile>\n";
    f.close();

    TTAVData avDst;
    QEventLoop loop;
    bool done = false;
    QObject::connect(&avDst, &TTAVData::readProjectFileFinished, [&](const QString&) {
        done = true;
        loop.quit();
    });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    avDst.readProjectFile(QFileInfo(projectPath));
    if (!done) loop.exec();

    check(done, "unknown-child project: readProjectFileFinished fired");
    check(avDst.avCount() == 1, "unknown-child project: one AV item loaded");
    if (avDst.avCount() != 1) return;

    TTAVItem* item = avDst.avItemAt(0);
    check(item->audioCount() == 1, "unknown-child project: one audio track loaded");

    QList<TTAudioRepairItem> repairs = item->audioRepairList();
    check(repairs.size() == 1,
          QString("unknown-child project: one repair item present (got %1)").arg(repairs.size()));
    if (repairs.size() == 1) {
        check(repairs[0].frameFrom() == 100, "unknown-child project: repair frameFrom");
        check(repairs[0].frameTo() == 120, "unknown-child project: repair frameTo");
        check(repairs[0].channelMask() == 3, "unknown-child project: repair channelMask");
        check(repairs[0].method() == "silence-fade", "unknown-child project: repair method");
    }
}

// --- Case 4: onSwapAudioItems() keeps the repair attached to its track's
// audio file (not to the numeric position), across an in-memory swap AND a
// save/reload cycle -----------------------------------------------------
static void testReorderReassignsTrack(const QString& workDir)
{
    const QString audioA = QDir(workDir).absoluteFilePath("reorder_track_a.ac3");
    const QString audioB = QDir(workDir).absoluteFilePath("reorder_track_b.ac3");
    QFile::remove(audioA);
    QFile::remove(audioB);
    if (!QFile::copy(kAudioFile, audioA) || !QFile::copy(kAudioFile, audioB)) {
        check(false, "reorder: could not stage two-track fixture");
        return;
    }

    const QString projectPath = QDir(workDir).absoluteFilePath("repair_reorder.ttcut");
    QFile f(projectPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        check(false, "reorder: could not write project file");
        return;
    }
    QTextStream out(&f);
    out << "<!DOCTYPE TTCut-Projectfile>\n"
           "<TTCut-Projectfile>\n"
           " <Version>1.0</Version>\n"
           " <Video>\n"
           "  <Order>0</Order>\n"
           "  <Name>" << kVideoFile << "</Name>\n"
           "  <Audio>\n"
           "   <Order>0</Order>\n"
           "   <Name>" << audioA << "</Name>\n"
           "   <Repair>\n"
           "    <FrameFrom>200</FrameFrom>\n"
           "    <FrameTo>210</FrameTo>\n"
           "    <Channels>4</Channels>\n"
           "    <Method>silence-fade</Method>\n"
           "   </Repair>\n"
           "  </Audio>\n"
           "  <Audio>\n"
           "   <Order>1</Order>\n"
           "   <Name>" << audioB << "</Name>\n"
           "  </Audio>\n"
           " </Video>\n"
           "</TTCut-Projectfile>\n";
    f.close();

    TTAVData avData;
    QEventLoop loop;
    bool done = false;
    QObject::connect(&avData, &TTAVData::readProjectFileFinished, [&](const QString&) {
        done = true;
        loop.quit();
    });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    avData.readProjectFile(QFileInfo(projectPath));
    if (!done) loop.exec();

    check(done, "reorder: initial load finished");
    check(avData.avCount() == 1, "reorder: one AV item loaded");
    if (avData.avCount() != 1) return;

    TTAVItem* item = avData.avItemAt(0);
    check(item->audioCount() == 2, "reorder: two audio tracks loaded");
    if (item->audioCount() != 2) return;

    // sortByProjectOrder() should have put track A (with the repair) at
    // position 0, matching its saved <Order>.
    check(item->audioStreamAt(0)->filePath() == audioA, "reorder: track A at position 0 before swap");
    check(item->audioRepairList().size() == 1, "reorder: one repair loaded before swap");
    if (item->audioRepairList().size() == 1)
        check(item->audioRepairList().first().trackIndex() == 0, "reorder: repair trackIndex 0 before swap");

    item->onSwapAudioItems(0, 1);
    check(item->audioStreamAt(0)->filePath() == audioB, "reorder: track B at position 0 after swap");
    check(item->audioStreamAt(1)->filePath() == audioA, "reorder: track A at position 1 after swap");

    QList<TTAudioRepairItem> afterSwap = item->audioRepairList();
    check(afterSwap.size() == 1, "reorder: repair still present after swap (in-memory)");
    if (afterSwap.size() == 1)
        check(afterSwap.first().trackIndex() == 1,
              "reorder: repair trackIndex follows track A to position 1 after swap (in-memory)");

    const QString savedProjectPath = QDir(workDir).absoluteFilePath("repair_reorder_saved.ttcut");
    avData.writeProjectFile(QFileInfo(savedProjectPath), {}, TTLogoProjectData());

    TTAVData avReload;
    QEventLoop reloadLoop;
    bool reloadDone = false;
    QObject::connect(&avReload, &TTAVData::readProjectFileFinished, [&](const QString&) {
        reloadDone = true;
        reloadLoop.quit();
    });
    QTimer::singleShot(20000, &reloadLoop, &QEventLoop::quit);
    avReload.readProjectFile(QFileInfo(savedProjectPath));
    if (!reloadDone) reloadLoop.exec();

    check(reloadDone, "reorder: reload after save finished");
    check(avReload.avCount() == 1, "reorder: reload has one AV item");
    if (avReload.avCount() != 1) return;

    TTAVItem* reloaded = avReload.avItemAt(0);
    check(reloaded->audioCount() == 2, "reorder: reload has two audio tracks");
    if (reloaded->audioCount() != 2) return;

    // Find track A (the original repair owner) by filename, not by position -
    // that's the whole point of the fix: the reviewer's scenario is exactly
    // "repair silently reassigned to the wrong track after a reorder+save".
    int trackAIndex = -1;
    for (int i = 0; i < reloaded->audioCount(); ++i) {
        if (reloaded->audioStreamAt(i)->filePath() == audioA) trackAIndex = i;
    }
    check(trackAIndex >= 0, "reorder: track A found after reload by filename");

    QList<TTAudioRepairItem> reloadedRepairs = reloaded->audioRepairList();
    check(reloadedRepairs.size() == 1,
          QString("reorder: reload has one repair (got %1)").arg(reloadedRepairs.size()));
    if (reloadedRepairs.size() == 1 && trackAIndex >= 0) {
        check(reloadedRepairs.first().trackIndex() == trackAIndex,
              "reorder: reloaded repair still attributed to track A's file, not to position 0");
        check(reloadedRepairs.first().frameFrom() == 200, "reorder: reloaded repair frameFrom intact");
    }
}

// --- Case 5: onRemoveAudioItem() drops the removed track's repair and
// shifts trailing repairs' trackIndex down by one -------------------------
static void testRemoveShiftsTrackIndex(const QString& workDir)
{
    const QString audioA = QDir(workDir).absoluteFilePath("remove_track_a.ac3");
    const QString audioB = QDir(workDir).absoluteFilePath("remove_track_b.ac3");
    QFile::remove(audioA);
    QFile::remove(audioB);
    if (!QFile::copy(kAudioFile, audioA) || !QFile::copy(kAudioFile, audioB)) {
        check(false, "remove: could not stage two-track fixture");
        return;
    }

    const QString projectPath = QDir(workDir).absoluteFilePath("repair_remove.ttcut");
    QFile f(projectPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        check(false, "remove: could not write project file");
        return;
    }
    QTextStream out(&f);
    out << "<!DOCTYPE TTCut-Projectfile>\n"
           "<TTCut-Projectfile>\n"
           " <Version>1.0</Version>\n"
           " <Video>\n"
           "  <Order>0</Order>\n"
           "  <Name>" << kVideoFile << "</Name>\n"
           "  <Audio>\n"
           "   <Order>0</Order>\n"
           "   <Name>" << audioA << "</Name>\n"
           "   <Repair>\n"
           "    <FrameFrom>10</FrameFrom>\n"
           "    <FrameTo>20</FrameTo>\n"
           "    <Channels>1</Channels>\n"
           "    <Method>silence-fade</Method>\n"
           "   </Repair>\n"
           "  </Audio>\n"
           "  <Audio>\n"
           "   <Order>1</Order>\n"
           "   <Name>" << audioB << "</Name>\n"
           "   <Repair>\n"
           "    <FrameFrom>500</FrameFrom>\n"
           "    <FrameTo>510</FrameTo>\n"
           "    <Channels>2</Channels>\n"
           "    <Method>mute</Method>\n"
           "   </Repair>\n"
           "  </Audio>\n"
           " </Video>\n"
           "</TTCut-Projectfile>\n";
    f.close();

    TTAVData avData;
    QEventLoop loop;
    bool done = false;
    QObject::connect(&avData, &TTAVData::readProjectFileFinished, [&](const QString&) {
        done = true;
        loop.quit();
    });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    avData.readProjectFile(QFileInfo(projectPath));
    if (!done) loop.exec();

    check(done, "remove: initial load finished");
    check(avData.avCount() == 1, "remove: one AV item loaded");
    if (avData.avCount() != 1) return;

    TTAVItem* item = avData.avItemAt(0);
    check(item->audioCount() == 2, "remove: two audio tracks loaded");
    check(item->audioRepairList().size() == 2, "remove: two repairs loaded before removal");
    if (item->audioCount() != 2 || item->audioRepairList().size() != 2) return;

    item->onRemoveAudioItem(0); // removes track A (the frameFrom=10 repair's track)

    check(item->audioCount() == 1, "remove: one audio track remains");
    check(item->audioStreamAt(0)->filePath() == audioB, "remove: remaining track is B");

    QList<TTAudioRepairItem> remaining = item->audioRepairList();
    check(remaining.size() == 1,
          QString("remove: one repair remains (got %1)").arg(remaining.size()));
    if (remaining.size() == 1) {
        check(remaining.first().frameFrom() == 500, "remove: surviving repair is track B's (frameFrom 500)");
        check(remaining.first().trackIndex() == 0,
              "remove: surviving repair's trackIndex shifted from 1 to 0");
    }
}

// --- Case 6: load-time validation disables a repair range past the audio
// file's end, while an in-bounds repair in the same section stays enabled --
static void testLoadValidationDisablesOutOfRange(const QString& workDir, const QString& logPath)
{
    // tux_test.ac3 is real 192 kbit/s @ 48 kHz AC3 (verified via
    // `ffprobe -show_packets`: pkt_size=768 for every packet), so its real
    // frame size is 768 bytes/frame, NOT the 384 kbit/s 1536 B/frame the old
    // hardcoded constant assumed. 4205568 bytes / 768 B = exactly 5476
    // frames (indices 0..5475).
    const qint64 audioSize = QFileInfo(kAudioFile).size();
    check(audioSize == 4205568, QString("fixture: tux_test.ac3 is 4205568 bytes (got %1)").arg(audioSize));
    check(audioSize % 768 == 0 && audioSize / 768 == 5476,
          QString("fixture: tux_test.ac3 is exactly 5476 frames of 768 B (got %1)").arg(audioSize / 768));

    // Three repair items:
    //   A  frameFrom=90,   frameTo=100   - comfortably inside, non-discriminating
    //      sanity check (identical verdict under either frame-size assumption).
    //   B  frameFrom=5470, frameTo=5475  - frame 5475 is the LAST VALID frame
    //      (byte range [4204800, 4205568), exactly the file's tail). Real
    //      768 B/frame math says IN BOUNDS (5475*768 = 4204800 < 4205568).
    //      This is both the boundary/grenzfall check and the discriminator:
    //      under the old 1536 B/frame constant, 5475*1536 = 8409600, which
    //      is >= audioSize, so the old code would wrongly disable this item.
    //   C  frameFrom=5480, frameTo=5490  - genuinely past the end under
    //      either assumption (5490*768 = 4216320 >= audioSize); confirms
    //      real out-of-range detection still works.
    const QString projectPath = QDir(workDir).absoluteFilePath("repair_load_validation.ttcut");
    QFile f(projectPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        check(false, "load-validation: could not write project file");
        return;
    }
    QTextStream out(&f);
    out << "<!DOCTYPE TTCut-Projectfile>\n"
           "<TTCut-Projectfile>\n"
           " <Version>1.0</Version>\n"
           " <Video>\n"
           "  <Order>0</Order>\n"
           "  <Name>" << kVideoFile << "</Name>\n"
           "  <Audio>\n"
           "   <Order>0</Order>\n"
           "   <Name>" << kAudioFile << "</Name>\n"
           "   <Repair>\n"
           "    <FrameFrom>90</FrameFrom>\n"
           "    <FrameTo>100</FrameTo>\n"
           "    <Channels>12</Channels>\n"
           "    <Method>silence-fade</Method>\n"
           "   </Repair>\n"
           "   <Repair>\n"
           "    <FrameFrom>5470</FrameFrom>\n"
           "    <FrameTo>5475</FrameTo>\n"
           "    <Channels>12</Channels>\n"
           "    <Method>silence-fade</Method>\n"
           "   </Repair>\n"
           "   <Repair>\n"
           "    <FrameFrom>5480</FrameFrom>\n"
           "    <FrameTo>5490</FrameTo>\n"
           "    <Channels>12</Channels>\n"
           "    <Method>silence-fade</Method>\n"
           "   </Repair>\n"
           "  </Audio>\n"
           " </Video>\n"
           "</TTCut-Projectfile>\n";
    f.close();

    // Truncate the log so this test's assertions only see its own warning,
    // not leftovers from an earlier case run in the same process.
    QFile::remove(logPath);
    TTMessageLogger::getInstance()->setLogFilePath(logPath);

    TTAVData avData;
    QEventLoop loop;
    bool done = false;
    QObject::connect(&avData, &TTAVData::readProjectFileFinished, [&](const QString&) {
        done = true;
        loop.quit();
    });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    avData.readProjectFile(QFileInfo(projectPath));
    if (!done) loop.exec();

    check(done, "load-validation: readProjectFileFinished fired");
    check(avData.avCount() == 1, "load-validation: one AV item loaded");
    if (avData.avCount() != 1) return;

    TTAVItem* item = avData.avItemAt(0);
    QList<TTAudioRepairItem> repairs = item->audioRepairList();
    check(repairs.size() == 3,
          QString("load-validation: all three repair items present, none dropped (got %1)").arg(repairs.size()));
    if (repairs.size() != 3) return;

    const TTAudioRepairItem* inBounds = nullptr;
    const TTAudioRepairItem* nearEnd = nullptr;
    const TTAudioRepairItem* outOfBounds = nullptr;
    for (const TTAudioRepairItem& r : repairs) {
        if (r.frameFrom() == 90)   inBounds = &r;
        if (r.frameFrom() == 5470) nearEnd = &r;
        if (r.frameFrom() == 5480) outOfBounds = &r;
    }
    check(inBounds != nullptr, "load-validation: in-bounds repair (frameFrom 90) found");
    check(nearEnd != nullptr, "load-validation: near-end repair (frameFrom 5470) found");
    check(outOfBounds != nullptr, "load-validation: out-of-bounds repair (frameFrom 5480) found");
    if (inBounds)
        check(inBounds->isEnabled(), "load-validation: in-bounds repair stays enabled");
    if (nearEnd)
        check(nearEnd->isEnabled(),
              "load-validation: near-end repair (frameTo 5475, the real last valid frame) "
              "stays enabled under the real 768 B/frame size");
    if (outOfBounds)
        check(!outOfBounds->isEnabled(), "load-validation: out-of-bounds repair is disabled, not dropped");

    // The warning must be on record - a disabled entry with no log trace
    // would be indistinguishable from a bug that dropped it silently.
    QFile logFile(logPath);
    QString logText;
    if (logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        logText = QString::fromUtf8(logFile.readAll());
        logFile.close();
    }
    check(logText.contains("5480") && logText.contains("5490"),
          "load-validation: log entry names the out-of-bounds repair range");
    check(logText.contains("disabling", Qt::CaseInsensitive),
          "load-validation: log entry explains why the repair was disabled");
    check(!logText.contains("repair range 90-100"),
          "load-validation: no warning logged for the in-bounds repair");
    check(!logText.contains("repair range 5470-5475"),
          "load-validation: no warning logged for the near-end repair");
}

// --- Case 7 (final review M4/M5): the two validation holes case 6 could not
// see, because tux_test.ac3's size happens to be an exact multiple of its
// frame size.
//
// M4: the old test was `frameTo * frameBytes >= size`, i.e. "does the last
// repaired frame START inside the file". A file whose tail holds only PART of
// a frame passes that test while the frame itself is incomplete. Built here
// by appending 400 zero bytes to a copy of tux_test.ac3 (5476 complete frames
// of 768 B + 400 bytes of nothing): a repair up to frame 5476 must be
// disabled, and under the OLD formula it would not have been (5476*768 =
// 4205568 < 4205968).
//
// M5: a hand-edited project file can carry a reversed or negative range.
// Neither is caught by any file-size test - a negative frameFrom made
// buildRepairTable read from the file's start, a reversed one produced an
// empty table - so both must be rejected structurally.
static void testLoadValidationRejectsMalformedRanges(const QString& workDir, const QString& logPath)
{
    const QString paddedAudio = QDir(workDir).absoluteFilePath("tux_test_padded_tail.ac3");
    QFile::remove(paddedAudio);
    if (!QFile::copy(kAudioFile, paddedAudio)) {
        check(false, "malformed-range: could not copy the audio fixture");
        return;
    }
    {
        QFile pad(paddedAudio);
        if (!pad.open(QIODevice::Append)) {
            check(false, "malformed-range: could not append the partial-frame tail");
            return;
        }
        pad.write(QByteArray(400, '\0'));
        pad.close();
    }
    const qint64 paddedSize = QFileInfo(paddedAudio).size();
    check(paddedSize == 4205568 + 400,
          QString("malformed-range fixture: %1 bytes = 5476 full frames + 400 (got %2)")
              .arg(4205568 + 400).arg(paddedSize));

    const QString projectPath = QDir(workDir).absoluteFilePath("repair_malformed_ranges.ttcut");
    QFile f(projectPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        check(false, "malformed-range: could not write project file");
        return;
    }
    QTextStream out(&f);
    out << "<!DOCTYPE TTCut-Projectfile>\n"
           "<TTCut-Projectfile>\n"
           " <Version>1.0</Version>\n"
           " <Video>\n"
           "  <Order>0</Order>\n"
           "  <Name>" << kVideoFile << "</Name>\n"
           "  <Audio>\n"
           "   <Order>0</Order>\n"
           "   <Name>" << paddedAudio << "</Name>\n"
           // control: comfortably inside, must stay enabled
           "   <Repair>\n"
           "    <FrameFrom>90</FrameFrom><FrameTo>100</FrameTo>\n"
           "    <Channels>12</Channels><Method>silence-fade</Method>\n"
           "   </Repair>\n"
           // M4: last frame only partially present in the file
           "   <Repair>\n"
           "    <FrameFrom>5470</FrameFrom><FrameTo>5476</FrameTo>\n"
           "    <Channels>12</Channels><Method>silence-fade</Method>\n"
           "   </Repair>\n"
           // M5: end before start
           "   <Repair>\n"
           "    <FrameFrom>200</FrameFrom><FrameTo>100</FrameTo>\n"
           "    <Channels>12</Channels><Method>silence-fade</Method>\n"
           "   </Repair>\n"
           // M5: negative start
           "   <Repair>\n"
           "    <FrameFrom>-5</FrameFrom><FrameTo>10</FrameTo>\n"
           "    <Channels>12</Channels><Method>silence-fade</Method>\n"
           "   </Repair>\n"
           "  </Audio>\n"
           " </Video>\n"
           "</TTCut-Projectfile>\n";
    f.close();

    QFile::remove(logPath);
    TTMessageLogger::getInstance()->setLogFilePath(logPath);

    TTAVData avData;
    QEventLoop loop;
    bool done = false;
    QObject::connect(&avData, &TTAVData::readProjectFileFinished, [&](const QString&) {
        done = true;
        loop.quit();
    });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    avData.readProjectFile(QFileInfo(projectPath));
    if (!done) loop.exec();

    check(done, "malformed-range: readProjectFileFinished fired");
    check(avData.avCount() == 1, "malformed-range: one AV item loaded");
    if (avData.avCount() != 1) return;

    const QList<TTAudioRepairItem> repairs = avData.avItemAt(0)->audioRepairList();
    check(repairs.size() == 4,
          QString("malformed-range: all four repair items present, none dropped (got %1)")
              .arg(repairs.size()));

    const TTAudioRepairItem* control = nullptr;
    const TTAudioRepairItem* partialTail = nullptr;
    const TTAudioRepairItem* reversed = nullptr;
    const TTAudioRepairItem* negative = nullptr;
    for (const TTAudioRepairItem& r : repairs) {
        if (r.frameFrom() == 90)   control = &r;
        if (r.frameFrom() == 5470) partialTail = &r;
        if (r.frameFrom() == 200)  reversed = &r;
        if (r.frameFrom() == -5)   negative = &r;
    }
    check(control && control->isEnabled(), "malformed-range: the in-bounds control stays enabled");
    check(partialTail && !partialTail->isEnabled(),
          "malformed-range (M4): a range whose last frame is only partially present "
          "in the file is disabled");
    check(reversed && !reversed->isEnabled(),
          "malformed-range (M5): a range with end before start is disabled");
    check(negative && !negative->isEnabled(),
          "malformed-range (M5): a range with a negative start is disabled");

    QFile logFile(logPath);
    QString logText;
    if (logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        logText = QString::fromUtf8(logFile.readAll());
        logFile.close();
    }
    check(logText.contains("5470-5476"), "malformed-range: log names the partial-tail range");
    check(logText.contains("200-100"),   "malformed-range: log names the reversed range");
    check(logText.contains("-5-10"),     "malformed-range: log names the negative range");
    check(!logText.contains("repair range 90-100"),
          "malformed-range: no warning logged for the control item");
}

// --- Case 8: StreamPoint AudioFrameFrom/AudioFrameTo round trip (R4) -------
static void testStreamPointAudioFrameRangeRoundTrip(const QString& workDir)
{
    const QString projectPath = QDir(workDir).absoluteFilePath("streampoint_afrange_roundtrip.ttcut");
    QFile::remove(projectPath);

    TTAVData avSrc;
    QEventLoop srcLoop;
    bool srcDone = false;
    QObject::connect(&avSrc, &TTAVData::threadPoolExit, [&]() {
        srcDone = true;
        srcLoop.quit();
    });
    avSrc.openAVStreams(kVideoFile);
    if (!srcDone) srcLoop.exec();

    check(avSrc.avCount() == 1, "streampoint round trip: one AV item opened");
    if (avSrc.avCount() != 1) return;

    // With range: an AudioAnomaly finding carrying the exact AC3 frame range
    // the scanner produced.
    TTStreamPoint withRange(1234, StreamPointType::AudioAnomaly,
                             QStringLiteral("LFE/center burst"), 0.87f, 0.16f);
    withRange.setAudioFrameRange(5000, 5004);
    check(withRange.hasAudioFrameRange(), "source: withRange has an audio frame range");

    // Without range: any other marker type, or an AudioAnomaly point from
    // before this field existed - must round-trip on the frameIndex/duration
    // estimate alone, not trip over missing <AudioFrameFrom>/<AudioFrameTo>.
    TTStreamPoint noRange(42, StreamPointType::ManualMarker,
                          QStringLiteral("Marker (manuell)"));
    check(!noRange.hasAudioFrameRange(), "source: noRange has no audio frame range");

    QList<TTStreamPoint> points{withRange, noRange};

    avSrc.writeProjectFile(QFileInfo(projectPath), points, TTLogoProjectData());
    check(QFileInfo::exists(projectPath), "streampoint round trip: project file written");

    QFile raw(projectPath);
    QString xmlText;
    if (raw.open(QIODevice::ReadOnly | QIODevice::Text)) {
        xmlText = QString::fromUtf8(raw.readAll());
        raw.close();
    }
    check(xmlText.contains("<AudioFrameFrom>5000</AudioFrameFrom>"),
          "written xml contains AudioFrameFrom 5000");
    check(xmlText.contains("<AudioFrameTo>5004</AudioFrameTo>"),
          "written xml contains AudioFrameTo 5004");
    // The no-range point must NOT have grown a range element of its own -
    // count occurrences rather than just "contains", so a bug that emits it
    // unconditionally for every point is caught too.
    check(xmlText.count(QStringLiteral("<AudioFrameFrom>")) == 1,
          "written xml has exactly one AudioFrameFrom element (not one per point)");

    TTAVData avDst;
    QList<TTStreamPoint> loaded;
    bool gotStreamPoints = false;
    QObject::connect(&avDst, &TTAVData::streamPointsLoaded,
                      [&](const QList<TTStreamPoint>& pts) {
        loaded = pts;
        gotStreamPoints = true;
    });
    QEventLoop dstLoop;
    bool dstDone = false;
    QObject::connect(&avDst, &TTAVData::readProjectFileFinished, [&](const QString&) {
        dstDone = true;
        dstLoop.quit();
    });
    QTimer::singleShot(20000, &dstLoop, &QEventLoop::quit);
    avDst.readProjectFile(QFileInfo(projectPath));
    if (!dstDone) dstLoop.exec();

    check(dstDone, "streampoint round trip: readProjectFileFinished fired");
    check(gotStreamPoints, "streampoint round trip: streamPointsLoaded fired");
    check(loaded.size() == 2,
          QString("streampoint round trip: two points loaded (got %1)").arg(loaded.size()));
    if (loaded.size() != 2) return;

    const TTStreamPoint* rWithRange = nullptr;
    const TTStreamPoint* rNoRange = nullptr;
    for (const TTStreamPoint& p : loaded) {
        if (p.type() == StreamPointType::AudioAnomaly) rWithRange = &p;
        if (p.type() == StreamPointType::ManualMarker)  rNoRange = &p;
    }
    check(rWithRange != nullptr, "streampoint round trip: AudioAnomaly point found");
    check(rNoRange != nullptr, "streampoint round trip: ManualMarker point found");

    if (rWithRange) {
        check(rWithRange->hasAudioFrameRange(),
              "reload: AudioAnomaly point still has an audio frame range");
        check(rWithRange->audioFrameFrom() == 5000, "reload: audioFrameFrom round-trips (5000)");
        check(rWithRange->audioFrameTo() == 5004,   "reload: audioFrameTo round-trips (5004)");
        check(rWithRange->frameIndex() == 1234,     "reload: frameIndex round-trips");
        check(rWithRange->description() == QStringLiteral("LFE/center burst"),
              "reload: description round-trips");
    }
    if (rNoRange) {
        check(!rNoRange->hasAudioFrameRange(),
              "reload: legacy/no-range point still has no audio frame range "
              "(old-project load path unaffected)");
        check(rNoRange->frameIndex() == 42, "reload: no-range point frameIndex round-trips");
    }
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    if (argc < 2) {
        fprintf(stderr, "usage: %s <workdir>\n", argv[0]);
        return 2;
    }
    const QString workDir = QString::fromUtf8(argv[1]);
    QDir().mkpath(workDir);

    if (!QFileInfo::exists(kVideoFile) || !QFileInfo::exists(kAudioFile)) {
        fprintf(stderr, "missing fixture(s): %s / %s\n",
                qPrintable(kVideoFile), qPrintable(kAudioFile));
        return 2;
    }

    testRoundTrip(workDir);
    testNoRepairElement();
    testUnknownAudioChildIgnored(workDir);
    testReorderReassignsTrack(workDir);
    testRemoveShiftsTrackIndex(workDir);
    testLoadValidationDisablesOutOfRange(workDir, QDir(workDir).absoluteFilePath("audiorepair_persist.log"));
    testLoadValidationRejectsMalformedRanges(workDir, QDir(workDir).absoluteFilePath("audiorepair_malformed.log"));
    testStreamPointAudioFrameRangeRoundTrip(workDir);

    printf("\n%s (%d failures)\n", gFailures == 0 ? "ALL PASS" : "FAILED", gFailures);
    return gFailures == 0 ? 0 : 1;
}
