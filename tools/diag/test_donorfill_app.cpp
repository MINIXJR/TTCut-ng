// The donor fill inside the application (spec 2026-10-10): which tracks can
// be donors, the donor reference through track-list changes, the cut, the
// project file and the repair dialog's fill view - offscreen, on the files
// of make_donorfill_sample.sh (built if missing) and tools/testdata.
//
//   usage: test_donorfill_app <workdir>
//
// Build via `cmake --build build --target test_donorfill_app`.
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QTextStream>
#include <QTimer>
#include <QVector>

#include <cmath>
#include <cstdio>

#include "avstream/ttavstream.h"
#include "avstream/ttavtypes.h"
#include "data/ttavlist.h"
#include "data/ttavdata.h"
#include "data/ttcutprojectdata.h"
#include "data/ttstreampoint.h"
#include "gui/ttaudiorepairdialog.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}
#include "extern/ttaudiorepair.h"
#include "extern/ttaudiorepairitem.h"

static int gFailures = 0;

static void check(bool ok, const QString& what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) gFailures++;
}

static const QString kDir = QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/donorfill_sample");
static const QString kMakeScript = QStringLiteral("/usr/local/src/TTCut-ng/tools/diag/make_donorfill_sample.sh");
static QString fx(const char* name) { return kDir + QLatin1Char('/') + QLatin1String(name); }

static bool ensureFixture()
{
    if (QFileInfo::exists(fx("unrelated.mp2"))) return true;   // the script's last output
    QProcess proc;
    proc.start(kMakeScript, {kDir});
    return proc.waitForStarted(5000) && proc.waitForFinished(300000) && proc.exitCode() == 0;
}

// An item without video that holds the given audio files, in this order.
static TTAVItem* itemOf(const QStringList& files)
{
    TTAVItem* item = new TTAVItem(nullptr);
    for (const QString& file : files) {
        TTAudioType    type(file);
        TTAudioStream* stream = type.createAudioStream();
        if (!stream) { check(false, "open " + file); continue; }
        stream->createHeaderList();
        item->appendAudioEntry(stream);
    }
    return item;
}

// The fill of hole A of hole51.ac3 from the donor at `donorTrack` (values
// measured 2026-10-10: hole 240243..240721, shift 525 resp. 925).
static TTAudioRepairItem fillA(int donorTrack, qint64 shift = 525)
{
    return TTAudioRepair::makeDonorFillItem(0, TTAudioRepair::kDonorFillMaskCentre, donorTrack,
                                            240243, 240721, shift, {1.374}, 0.977, 48000);
}

