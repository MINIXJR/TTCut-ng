/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "tth26xvideostream.h"
#include "ttvideoindexlist.h"
#include "ttesinfo.h"
#include "../extern/ttframeindexer.h"
#include "../common/ttcut.h"
#include "../common/ttsettings.h"
#include "../common/ttexception.h"
#include "../common/istatusreporter.h"

#include <QDebug>

TTH26xVideoStream::TTH26xVideoStream(const QFileInfo& fInfo)
    : TTVideoStream(fInfo)
    , mFFmpeg(nullptr)
{
    mLog = TTMessageLogger::getInstance();
}

TTH26xVideoStream::~TTH26xVideoStream()
{
    if (mFFmpeg) {
        mFFmpeg->closeFile();
        delete mFFmpeg;
        mFFmpeg = nullptr;
    }
}

float TTH26xVideoStream::frameRate()
{
    return frame_rate;
}

bool TTH26xVideoStream::openStream()
{
    if (mFFmpeg != nullptr) {
        return true;  // already open
    }

    mFFmpeg = new TTFFmpegWrapper();
    if (!mFFmpeg->openFile(filePath())) {
        mLog->errorMsg(__FILE__, __LINE__,
            QString("Failed to open %1 stream: %2").arg(codecLabel(), mFFmpeg->lastError()));
        delete mFFmpeg;
        mFFmpeg = nullptr;
        return false;
    }

    TTVideoCodecType detected = mFFmpeg->detectVideoCodec();
    if (detected != expectedCodec()) {
        mLog->errorMsg(__FILE__, __LINE__,
            QString("File is not %1, detected: %2")
                .arg(codecLabel(), TTFFmpegWrapper::codecTypeToString(detected)));
        delete mFFmpeg;
        mFFmpeg = nullptr;
        return false;
    }

    mLog->infoMsg(__FILE__, __LINE__,
        QString("Opened %1 stream: %2").arg(codecLabel(), filePath()));
    return true;
}

