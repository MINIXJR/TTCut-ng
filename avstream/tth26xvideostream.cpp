/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                                */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : tth26xvideostream.cpp                                           */
/*----------------------------------------------------------------------------*/

#include "tth26xvideostream.h"
#include "ttvideoindexlist.h"
#include "ttesinfo.h"
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

bool TTH26xVideoStream::closeStream()
{
    if (mFFmpeg) {
        mFFmpeg->closeFile();
        delete mFFmpeg;
        mFFmpeg = nullptr;
    }
    return true;
}

int TTH26xVideoStream::createHeaderList()
{
    emit statusReport(StatusReportArgs::Start,
        tr("Opening %1 stream...").arg(codecLabel()), 100);

    if (!openStream()) {
        emit statusReport(StatusReportArgs::Error,
            tr("Failed to open %1 stream").arg(codecLabel()), 0);
        return -1;
    }

    // Forward FFmpeg progress to statusReport (buildFrameIndex is the slow part).
    // Must be done after openStream() since that is what creates mFFmpeg.
    connect(mFFmpeg, &TTFFmpegWrapper::progressChanged, this,
            [this](int percent, const QString&) {
        int mapped = 10 + percent * 70 / 100;
        emit statusReport(StatusReportArgs::Step, tr("Building frame index..."), mapped);
    });

    mLog->infoMsg(__FILE__, __LINE__,
        QString("Creating %1 header list...").arg(codecLabel()));
    emit statusReport(StatusReportArgs::Step,
        tr("Creating %1 header list...").arg(codecLabel()), 10);

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

    emit statusReport(StatusReportArgs::Step, tr("Building frame index..."), 10);

    if (!mFFmpeg->buildFrameIndex(videoStreamIdx)) {
        mLog->errorMsg(__FILE__, __LINE__,
            QString("Failed to build frame index: %1").arg(mFFmpeg->lastError()));
        disconnect(mFFmpeg, &TTFFmpegWrapper::progressChanged, this, nullptr);
        emit statusReport(StatusReportArgs::Error, tr("Failed to build frame index"), 0);
        return -1;
    }

    disconnect(mFFmpeg, &TTFFmpegWrapper::progressChanged, this, nullptr);

    // PAFF correction (H.264 only — H.265 returns false from the hook)
    if (isPAFFCorrectionApplicable() && mFFmpeg->isPAFF() && frame_rate > 30) {
        mLog->infoMsg(__FILE__, __LINE__,
            QString("PAFF detected: correcting frame rate from %1 to %2 fps")
                .arg(frame_rate).arg(frame_rate / 2.0f));
        frame_rate /= 2.0f;
        setSPSFrameRate(static_cast<double>(frame_rate));
    }

    emit statusReport(StatusReportArgs::Step, tr("Building GOP index..."), 82);
    mFFmpeg->buildGOPIndex();

    emit statusReport(StatusReportArgs::Step, tr("Processing frames..."), 90);
    buildAccessUnits();

    int n = accessUnitCount();
    mLog->infoMsg(__FILE__, __LINE__,
        QString("%1 header list created: %2 frames, %3 GOPs")
            .arg(codecLabel()).arg(n).arg(mFFmpeg->gopCount()));

    emit statusReport(StatusReportArgs::Finished,
        tr("%1 header list created").arg(codecLabel()), 100);

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
        TTVideoIndex* vidIndex = new TTVideoIndex();
        vidIndex->setDisplayOrder(i);
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

    int index = (pos < 0) ? currentIndex() : pos;
    if (index < 0 || index >= accessUnitCount()) return false;

    return accessUnitIsRAP(index);
}

bool TTH26xVideoStream::isCutOutPoint(int pos)
{
    if (TTSettings::instance()->encoderMode()) return true;

    int index = (pos < 0) ? currentIndex() : pos;
    int n = accessUnitCount();
    if (index < 0 || index >= n) return false;

    if (index == n - 1) return true;
    if (index + 1 < n && accessUnitIsRAP(index + 1)) return true;
    return false;
}

int TTH26xVideoStream::findIDRBefore(int frameIndex)
{
    for (int i = frameIndex; i >= 0; --i) {
        if (accessUnitIsIDR(i)) return i;
    }
    return -1;
}

int TTH26xVideoStream::gopCount() const
{
    return mFFmpeg ? mFFmpeg->gopCount() : 0;
}

int TTH26xVideoStream::findGOPForFrame(int frameIndex)
{
    return mFFmpeg ? mFFmpeg->findGOPForFrame(frameIndex) : -1;
}

int TTH26xVideoStream::getGOPStart(int gopIndex)
{
    if (mFFmpeg && gopIndex >= 0 && gopIndex < mFFmpeg->gopCount()) {
        return mFFmpeg->gopIndex()[gopIndex].startFrame;
    }
    return -1;
}

int TTH26xVideoStream::getGOPEnd(int gopIndex)
{
    if (mFFmpeg && gopIndex >= 0 && gopIndex < mFFmpeg->gopCount()) {
        return mFFmpeg->gopIndex()[gopIndex].endFrame;
    }
    return -1;
}

const QList<TTFrameInfo>& TTH26xVideoStream::ffmpegFrameIndex() const
{
    return mFFmpeg->frameIndex();
}