static void testItem()
{
    // 0 hole51.ac3 (5.1)  1 donor51_300.mp2  2 mono.ac3  3 donor51_300.ac3 (2.0)  4 clean51.ac3 (5.1)
    TTAVItem* item = itemOf({fx("hole51.ac3"), fx("donor51_300.mp2"), fx("mono.ac3"), fx("donor51_300.ac3"), fx("clean51.ac3")});
    check(item->audioCount() == 5, "item: five tracks opened");
    if (item->audioCount() != 5) return;

    check(item->donorCandidateTracks(0) == QList<int>({1, 3}),
          "candidates: the MP2 and the AC3 2.0 track, not mono, not 5.1, not the track itself");
    check(item->donorCandidateTracks(4) == QList<int>({1, 3}), "candidates: the same for the other 5.1 track");
    check(item->donorCandidateTracks(9).isEmpty() && item->donorCandidateTracks(-1).isEmpty(), "candidates: none for a track that does not exist");

    check(item->presetDonorTrack(0) == 1, "preset: the first candidate when no language says otherwise");
    item->onAudioLanguageChanged(0, "deu");
    item->onAudioLanguageChanged(1, "mis");
    item->onAudioLanguageChanged(3, "deu");
    check(item->presetDonorTrack(0) == 3, "preset: the candidate with the repaired track's language comes first");
    item->onAudioLanguageChanged(3, "eng");
    check(item->presetDonorTrack(0) == 1, "preset: no candidate in that language - the first one");
    check(itemOf({fx("hole51.ac3"), fx("mono.ac3")})->presetDonorTrack(0) == -1, "preset: -1 without a candidate");
    check(item->expectedDonorShift(0, 1) == 0, "expected shift: 0 without a video and its .info");

    // The donor reference through track-list changes (review focus 2).
    item->appendAudioRepair(fillA(3, 300));
    item->appendAudioRepair(TTAudioRepairItem(0, 100, 110, 0x04));
    int changed = 0;
    QObject::connect(item, &TTAVItem::audioRepairsChanged, [&]() { ++changed; });
    item->onSwapAudioItems(1, 3);            // donor 3 -> 1
    QList<TTAudioRepairItem> r = item->audioRepairList();
    check(r.size() == 2 && r[0].donorTrack() == 1 && r[0].trackIndex() == 0 && r[0].isEnabled() && changed == 1,
          QString("remap: swapping the donor's track moves the donor reference (donor %1)").arg(r.value(0).donorTrack()));
    item->onSwapAudioItems(0, 4);            // repaired track 0 -> 4
    r = item->audioRepairList();
    check(r.size() == 2 && r[0].trackIndex() == 4 && r[0].donorTrack() == 1 && r[1].trackIndex() == 4,
          "remap: swapping the repaired track moves both repairs, the donor reference stays");
    item->onRemoveAudioItem(2);              // a track between: 4 -> 3, donor 1 stays
    r = item->audioRepairList();
    check(r.size() == 2 && r[0].trackIndex() == 3 && r[0].donorTrack() == 1 && r[0].isEnabled(),
          "remap: removing another track shifts the repaired track's index");
    item->onRemoveAudioItem(1);              // the donor
    r = item->audioRepairList();
    check(r.size() == 2 && r[0].isDonorFill() && !r[0].isEnabled() && r[0].donorTrack() == -1 && r[0].trackIndex() == 2
          && r[1].isEnabled(),
          "remap: removing the donor disables the fill, keeps it, and leaves the other repair alone");
    item->onRemoveAudioItem(2);              // the repaired track
    check(item->audioRepairList().isEmpty(), "remap: removing the repaired track removes its repairs");

    // After loading: a fill whose donor is no candidate, or lies outside the donor.
    TTAVItem* loaded = itemOf({fx("hole51.ac3"), fx("donor51_300.mp2"), fx("mono.ac3")});
    // As the project loader hands them over: the donor is a saved position
    // (here equal to the list position, no track is missing).
    auto fromProject = [](TTAudioRepairItem fill) { fill.setDonorIsSavedOrder(true); return fill; };
    loaded->appendAudioRepair(fromProject(fillA(1)));                 // fine
    loaded->appendAudioRepair(fromProject(fillA(2)));                 // mono: no candidate
    loaded->appendAudioRepair(fromProject(fillA(0)));                 // the track itself
    loaded->appendAudioRepair(fromProject(fillA(7)));                 // no such track
    loaded->appendAudioRepair(fromProject(fillA(1, 5000000)));        // past the donor's end
    loaded->appendAudioRepair(fromProject(fillA(1, -400000)));        // before the donor's start
    loaded->appendAudioRepair(fillA(1, 5000000));                     // planned in this session: not this check's
    check(loaded->resolveLoadedDonorFills() == 5, "load check: five of the six loaded fills are disabled");
    r = loaded->audioRepairList();
    check(r.size() == 7 && r[0].isEnabled() && !r[1].isEnabled() && !r[2].isEnabled() && !r[3].isEnabled()
          && !r[4].isEnabled() && !r[5].isEnabled(), "load check: all kept, of the loaded ones only the sound one enabled");
    check(r.size() == 7 && r[3].donorTrack() == -1 && r[0].donorTrack() == 1 && !r[0].donorIsSavedOrder(),
          "load check: a donor that is not there becomes -1, the others are list positions now");
    check(r.size() == 7 && r[6].isEnabled(), "load check: a fill planned in this session is left alone");
    check(loaded->resolveLoadedDonorFills() == 0, "load check: a second run disables nothing more");
}