int TTH26xVideoStream::createHeaderList()
{
    // Progress domain is the video file's byte size, matching what MPEG-2's
    // createHeaderList() registers (stream_buffer->size()). The pool sums
    // Start totals across all open tasks (4 OpenAudioTask byte totals plus
    // this one) into a single overall percentage - if this task registered
    // its internal 0-100 percent scale instead, it would be swamped by the
    // audio tasks' byte totals and carry effectively zero weight in the bar,
    // even though building the video index is the dominant wall-time cost.
    qint64 fileSize = QFileInfo(filePath()).size();
    quint64 total = (fileSize > 0) ? static_cast<quint64>(fileSize) : 100;

    emit statusReport(StatusReportArgs::Start,
        tr("Opening %1 stream...").arg(codecLabel()), total);

    if (!openStream()) {
        emit statusReport(StatusReportArgs::Error,
            tr("Failed to open %1 stream").arg(codecLabel()), 0);
        return -1;
    }

    mLog->infoMsg(__FILE__, __LINE__,
        QString("Creating %1 header list...").arg(codecLabel()));
    emit statusReport(StatusReportArgs::Step,
        tr("Creating %1 header list...").arg(codecLabel()), 10 * total / 100);

    int videoStreamIdx = mFFmpeg->findBestVideoStream();
    if (videoStreamIdx < 0) {
        mLog->errorMsg(__FILE__, __LINE__, "No video stream found");
        emit statusReport(StatusReportArgs::Error, tr("No video stream found"), 0);
        return -1;
    }

    TTStreamInfo streamInfo = mFFmpeg->getStreamInfo(videoStreamIdx);

    // Reset and build SPS via derived
    resetSPS();
    buildSPSFromStreamInfo(streamInfo);

    frame_rate = static_cast<float>(streamInfo.frameRate);

    // .info file overrides ffmpeg's frame-rate detection if present
    QString infoFile = TTESInfo::findInfoFile(filePath());
    if (!infoFile.isEmpty()) {
        TTESInfo esInfo(infoFile);
        if (esInfo.isLoaded() && esInfo.frameRate() > 0) {
            frame_rate = static_cast<float>(esInfo.frameRate());
            setSPSFrameRate(esInfo.frameRate());
            mLog->infoMsg(__FILE__, __LINE__,
                QString("Using frame rate from .info file: %1 fps").arg(frame_rate));
        }
    }

    bit_rate = static_cast<float>(streamInfo.bitRate) / 1000.0f;

    mLog->infoMsg(__FILE__, __LINE__,
        QString("%1 stream: %2x%3 @ %4 fps, %5")
            .arg(codecLabel())
            .arg(streamInfo.width)
            .arg(streamInfo.height)
            .arg(frame_rate, 0, 'f', 2)
            .arg(spsDescription()));

    emit statusReport(StatusReportArgs::Step, tr("Building frame index..."), 10 * total / 100);

    // The indexer's progress percent (0-100) is first mapped onto the milestone
    // scale used by this method's own Step calls (10/82/90, see below), then
    // that mapped value is scaled onto the byte-domain `total` so all Step
    // values emitted by this task share one consistent unit.
    TTFrameIndexer indexer;
    const bool indexed = indexer.build(filePath(), videoStreamIdx,
        [this, total](int percent, const QString&) {
            int mapped = 10 + percent * 70 / 100;
            quint64 value = static_cast<quint64>(mapped) * total / 100;
            emit statusReport(StatusReportArgs::Step, tr("Building frame index..."), value);
        });
    if (!indexed) {
        mLog->errorMsg(__FILE__, __LINE__,
            QString("Failed to build frame index: %1").arg(indexer.lastError()));
        emit statusReport(StatusReportArgs::Error, tr("Failed to build frame index"), 0);
        return -1;
    }
    mFrameIndexBundle = indexer.bundle();
    mFFmpeg->setFrameIndex(mFrameIndexBundle);

    // PAFF correction (H.264 only — H.265 returns false from the hook)
    if (isPAFFCorrectionApplicable() && mFFmpeg->isPAFF() && frame_rate > 30) {
        mLog->infoMsg(__FILE__, __LINE__,
            QString("PAFF detected: correcting frame rate from %1 to %2 fps")
                .arg(frame_rate).arg(frame_rate / 2.0f));
        frame_rate /= 2.0f;
        setSPSFrameRate(static_cast<double>(frame_rate));
    }

    // The GOP table is part of the bundle the indexer produced; the Step report
    // stays so the progress sequence is unchanged.
    emit statusReport(StatusReportArgs::Step, tr("Building GOP index..."), 82 * total / 100);

    emit statusReport(StatusReportArgs::Step, tr("Processing frames..."), 90 * total / 100);
    buildAccessUnits();

    int n = accessUnitCount();
    mLog->infoMsg(__FILE__, __LINE__,
        QString("%1 header list created: %2 frames, %3 GOPs")
            .arg(codecLabel()).arg(n).arg(mFrameIndexBundle.gops.size()));

    // total (bytes), not a literal 100: TTThreadTask::onStatusReport() divides
    // this by mTotalSteps (== total, set from the Start value above) to derive
    // the individual task's own percentage() - a literal 100 here would make
    // that division collapse to ~0% once total is in the hundreds-of-MB range.
    emit statusReport(StatusReportArgs::Finished,
        tr("%1 header list created").arg(codecLabel()), total);

    return n;
}

int TTH26xVideoStream::createIndexList()
{
    if (accessUnitCount() == 0) {
        mLog->errorMsg(__FILE__, __LINE__,
            "Cannot create index list: no frames in header list");
        return -1;
    }

    if (index_list == nullptr) {
        index_list = new TTVideoIndexList();
    }

    int n = accessUnitCount();
    for (int i = 0; i < n; ++i) {
        const int disp = decodeToDisplayIndex(i);
        // Dropped RASL leading pics (NoRaslOutputFlag, HEVC) have no display
        // position and are not output by any decoder -> not navigable/cuttable.
        // Excluding them makes frameCount() == the decoder/playback frame count.
        if (disp < 0) continue;
        TTVideoIndex* vidIndex = new TTVideoIndex();
        // Real display rank from the POC map (identity for streams without
        // B-reorder, and for MPEG-2). sortDisplayOrder() at open then makes
        // list position == display position, and headerListIndex(pos) ==
        // decode-order AU — the same semantics MPEG-2 has via temporal_reference.
        vidIndex->setDisplayOrder(disp);
        vidIndex->setHeaderListIndex(i);
        vidIndex->setPictureCodingType(accessUnitToCodingType(i));
        index_list->add(vidIndex);
    }

    mLog->infoMsg(__FILE__, __LINE__,
        QString("%1 index list created: %2 entries")
            .arg(codecLabel()).arg(index_list->count()));

    return index_list->count();
}

void TTH26xVideoStream::cut(int start, int end, TTCutParameter* /*cp*/)
{
    Q_UNUSED(start);
    Q_UNUSED(end);
    throw TTInvalidOperationException(__FILE__, __LINE__,
        QString("%1 stream cut() is a deprecated stub; use TTESSmartCut instead")
            .arg(codecLabel()));
}

