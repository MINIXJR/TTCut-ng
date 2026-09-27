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
#include "ttframeindexer.h"
#include "ttesinfo.h"
#include "../common/ttcut.h"
#include "../common/ttsettings.h"
#include "../common/ttexception.h"
#include "../common/istatusreporter.h"

#include <QDebug>

#include <cmath>

TTH26xVideoStream::TTH26xVideoStream(const QFileInfo& fInfo)
    : TTVideoStream(fInfo)
{
    mLog = TTMessageLogger::getInstance();
}

float TTH26xVideoStream::frameRate()
{
    return frame_rate;
}

bool TTH26xVideoStream::openStream()
{
    if (mProbed) {
        return true;  // already probed
    }

    QString err;
    TTVideoProbe probe;
    if (!ttProbeVideo(filePath(), &probe, &err)) {
        mLog->errorMsg(__FILE__, __LINE__,
            QString("Failed to open %1 stream: %2").arg(codecLabel(), err));
        return false;
    }

    if (probe.codecType != expectedCodec()) {
        mLog->errorMsg(__FILE__, __LINE__,
            QString("File is not %1, detected: %2")
                .arg(codecLabel(), ttCodecTypeToString(probe.codecType)));
        return false;
    }

    mProbe  = probe;
    mProbed = true;
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

    const int videoStreamIdx = mProbe.videoStreamIndex;
    if (videoStreamIdx < 0) {
        mLog->errorMsg(__FILE__, __LINE__, "No video stream found");
        emit statusReport(StatusReportArgs::Error, tr("No video stream found"), 0);
        return -1;
    }

    const TTStreamInfo& probe = mProbe.info;
    bit_rate = static_cast<float>(probe.bitRate) / 1000.0f;

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

    // .info first (PAFF field rate halved), else the SPS timing, else 25
    frame_rate = static_cast<float>(TTFrameIndexer::effectiveFrameRate(
        probe.frameRate, filePath(), mFrameIndexBundle.isPAFF, &mFrameRateOrigin));

    mLog->infoMsg(__FILE__, __LINE__,
        QString("%1 stream: %2x%3 @ %4 fps (SPS timing %5 fps%6), profile %7, level %8")
            .arg(codecLabel())
            .arg(probe.width)
            .arg(probe.height)
            .arg(frame_rate, 0, 'f', 3)
            .arg(probe.frameRate, 0, 'f', 3)
            .arg(mFrameIndexBundle.isPAFF ? ", PAFF" : "")
            .arg(probe.profile)
            .arg(probe.level));
    // A .info without a usable frame_rate line counts as no .info here.
    const QString noInfo = TTESInfo::findInfoFile(filePath()).isEmpty()
        ? QStringLiteral("no .info") : QStringLiteral(".info without frame_rate");
    switch (mFrameRateOrigin) {
    case TTFrameRateOrigin::StreamTiming:
        mLog->infoMsg(__FILE__, __LINE__,
            QString("%1: %2, frame rate from the SPS timing: %3 fps")
                .arg(filePath(), noInfo).arg(frame_rate, 0, 'f', 3));
        break;
    case TTFrameRateOrigin::Assumed:
        mLog->warningMsg(__FILE__, __LINE__,
            QString("%1: %2 and no SPS timing, frame rate assumed: %3 fps")
                .arg(filePath(), noInfo).arg(frame_rate, 0, 'f', 3));
        break;
    case TTFrameRateOrigin::Info:
        if (probe.frameRate > 0 && std::fabs(frame_rate - probe.frameRate) > 0.001 * frame_rate)
            mLog->warningMsg(__FILE__, __LINE__,
                QString("%1: .info frame rate %2 fps disagrees with the SPS timing %3 fps - using .info")
                    .arg(filePath()).arg(frame_rate, 0, 'f', 3).arg(probe.frameRate, 0, 'f', 3));
        break;
    }

    // The GOP table is part of the bundle the indexer produced; the Step report
    // stays so the progress sequence is unchanged.
    emit statusReport(StatusReportArgs::Step, tr("Building GOP index..."), 82 * total / 100);

    emit statusReport(StatusReportArgs::Step, tr("Processing frames..."), 90 * total / 100);

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
        vidIndex->setPictureCodingType(accessUnitCodingType(i));
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
    // `frameIndex` is a DISPLAY position (the cut-out preview window,
    // ttPreviewCutOutWindow). A true IDR (NAL scan, TTFrameInfo::isIDR) - not
    // the libav key flag, which also marks H.264 recovery points and every
    // HEVC IRAP. The walk goes down DISPLAY positions: in decode order a
    // picture decoded after its IDR can display before it (HEVC RADL), and the
    // result would lie behind the position.
    const QList<TTFrameInfo>& index = mFrameIndexBundle.index;
    for (int disp = qMin(frameIndex, frameCount() - 1); disp >= 0; --disp) {
        const int dec = displayToDecodeIndex(disp);
        if (dec >= 0 && dec < index.size() && index[dec].isIDR) return disp;
    }
    return -1;
}

bool TTH26xVideoStream::accessUnitIsRAP(int idx) const
{
    if (idx < 0 || idx >= accessUnitCount()) return false;
    return mFrameIndexBundle.index[idx].isKeyframe;
}

int TTH26xVideoStream::accessUnitCodingType(int idx) const
{
    // The indexer writes AV_PICTURE_TYPE_I/P/B (1/2/3) for every entry, I for
    // every key picture; TTVideoIndex uses the same numbering.
    if (idx < 0 || idx >= accessUnitCount()) return 1;
    return mFrameIndexBundle.index[idx].frameType;
}

int TTH26xVideoStream::decodeToDisplayIndex(int index) const
{
    // An invalid (not yet built) map returns the input unchanged
    // (ttdisplayordermap.cpp), which is what the old `mFFmpeg ? … : index` did.
    return mFrameIndexBundle.displayMap.decodeToDisplay(index);
}

int TTH26xVideoStream::displayToDecodeIndex(int index) const
{
    return mFrameIndexBundle.displayMap.displayToDecode(index);
}

const TTDisplayOrderMap& TTH26xVideoStream::displayOrderMap() const
{
    return mFrameIndexBundle.displayMap;
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
    const int merged = mFrameIndexBundle.rawToMergedIndex(raw);
    if (merged < 0) return -1;
    return decodeToDisplayIndex(merged);
}

bool TTH26xVideoStream::rawAuIsCollapsedField(int raw) const
{
    return mFrameIndexBundle.rawIsCollapsedField(raw);
}