// Centre plane (index 2) of an AC3 file, decoded without dynamic range
// compression as the repair decodes it; sample i is track sample i.
static QVector<float> centreOf(const QString& path)
{
    QVector<float> out;
    AVFormatContext* fmt = nullptr;
    int idx = -1;
    QString err;
    if (!TTAudioRepair::openFirstAudioStream(path, &fmt, &idx, &err)) return out;
    const AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_AC3);
    AVCodecContext* ctx = dec ? avcodec_alloc_context3(dec) : nullptr;
    if (ctx) av_opt_set_double(ctx->priv_data, "drc_scale", 0.0, 0);
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (ctx && avcodec_open2(ctx, dec, nullptr) >= 0) {
        while (av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == idx && avcodec_send_packet(ctx, pkt) >= 0 && avcodec_receive_frame(ctx, frame) >= 0
                && frame->ch_layout.nb_channels > 2) {
                const float* d = reinterpret_cast<const float*>(frame->extended_data[2]);
                for (int n = 0; n < frame->nb_samples; ++n) out.append(d[n]);
                av_frame_unref(frame);
            }
            av_packet_unref(pkt);
        }
    }
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    return out;
}

// How many dB `repaired` is closer to `clean` than silence over count samples.
static double closerThanSilence(const QVector<float>& repaired, qint64 r0, const QVector<float>& clean, qint64 c0, qint64 count)
{
    double sig = 0.0, err = 0.0;
    for (qint64 i = 0; i < count; ++i) {
        sig += double(clean[c0 + i]) * clean[c0 + i];
        const double e = double(clean[c0 + i]) - repaired[r0 + i];
        err += e * e;
    }
    return 10.0 * std::log10(qMax(sig, 1e-30) / qMax(err, 1e-30));
}

static void testCut(const QString& workDir)
{
    // Keep 3.2 s .. 8.0 s: frames 100..249 of the source, hole A (frame 156) inside.
    auto cut = [&](const TTAudioRepairItem* repair, const QString& outFile, QStringList* notes, QStringList* reasons) {
        TTAVItem* item = itemOf({fx("hole51.ac3"), fx("donor51_300.mp2")});
        if (repair) item->appendAudioRepair(*repair);
        QFile::remove(outFile);
        TTAVData avData;
        bool ok = false;
        avData.cutAudioTracks(item, {0}, {qMakePair(3.2, 8.0)}, false,
            [&](int, const QString&) { return outFile; },
            [&](int, const QString&, const QString&, bool cutOk) { ok = cutOk; });
        if (notes) *notes = avData.audioRepairNotes();
        if (reasons) *reasons = avData.audioCutFailureReasons();
        return ok;
    };
    const QString plain = QDir(workDir).absoluteFilePath("cut_plain.ac3"), filled = QDir(workDir).absoluteFilePath("cut_filled.ac3");
    const TTAudioRepairItem fill = fillA(1);
    QStringList notes, reasons;
    // The cuts run first: the message below reads what they left behind.
    const bool bothCut = cut(nullptr, plain, nullptr, nullptr) && cut(&fill, filled, &notes, &reasons);
    check(bothCut, "cut: both cuts succeed (" + reasons.join(" | ") + ")");
    check(notes == QStringList{QStringLiteral("Audio track 1: repairs applied: 1 (frames replaced: 1)")},
          "cut: the note counts the fill: " + notes.join(" | "));

    // Where the cut starts in the source: the frame offset at which the cut
    // file's sound lines up with the source's (the keep window starts at
    // frame 100; the cutter may round by a frame).
    const QVector<float> a = centreOf(plain), b = centreOf(filled), clean = centreOf(fx("clean51.ac3")), src = centreOf(fx("hole51.ac3"));
    const qint64 hs = 240243, he = 240721;
    qint64 off = -1;
    double best = 0.0;
    for (qint64 k = 97; k <= 103 && a.size() > 20 * 1536 && src.size() > 130 * 1536; ++k) {
        double c = 0.0;
        for (qint64 i = 1536; i < 11 * 1536; ++i) c += double(a[i]) * src[k * 1536 + i];
        if (c > best) { best = c; off = k * 1536; }
    }
    check(off > 0 && a.size() == b.size() && a.size() > he - off && clean.size() > he,
          QString("cut: outputs decoded, the cut starts at source frame %1").arg(off / 1536));
    if (off <= 0 || a.size() != b.size() || a.size() <= he - off || clean.size() <= he) return;
    const double before = closerThanSilence(a, hs - off, clean, hs, he - hs), after = closerThanSilence(b, hs - off, clean, hs, he - hs);
    check(before < 1.0 && after >= 9.0,
          QString("cut: the hole is filled in the cut file (%1 dB before, %2 dB after, >= 9)").arg(before, 0, 'f', 1).arg(after, 0, 'f', 1));

    // Review focus 1: a fill whose donor is gone is skipped and counted, the cut succeeds.
    TTAudioRepairItem gone = fillA(-1);
    gone.setEnabled(false);
    const bool goneCut = cut(&gone, filled, &notes, &reasons);
    check(goneCut && notes == QStringList{QStringLiteral("Audio track 1: repairs applied: 0 (frames replaced: 0), disabled: 1")},
          "cut: disabled fill is skipped, the note says so: " + notes.join(" | "));
    // An enabled fill whose donor track does not exist fails the track's cut with a reason.
    const TTAudioRepairItem broken = fillA(5);
    const bool brokenCut = cut(&broken, filled, nullptr, &reasons);
    check(!brokenCut && !reasons.isEmpty() && reasons.first().contains("donor"),
          "cut: a fill without its donor file fails the track with a reason: " + reasons.join(" | "));
    QFile::remove(plain);
    QFile::remove(filled);
}