bool TTH26xVideoStream::isCutInPoint(int pos)
{
    if (TTSettings::instance()->encoderMode()) return true;

    // `index` is a DISPLAY position (navigation is display-order since 7f494e0).
    // Bound in DISPLAY space: frameCount() (== index_list count) excludes dropped
    // HEVC RASL leading pics; accessUnitCount() (raw decode AUs) would admit
    // phantom positions. The AU array is decode-ordered, so convert before lookup.
    int index = (pos < 0) ? currentIndex() : pos;
    if (index < 0 || index >= frameCount()) return false;

    return accessUnitIsRAP(displayToDecodeIndex(index));
}

bool TTH26xVideoStream::isCutOutPoint(int pos)
{
    if (TTSettings::instance()->encoderMode()) return true;

    // `index` is a DISPLAY position (navigation is display-order since 7f494e0).
    // The AU array is indexed in DECODE order, so convert before each AU lookup.
    // The end-of-stream check (index == n-1) stays in DISPLAY space — the last
    // displayed frame is always a valid cut-out regardless of decode order.
    // n is the DISPLAY-space count (frameCount() == index_list count), which for
    // HEVC excludes dropped RASL leading pics; using accessUnitCount() (raw n)
    // here would miss the EOS shortcut for the true last displayed frame.
    int index = (pos < 0) ? currentIndex() : pos;
    int n = frameCount();
    if (index < 0 || index >= n) return false;

    if (index == n - 1) return true;
    if (index + 1 < n && accessUnitIsRAP(displayToDecodeIndex(index + 1))) return true;
    return false;
}

int TTH26xVideoStream::findIDRBefore(int frameIndex)
{
    // `frameIndex` is a DISPLAY position (caller in ttcutpreviewtask.cpp supplies
    // cutOutIndex(), which is stored in display space since 7f494e0).
    // The AU array is decode-ordered, so convert on the way in and on the way out.
    int decodeStart = displayToDecodeIndex(frameIndex);
    for (int i = decodeStart; i >= 0; --i) {
        if (accessUnitIsIDR(i)) return decodeToDisplayIndex(i);
    }
    return -1;
}

int TTH26xVideoStream::decodeToDisplayIndex(int index) const
{
    return mFFmpeg ? mFFmpeg->displayOrderMap().decodeToDisplay(index) : index;
}

int TTH26xVideoStream::displayToDecodeIndex(int index) const
{
    return mFFmpeg ? mFFmpeg->displayOrderMap().displayToDecode(index) : index;
}


const TTDisplayOrderMap& TTH26xVideoStream::displayOrderMap() const
{
    static const TTDisplayOrderMap empty;
    return mFFmpeg ? mFFmpeg->displayOrderMap() : empty;
}

TTFrameIndexBundle TTH26xVideoStream::ffmpegFrameIndexBundle() const
{
    return mFrameIndexBundle;
}

// See header doc + spec 2026-06-05. QList<TTFrameInfo> is Qt copy-on-write:
// setFrameIndex only copies the COW header here; a later lazy
// deliveredDecodeIndex write in the consumer detaches its own copy → no data
// races between parallel wrappers.
bool TTH26xVideoStream::provideFrameIndexTo(TTFFmpegWrapper* consumer) const
{
    if (!consumer)
        return false;
    const TTFrameIndexBundle bundle = ffmpegFrameIndexBundle();
    if (bundle.isEmpty())
        return false;                 // not built yet → caller builds itself
    consumer->setFrameIndex(bundle);  // index + stream metadata in one step
    return true;
}

int TTH26xVideoStream::rawAuCount() const
{
    return mFrameIndexBundle.rawPacketCount;
}

// raw AU -> merged frame. See TTFrameIndexBundle::rawToMergedIndex for the
// encoding; an empty map means "no PAFF merge happened", i.e. raw numbering
// IS merged numbering.
int TTH26xVideoStream::mapRawAuToDisplayIndex(int raw) const
{
    if (!mFFmpeg) return -1;
    const int merged = mFrameIndexBundle.rawToMergedIndex(raw);
    if (merged < 0) return -1;
    return mFFmpeg->displayOrderMap().decodeToDisplay(merged);
}

bool TTH26xVideoStream::rawAuIsCollapsedField(int raw) const
{
    return mFrameIndexBundle.rawIsCollapsedField(raw);
}
