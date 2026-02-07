/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2026                                                      */
/* FILE     : ttesinfo.cpp                                                    */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2026  */
/*----------------------------------------------------------------------------*/

#include "ttesinfo.h"

#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QDebug>
#include <QRegularExpression>

// ----------------------------------------------------------------------------
// TTMarkerInfo implementation
// ----------------------------------------------------------------------------
int TTMarkerInfo::toMilliseconds() const
{
    // Parse timestamp format H:MM:SS.FF (where FF is frame number within second)
    QRegularExpression re("(\\d+):(\\d+):(\\d+)\\.(\\d+)");
    QRegularExpressionMatch match = re.match(timestamp);

    if (match.hasMatch()) {
        int hours = match.captured(1).toInt();
        int minutes = match.captured(2).toInt();
        int seconds = match.captured(3).toInt();
        int frames = match.captured(4).toInt();

        // Convert to milliseconds (assuming 25fps for frame portion)
        int ms = (hours * 3600 + minutes * 60 + seconds) * 1000;
        ms += (frames * 1000) / 25;  // Approximate frame to ms
        return ms;
    }
    return 0;
}

int TTMarkerInfo::toFrame(double fps) const
{
    // If we already have a frame number, use it
    if (frame > 0) {
        return frame;
    }

    // Otherwise calculate from timestamp
    if (fps <= 0) fps = 25.0;
    return static_cast<int>(toMilliseconds() * fps / 1000.0);
}

// ----------------------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------------------
TTESInfo::TTESInfo()
    : mLoaded(false)
    , mVideoWidth(0)
    , mVideoHeight(0)
    , mFrameRateNum(25)
    , mFrameRateDen(1)
    , mStartPts(0.0)
    , mFillerStripped(false)
    , mFillerSavedBytes(0)
    , mHasTimingInfo(false)
    , mFirstVideoPts(0.0)
    , mFirstAudioPts(0.0)
    , mAvOffsetMs(0)
{
}

TTESInfo::TTESInfo(const QString& infoFilePath)
    : TTESInfo()
{
    load(infoFilePath);
}

// ----------------------------------------------------------------------------
// Load and parse info file
// ----------------------------------------------------------------------------
bool TTESInfo::load(const QString& infoFilePath)
{
    mLoaded = false;

    QFile file(infoFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        mLastError = QString("Cannot open info file: %1").arg(infoFilePath);
        return false;
    }

    QTextStream in(&file);
    QString currentSection;
    QMap<QString, QString> currentValues;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();

        // Skip empty lines and comments
        if (line.isEmpty() || line.startsWith('#')) {
            // Check for source file in comments
            if (line.startsWith("# Source:")) {
                mSourceFile = line.mid(9).trimmed();
            }
            continue;
        }

        // Section header [section]
        if (line.startsWith('[') && line.endsWith(']')) {
            // Process previous section
            if (!currentSection.isEmpty()) {
                parseSection(currentSection, currentValues);
            }
            currentSection = line.mid(1, line.length() - 2);
            currentValues.clear();
            continue;
        }

        // Key=value pair
        int eqPos = line.indexOf('=');
        if (eqPos > 0) {
            QString key = line.left(eqPos).trimmed();
            QString value = line.mid(eqPos + 1).trimmed();
            currentValues[key] = value;
        }
    }

    // Process last section
    if (!currentSection.isEmpty()) {
        parseSection(currentSection, currentValues);
    }

    file.close();
    mLoaded = true;

    qDebug() << "Loaded ES info:" << infoFilePath;
    qDebug() << "  Video:" << mVideoFile << mVideoCodec;
    qDebug() << "  Resolution:" << mVideoWidth << "x" << mVideoHeight;
    qDebug() << "  Frame rate:" << mFrameRateNum << "/" << mFrameRateDen << "=" << frameRate();
    qDebug() << "  Audio tracks:" << mAudioTracks.size();

    return true;
}

// ----------------------------------------------------------------------------
// Parse a section
// ----------------------------------------------------------------------------
bool TTESInfo::parseSection(const QString& section, const QMap<QString, QString>& values)
{
    if (section == "video") {
        mVideoFile = values.value("file");
        mVideoCodec = values.value("codec");
        mVideoWidth = values.value("width", "0").toInt();
        mVideoHeight = values.value("height", "0").toInt();
        mStartPts = values.value("start_pts", "0").toDouble();
        mFillerStripped = (values.value("filler_stripped", "false") == "true");
        mFillerSavedBytes = values.value("filler_saved_bytes", "0").toLongLong();

        // Parse frame_rate (can be "50/1" or "25" or "29.97")
        QString frameRateStr = values.value("frame_rate", "25/1");
        parseFrameRate(frameRateStr);
    }
    else if (section == "audio") {
        int count = values.value("count", "0").toInt();
        mAudioTracks.clear();

        for (int i = 0; i < count; ++i) {
            TTAudioTrackInfo track;
            track.file = values.value(QString("audio_%1_file").arg(i));
            track.codec = values.value(QString("audio_%1_codec").arg(i));
            track.language = values.value(QString("audio_%1_lang").arg(i), "und");
            mAudioTracks.append(track);
        }
    }
    else if (section == "markers") {
        int count = values.value("count", "0").toInt();
        mMarkers.clear();

        for (int i = 0; i < count; ++i) {
            QString markerStr = values.value(QString("marker_%1").arg(i));
            if (markerStr.isEmpty()) continue;

            // Parse format: timestamp|frame|type|verified
            // Example: 0:15:58.14|23964|mark|*
            QStringList parts = markerStr.split('|');
            if (parts.size() >= 3) {
                TTMarkerInfo marker;
                marker.timestamp = parts[0];
                marker.frame = parts[1].toInt();
                marker.type = parts[2];
                marker.verified = (parts.size() > 3 && parts[3] == "*");
                mMarkers.append(marker);
            }
        }

        if (!mMarkers.isEmpty()) {
            qDebug() << "  VDR Markers:" << mMarkers.size();
        }
    }
    else if (section == "timing") {
        // A/V sync offset information
        mFirstVideoPts = values.value("first_video_pts", "0").toDouble();
        mFirstAudioPts = values.value("first_audio_pts", "0").toDouble();
        mAvOffsetMs = values.value("av_offset_ms", "0").toInt();
        mHasTimingInfo = true;

        if (mAvOffsetMs != 0) {
            qDebug() << "  A/V offset:" << mAvOffsetMs << "ms";
        }
    }

    return true;
}