static const QString kVideoFile = QStringLiteral("/usr/local/src/TTCut-ng/tools/testdata/tux_test.264");

// Loads a project and waits for it; null when it did not load one item.
static TTAVItem* loadProject(TTAVData& avData, const QString& projectPath)
{
    QEventLoop loop;
    bool done = false;
    QObject::connect(&avData, &TTAVData::readProjectFileFinished, [&](const QString&) { done = true; loop.quit(); });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    avData.readProjectFile(QFileInfo(projectPath));
    if (!done) loop.exec();
    return done && avData.avCount() == 1 ? avData.avItemAt(0) : nullptr;
}

// A project with hole51.ac3 (order 0, carrying `repairs`) and the given further tracks.
static QString writeProject(const QString& workDir, const QString& name, const QString& repairs, const QStringList& moreTracks)
{
    const QString path = QDir(workDir).absoluteFilePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return QString();
    QTextStream out(&f);
    out << "<!DOCTYPE TTCut-Projectfile>\n<TTCut-Projectfile>\n <Version>1.0</Version>\n <Video>\n  <Order>0</Order>\n"
           "  <Name>" << kVideoFile << "</Name>\n  <Audio>\n   <Order>0</Order>\n   <Name>" << fx("hole51.ac3") << "</Name>\n"
        << repairs << "  </Audio>\n";
    for (int i = 0; i < moreTracks.size(); ++i)
        out << "  <Audio>\n   <Order>" << i + 1 << "</Order>\n   <Name>" << moreTracks[i] << "</Name>\n  </Audio>\n";
    out << " </Video>\n</TTCut-Projectfile>\n";
    return path;
}

static void testProject(const QString& workDir)
{
    // Round trip through the application's own writer and reader.
    QString saved;
    {
        TTAVData src;
        TTAVItem* item = loadProject(src, writeProject(workDir, "fill_src.ttcut", QString(), {fx("donor51_300.mp2"), fx("donor20_300.mp2")}));
        check(item && item->audioCount() == 3, "project: three tracks loaded");
        if (!item || item->audioCount() != 3) return;
        item->appendAudioRepair(TTAudioRepair::makeDonorFillItem(0, TTAudioRepair::kDonorFillMaskCentre, 2,
                                                                 240250, 240723, -413, {1.4601}, 0.9986, 48000));
        saved = QDir(workDir).absoluteFilePath("fill_saved.ttcut");
        src.writeProjectFile(QFileInfo(saved), {}, TTLogoProjectData());
    }
    {
        QFile f(saved);
        const QString xml = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        check(xml.contains("<Method>donor-fill</Method>") && xml.contains("<Donor>2</Donor>") && xml.contains("<HoleStart>240250</HoleStart>")
              && xml.contains("<HoleEnd>240723</HoleEnd>") && xml.contains("<DonorShift>-413</DonorShift>")
              && xml.contains("<Gain>1.4601</Gain>") && xml.contains("<Match>0.9986</Match>"),
              "project: the saved file carries the donor-fill values");
        TTAVData dst;
        TTAVItem* item = loadProject(dst, saved);
        const QList<TTAudioRepairItem> r = item ? item->audioRepairList() : QList<TTAudioRepairItem>();
        check(r.size() == 1 && r[0].isDonorFill() && r[0].isEnabled() && r[0].trackIndex() == 0 && r[0].donorTrack() == 2
              && r[0].holeStart() == 240250 && r[0].holeEnd() == 240723 && r[0].donorShift() == -413
              && r[0].gains() == QVector<double>{1.4601} && qAbs(r[0].match() - 0.9986) < 1e-9
              && r[0].frameFrom() == 156 && r[0].frameTo() == 156 && r[0].channelMask() == TTAudioRepair::kDonorFillMaskCentre,
              "project: every value comes back, the repair is enabled");
    }

    // Load validation (review focus 1 and 5). Tracks: 0 hole51.ac3, 1 donor51_300.mp2, 2 mono.ac3.
    auto fillXml = [](int from, int to, const QString& values) {
        return QString("   <Repair><FrameFrom>%1</FrameFrom><FrameTo>%2</FrameTo><Channels>4</Channels>"
                       "<Method>donor-fill</Method>%3</Repair>\n").arg(from).arg(to).arg(values);
    };
    const QString v = "<HoleStart>%1</HoleStart><HoleEnd>%2</HoleEnd><DonorShift>%3</DonorShift><Gain>1.2</Gain><Match>0.9</Match>";
    QString repairs;
    repairs += fillXml(156, 156, "<Donor>1</Donor>" + v.arg(240250).arg(240723).arg(525));     // sound
    repairs += fillXml(200, 200, "<Donor>5</Donor>" + v.arg(307500).arg(307900).arg(525));     // donor order names no track
    repairs += fillXml(210, 210, "<Donor>2</Donor>" + v.arg(322860).arg(323260).arg(525));     // donor is mono
    repairs += fillXml(220, 220, "<Donor>0</Donor>" + v.arg(338220).arg(338620).arg(525));     // donor is the track itself
    repairs += fillXml(230, 230, "<Donor>1</Donor><HoleEnd>353900</HoleEnd><DonorShift>525</DonorShift><Gain>1.2</Gain><Match>0.9</Match>");  // no HoleStart
    repairs += fillXml(240, 240, v.arg(368940).arg(369340).arg(525));                          // no Donor
    repairs += fillXml(250, 260, "<Donor>1</Donor>" + v.arg(384300).arg(384700).arg(525));     // frame range does not fit
    repairs += fillXml(270, 270, "<Donor>1</Donor><HoleStart>415020</HoleStart><HoleEnd>415420</HoleEnd><DonorShift>525</DonorShift>"
                                 "<Gain>1.2</Gain><Gain>1.1</Gain><Match>0.9</Match>");          // a gain too many
    repairs += fillXml(280, 280, "<Donor>1</Donor>" + v.arg(430380).arg(430780).arg(900000));  // past the donor's end
    repairs += "   <Repair><FrameFrom>290</FrameFrom><FrameTo>291</FrameTo><Channels>4</Channels><Method>nonsense</Method></Repair>\n";
    TTAVData avData;
    TTAVItem* item = loadProject(avData, writeProject(workDir, "fill_validation.ttcut", repairs, {fx("donor51_300.mp2"), fx("mono.ac3")}));
    const QList<TTAudioRepairItem> r = item ? item->audioRepairList() : QList<TTAudioRepairItem>();
    check(r.size() == 10, QString("load: all ten repairs are kept (got %1)").arg(r.size()));
    auto enabledAt = [&](qint64 from) { for (const TTAudioRepairItem& x : r) if (x.frameFrom() == from) return x.isEnabled(); return false; };
    check(enabledAt(156), "load: a sound fill stays enabled");
    check(!enabledAt(200), "load: donor order names no track -> disabled");
    check(!enabledAt(210), "load: the donor is no two-channel track -> disabled");
    check(!enabledAt(220), "load: the donor is the repaired track -> disabled");
    check(!enabledAt(230), "load: a missing value -> disabled");
    check(!enabledAt(240), "load: no donor given -> disabled");
    check(!enabledAt(250), "load: values that do not give the frame range -> disabled");
    check(!enabledAt(270), "load: a gain too many -> disabled");
    check(!enabledAt(280), "load: a donor range past the donor's end -> disabled");
    check(!enabledAt(290), "load: an unknown method is still disabled");

    // A track of the project that cannot be opened leaves a gap: the tracks
    // behind it move up one position. <Donor> names the saved position, so a
    // fill must find its donor by that - not take whatever track now sits at
    // the number (final review, Important 1).
    {
        QString gapRepairs;
        gapRepairs += fillXml(156, 156, "<Donor>1</Donor>" + v.arg(240250).arg(240723).arg(525));   // its donor is the missing track
        gapRepairs += fillXml(200, 200, "<Donor>2</Donor>" + v.arg(307500).arg(307900).arg(925));   // its donor moves from 2 to 1
        TTAVData gapData;
        TTAVItem* gapItem = loadProject(gapData, writeProject(workDir, "fill_gap.ttcut", gapRepairs,
                                                              {kDir + "/does-not-exist.mp2", fx("donor51_700.mp2")}));
        check(gapItem && gapItem->audioCount() == 2 && gapItem->audioStreamAt(1)->filePath() == fx("donor51_700.mp2"),
              "gap: the project loads without the missing track, the track behind it moves to position 1");
        const QList<TTAudioRepairItem> g = gapItem ? gapItem->audioRepairList() : QList<TTAudioRepairItem>();
        check(g.size() == 2, QString("gap: both fills are kept (got %1)").arg(g.size()));
        for (const TTAudioRepairItem& x : g) {
            if (x.frameFrom() == 156)
                check(!x.isEnabled() && x.donorTrack() == -1,
                      QString("gap: a fill whose donor could not be opened is disabled, not filled from the next track (enabled %1, donor %2)")
                          .arg(x.isEnabled()).arg(x.donorTrack()));
            if (x.frameFrom() == 200)
                check(x.isEnabled() && x.donorTrack() == 1,
                      QString("gap: a fill whose donor moved up follows it (enabled %1, donor %2)").arg(x.isEnabled()).arg(x.donorTrack()));
        }
    }
}