// ----------------------------------------------------------------------------
// Parse frame rate string (e.g., "50/1", "25", "29.97")
// ----------------------------------------------------------------------------
bool TTESInfo::parseFrameRate(const QString& frameRateStr)
{
    // Try rational format first (e.g., "50/1", "30000/1001")
    if (frameRateStr.contains('/')) {
        QStringList parts = frameRateStr.split('/');
        if (parts.size() == 2) {
            mFrameRateNum = parts[0].toInt();
            mFrameRateDen = parts[1].toInt();
            if (mFrameRateDen == 0) mFrameRateDen = 1;
            return true;
        }
    }

    // Try decimal format (e.g., "29.97")
    bool ok;
    double fps = frameRateStr.toDouble(&ok);
    if (ok && fps > 0) {
        // Convert to rational
        // Common frame rates
        if (qAbs(fps - 23.976) < 0.01) {
            mFrameRateNum = 24000;
            mFrameRateDen = 1001;
        } else if (qAbs(fps - 29.97) < 0.01) {
            mFrameRateNum = 30000;
            mFrameRateDen = 1001;
        } else if (qAbs(fps - 59.94) < 0.01) {
            mFrameRateNum = 60000;
            mFrameRateDen = 1001;
        } else {
            // Approximate with integer
            mFrameRateNum = qRound(fps);
            mFrameRateDen = 1;
        }
        return true;
    }

    // Default to 25 fps
    mFrameRateNum = 25;
    mFrameRateDen = 1;
    return false;
}

// ----------------------------------------------------------------------------
// Get frame rate as double
// ----------------------------------------------------------------------------
double TTESInfo::frameRate() const
{
    if (mFrameRateDen == 0) return 25.0;
    return static_cast<double>(mFrameRateNum) / static_cast<double>(mFrameRateDen);
}

// ----------------------------------------------------------------------------
// Get frame duration in seconds
// ----------------------------------------------------------------------------
double TTESInfo::frameDurationSeconds() const
{
    double fr = frameRate();
    if (fr <= 0) return 0.04; // Default to 25fps
    return 1.0 / fr;
}

// ----------------------------------------------------------------------------
// Get frame duration in given time base
// E.g., for time base 90000 (common in TS), 25fps gives 3600 ticks per frame
// ----------------------------------------------------------------------------
int64_t TTESInfo::frameDurationInTimeBase(int64_t timeBase) const
{
    if (mFrameRateNum == 0) return timeBase / 25;
    return (timeBase * mFrameRateDen) / mFrameRateNum;
}

// ----------------------------------------------------------------------------
// Get audio track info
// ----------------------------------------------------------------------------
TTAudioTrackInfo TTESInfo::audioTrack(int index) const
{
    if (index >= 0 && index < mAudioTracks.size()) {
        return mAudioTracks[index];
    }
    return TTAudioTrackInfo();
}

// ----------------------------------------------------------------------------
// Get list of audio file names
// ----------------------------------------------------------------------------
QStringList TTESInfo::audioFiles() const
{
    QStringList files;
    for (const TTAudioTrackInfo& track : mAudioTracks) {
        if (!track.file.isEmpty()) {
            files.append(track.file);
        }
    }
    return files;
}

// ----------------------------------------------------------------------------
// Get marker info by index
// ----------------------------------------------------------------------------
TTMarkerInfo TTESInfo::marker(int index) const
{
    if (index >= 0 && index < mMarkers.size()) {
        return mMarkers[index];
    }
    return TTMarkerInfo();
}

// ----------------------------------------------------------------------------
// Find .info file for a video file
// E.g., for "Petrocelli_5min_video.264" looks for:
//   1. "Petrocelli_5min_video.info"
//   2. "Petrocelli_5min.info" (base name without _video suffix)
// ----------------------------------------------------------------------------
QString TTESInfo::findInfoFile(const QString& videoFilePath)
{
    QFileInfo videoInfo(videoFilePath);
    QString dir = videoInfo.absolutePath();
    QString baseName = videoInfo.completeBaseName();

    // Try 1: Same name with .info extension
    QString infoPath = dir + "/" + baseName + ".info";
    if (QFile::exists(infoPath)) {
        return infoPath;
    }

    // Try 2: Remove _video suffix
    if (baseName.endsWith("_video")) {
        baseName = baseName.left(baseName.length() - 6);
        infoPath = dir + "/" + baseName + ".info";
        if (QFile::exists(infoPath)) {
            return infoPath;
        }
    }

    // Try 3: Look for any .info file in the directory that matches the base
    QDir directory(dir);
    QStringList infoFiles = directory.entryList(QStringList() << "*.info", QDir::Files);
    for (const QString& infoFile : infoFiles) {
        // Check if the info file's base matches our video file's base
        QString infoBase = QFileInfo(infoFile).completeBaseName();
        if (baseName.startsWith(infoBase) || infoBase.startsWith(baseName.left(baseName.indexOf('_')))) {
            return dir + "/" + infoFile;
        }
    }

    return QString(); // Not found
}