// A hole marker on the AC3 frames from..from+2, as the scan writes it.
static TTStreamPoint holeMarker(qint64 from)
{
    TTStreamPoint p(int(from * 0.032 * 25), StreamPointType::AudioAnomaly, QStringLiteral("hole"), 0.0f, 0.096f);
    p.setAudioFrameRange(from, from + 2);
    p.setAudioChannelMask(0x3F);
    p.setAudioAnomalyKind(AudioAnomalyKind::Hole);
    return p;
}

static void testDialog()
{
    const QString kHint = QStringLiteral("The donor track hardly matches here, the fill gains little.");
    // 0 hole51.ac3  1 unrelated.mp2 (other sound)  2 donor51_300.mp2  3 donor51_700.mp2
    // A track without a language in its file name starts with the system's;
    // every track gets one here.
    TTAVItem* item = itemOf({fx("hole51.ac3"), fx("unrelated.mp2"), fx("donor51_300.mp2"), fx("donor51_700.mp2")});
    item->onAudioLanguageChanged(0, "deu");
    item->onAudioLanguageChanged(1, "eng");
    item->onAudioLanguageChanged(2, "deu");
    item->onAudioLanguageChanged(3, "eng");
    const TTStreamPoint marker = holeMarker(155);
    {
        TTAudioRepairDialog d(item, marker, 0, QList<int>(), nullptr);
        QComboBox* donor = d.donorComboForTest();
        check(donor && d.holeLabelForTest() && d.shiftLabelForTest() && d.matchLabelForTest() && d.fillMessageLabelForTest()
              && !d.startSpinBoxForTest() && !d.fadeEndSpinBoxForTest(), "dialog: a hole marker opens the fill view");
        if (!donor) return;
        check(donor->count() == 3 && donor->currentData().toInt() == 2,
              QString("dialog: three donors offered, preset by language (track %1)").arg(donor->currentData().toInt()));
        TTAudioRepairItem it = d.currentItemForTest();
        check(it.isDonorFill() && it.donorTrack() == 2 && it.donorShift() == 525 && it.match() >= 0.95
              && it.holeStart() >= 239952 && it.holeEnd() <= 240864 && it.frameFrom() == 156,
              QString("dialog: the fill found is shown (shift %1, match %2)").arg(it.donorShift()).arg(it.match()));
        check(!d.holeLabelForTest()->text().isEmpty() && d.holeLabelForTest()->text().startsWith("0:00:05")
              && !d.shiftLabelForTest()->text().isEmpty() && !d.matchLabelForTest()->text().isEmpty(),
              "dialog: hole, offset and match carry values: " + d.holeLabelForTest()->text() + " / "
                  + d.shiftLabelForTest()->text() + " / " + d.matchLabelForTest()->text());
        check(d.fillMessageLabelForTest()->isHidden() && d.planButtonForTest()->isEnabled() && d.playRepairedButtonForTest()->isEnabled(),
              "dialog: a good match shows no hint, plan and audition are enabled");

        donor->setCurrentIndex(donor->findData(3));           // another donor: searched again
        it = d.currentItemForTest();
        check(it.donorTrack() == 3 && it.donorShift() == 925, QString("dialog: changing the donor searches again (shift %1)").arg(it.donorShift()));

        donor->setCurrentIndex(donor->findData(1));           // unrelated sound: the hint
        it = d.currentItemForTest();
        check(it.isDonorFill() && it.donorTrack() == 1 && it.match() < 0.80 && !d.fillMessageLabelForTest()->isHidden()
              && d.fillMessageLabelForTest()->text() == kHint && d.planButtonForTest()->isEnabled(),
              QString("dialog: below 0.80 the hint shows and planning stays possible (match %1)").arg(it.match()));

        donor->setCurrentIndex(donor->findData(2));
        d.accept();
    }
    QList<TTAudioRepairItem> r = item->audioRepairList();
    check(r.size() == 1 && r[0].isDonorFill() && r[0].donorTrack() == 2 && r[0].donorShift() == 525 && r[0].isEnabled(),
          "dialog: accept stores the fill with its donor");
    if (r.size() != 1) return;

    // Review focus 4: editing shows the stored values and stores them again unchanged.
    TTAudioRepairItem marked = r[0];
    marked.setDonorFill(2, r[0].holeStart(), r[0].holeEnd(), r[0].donorShift(), r[0].gains(), 0.4242);
    item->removeAudioRepairAt(0);
    item->appendAudioRepair(marked);
    {
        TTAudioRepairDialog d(item, marker, 0, QList<int>(), nullptr);
        check(d.donorComboForTest() && d.donorComboForTest()->currentData().toInt() == 2
              && qAbs(d.currentItemForTest().match() - 0.4242) < 1e-9, "dialog: edit keeps the stored values (no new search)");
        d.accept();
    }
    r = item->audioRepairList();
    check(r.size() == 1 && qAbs(r[0].match() - 0.4242) < 1e-9, "dialog: accepting the edit stores the same values, one repair");
    {
        TTAudioRepairDialog d(item, marker, 0, QList<int>(), nullptr);
        d.donorComboForTest()->setCurrentIndex(d.donorComboForTest()->findData(3));
        check(d.currentItemForTest().donorTrack() == 3 && d.currentItemForTest().match() >= 0.95, "dialog: changing the donor while editing searches");
        d.reject();
    }
    check(item->audioRepairList().size() == 1 && item->audioRepairList().first().donorTrack() == 2, "dialog: reject leaves the stored fill untouched");

    // A fill disabled because its donor is gone opens with the preset donor and searches.
    item->onRemoveAudioItem(2);                               // tracks now: 0 hole51, 1 unrelated, 2 donor51_700
    check(item->audioRepairList().size() == 1 && !item->audioRepairList().first().isEnabled(), "dialog: removing the donor disabled the fill");
    {
        TTAudioRepairDialog d(item, marker, 0, QList<int>(), nullptr);
        const TTAudioRepairItem it = d.currentItemForTest();
        check(d.donorComboForTest() && d.donorComboForTest()->count() == 2 && it.isDonorFill() && it.donorTrack() == d.donorComboForTest()->currentData().toInt()
              && it.isEnabled(), "dialog: a disabled fill is searched anew with a donor that exists");
        d.donorComboForTest()->setCurrentIndex(d.donorComboForTest()->findData(2));
        d.accept();
    }
    r = item->audioRepairList();
    check(r.size() == 1 && r[0].isEnabled() && r[0].donorTrack() == 2 && r[0].donorShift() == 925, "dialog: planning it again replaces the disabled fill");

    // Review focus 3: where nothing can be planned.
    auto message = [&](const QString& what, TTAVItem* it, const TTStreamPoint& m, const QString& text) {
        TTAudioRepairDialog d(it, m, 0, QList<int>(), nullptr);
        const bool ok = d.fillMessageLabelForTest() && !d.fillMessageLabelForTest()->isHidden() && d.fillMessageLabelForTest()->text() == text
                        && !d.planButtonForTest()->isEnabled() && !d.playRepairedButtonForTest()->isEnabled();
        d.accept();                                           // must not store anything
        check(ok && it->audioRepairList().isEmpty(),
              QString("dialog: messages: %1 -> \"%2\" (shown: \"%3\")").arg(what, text, d.fillMessageLabelForTest() ? d.fillMessageLabelForTest()->text() : QString()));
    };
    message("no hole", itemOf({fx("clean51.ac3"), fx("donor51_300.mp2")}), marker, "No hole found.");
    message("no donor candidate among the tracks", itemOf({fx("hole51.ac3"), fx("mono.ac3")}), marker, "There is no suitable donor track.");
    message("a hole in the frame next to the marker's", itemOf({fx("hole51.ac3"), fx("donor51_300.mp2")}), holeMarker(157),
            "The hole lies next to the marker.");
    {
        const QString junkDonor = kDir + "/donor51_junk.mp2";
        QFile in(fx("donor51_300.mp2")), out(junkDonor);
        check(in.open(QIODevice::ReadOnly) && out.open(QIODevice::WriteOnly | QIODevice::Truncate)
              && out.write(QByteArray(100, '\0')) == 100 && out.write(in.readAll()) > 0, "fixture: donor with junk before the first frame written");
        out.close();
        message("a donor that cannot be read", itemOf({fx("hole51.ac3"), junkDonor}), marker, "The donor track cannot be read.");
    }
    {
        // Editing a fill that is not applied but whose donor is still there
        // starts from that donor, not from the preset one.
        TTAVItem* it = itemOf({fx("hole51.ac3"), fx("donor51_300.mp2"), fx("donor51_700.mp2")});
        it->onAudioLanguageChanged(0, "deu");
        it->onAudioLanguageChanged(1, "deu");
        it->onAudioLanguageChanged(2, "eng");
        TTAudioRepairItem off = fillA(2, 925);
        off.setEnabled(false);
        it->appendAudioRepair(off);
        TTAudioRepairDialog d(it, marker, 0, QList<int>(), nullptr);
        const TTAudioRepairItem shown = d.currentItemForTest();
        check(it->presetDonorTrack(0) == 1 && d.donorComboForTest() && d.donorComboForTest()->currentData().toInt() == 2
              && shown.donorTrack() == 2 && shown.donorShift() == 925 && shown.isEnabled(),
              QString("dialog: a fill that is not applied is searched anew with its own donor (donor %1, shift %2)")
                  .arg(shown.donorTrack()).arg(shown.donorShift()));
    }
    message("a mono track", itemOf({fx("mono.ac3"), fx("donor51_300.mp2")}), marker, "This audio format cannot be filled.");
    {
        const QString shortDonor = kDir + "/donor51_short.mp2";
        QFile in(fx("donor51_300.mp2")), out(shortDonor);
        check(in.open(QIODevice::ReadOnly) && out.open(QIODevice::WriteOnly | QIODevice::Truncate) && out.write(in.read(576 * 200)) == 576 * 200,
              "fixture: short donor written");
        out.close();
        message("a donor that ends before the hole", itemOf({fx("hole51.ac3"), shortDonor}), marker,
                "There is no sound next to the hole to compare with.");
    }
    {
        const QString acmodFile = QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/acmod_change_sample.ac3");
        if (!QFileInfo::exists(acmodFile))
            QProcess::execute(QStringLiteral("/usr/local/src/TTCut-ng/tools/diag/make_acmod_change_sample.sh"), {acmodFile});
        // The fixture changes from 5.1 to stereo at frame 63 (2 s).
        message("a channel-layout change", itemOf({acmodFile, fx("donor51_300.mp2")}), holeMarker(62), "The audio format changes next to the hole.");
    }
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: %s <workdir>\n", argv[0]); return 2; }
    const QString workDir = QString::fromUtf8(argv[1]);
    QDir().mkpath(workDir);

    const bool fixture = ensureFixture();
    check(fixture, "fixture: donorfill_sample available (built if missing)");
    if (fixture) {
        testItem();
        testCut(workDir);
        if (QFileInfo::exists(kVideoFile)) testProject(workDir);
        else check(false, "fixture: " + kVideoFile);
        testDialog();
    }
    printf("\n%s (%d failures)\n", gFailures == 0 ? "ALL PASS" : "FAILED", gFailures);
    return gFailures == 0 ? 0 : 1;
}
