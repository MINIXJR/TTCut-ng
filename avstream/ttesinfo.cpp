/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttesinfo.h"

#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QDebug>
#include <QRegularExpression>

#include <algorithm>

#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"

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
    , mHasWarnings(false)
    , mDecodeErrors(0)
    , mDecodeErrorRegionCount(0)
    , mRecommendProjectX(false)
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

    if (TTSettings::instance()->logAVStream()) {
        qDebug() << "Loaded ES info:" << infoFilePath;
        qDebug() << "  Video:" << mVideoFile << mVideoCodec;
        qDebug() << "  Resolution:" << mVideoWidth << "x" << mVideoHeight;
        qDebug() << "  Frame rate:" << mFrameRateNum << "/" << mFrameRateDen << "=" << frameRate();
        qDebug() << "  Audio tracks:" << mAudioTracks.size();
    }

    return true;
}

// ----------------------------------------------------------------------------
// Parse a section
// ----------------------------------------------------------------------------
bool TTESInfo::parseSection(const QString& section, const QMap<QString, QString>& values)
{
    // Cap list/range sizes to bound memory use against malformed .info
    // files. Shared by the "audio" (per-track corrupt_ranges) and
    // "warnings" (es_doubled_pts_aus, audio_gap_frames, es_missing_ranges,
    // corrupt_frame_ranges) branches below.
    const int maxExtraFrames = 100000;

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
        int count = qMin(values.value("count", "0").toInt(), 32);
        mAudioTracks.clear();

        for (int i = 0; i < count; ++i) {
            TTAudioTrackInfo track;
            track.file      = values.value(QString("audio_%1_file").arg(i));
            track.codec     = values.value(QString("audio_%1_codec").arg(i));
            track.language  = values.value(QString("audio_%1_lang").arg(i), "und");
            track.firstPts  = values.value(QString("audio_%1_first_pts").arg(i), "0").toDouble();
            track.trimmedMs = values.value(QString("audio_%1_trimmed_ms").arg(i), "0").toInt();
            track.silenceMs = values.value(QString("audio_%1_silence_ms").arg(i), "0").toInt();
            track.removedMs = values.value(QString("audio_%1_removed_ms").arg(i), "0").toInt();

            // Parse per-track structural-damage ranges (from ttcut-demux
            // sanitizer). Format: "start-end". No duration is reported ->
            // ms is always -1. Hardened exactly like the global
            // corrupt_frame_ranges block below (item cap, toInt ok-checks,
            // inverted range rejected). audio_N_junk_bytes and
            // audio_N_dropped_frames are human diagnostics only and are
            // intentionally NOT parsed here.
            QString rangesStr = values.value(QString("audio_%1_corrupt_ranges").arg(i));
            if (!rangesStr.isEmpty()) {
                const QStringList toks = rangesStr.split(',');
                for (const QString& tok : toks) {
                    if (track.corruptRanges.size() >= maxExtraFrames) break;
                    bool okStart, okEnd;
                    int start = tok.section('-', 0, 0).toInt(&okStart);
                    int end   = tok.section('-', 1, 1).toInt(&okEnd);
                    if (!okStart || !okEnd) continue;
                    if (end < start) continue;
                    TTESRange r;
                    r.start = start;
                    r.end   = end;
                    r.ms    = -1;
                    track.corruptRanges.append(r);
                }
            }

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
            if (TTSettings::instance()->logAVStream())
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
            if (TTSettings::instance()->logAVStream())
                qDebug() << "  A/V offset:" << mAvOffsetMs << "ms";
        }
    }
    else if (section == "warnings") {
        // Parse doubled-PTS candidate AU indices (comma-separated list).
        // The legacy key es_extra_frames is intentionally NOT parsed: its
        // TS-AU numbering was consumed as merged-frame numbering, which
        // drifts on PAFF streams (see spec 2026-07-19).
        mEsTotalAus = values.value("es_total_aus", "-1").toInt();
        QString doubledStr = values.value("es_doubled_pts_aus", "");
        if (!doubledStr.isEmpty()) {
            QStringList indices = doubledStr.split(',');
            for (const QString& idx : indices) {
                if (mEsDoubledPtsAus.size() >= maxExtraFrames) break;
                bool ok;
                int frameIdx = idx.trimmed().toInt(&ok);
                if (ok) mEsDoubledPtsAus.append(frameIdx);
            }
            if (!mEsDoubledPtsAus.isEmpty())
                if (TTSettings::instance()->logAVStream())
                    qDebug() << "Loaded" << mEsDoubledPtsAus.size()
                             << "doubled-PTS candidate AUs from .info"
                             << "(total_aus" << mEsTotalAus << ")";
        }

        // Parse audio gap frame indices (analogous to es_extra_frames).
        // Generated by ttcut-demux when audio packet gaps were detected
        // in the source TS and silence was inserted at the gap position.
        QString audioGapStr = values.value("audio_gap_frames", "");
        if (!audioGapStr.isEmpty()) {
            QStringList indices = audioGapStr.split(',');
            for (const QString& idx : indices) {
                if (mAudioGapFrames.size() >= maxExtraFrames) break;
                bool ok;
                int frameIdx = idx.trimmed().toInt(&ok);
                if (ok) mAudioGapFrames.append(frameIdx);
            }
            std::sort(mAudioGapFrames.begin(), mAudioGapFrames.end());
            if (!mAudioGapFrames.isEmpty())
                if (TTSettings::instance()->logAVStream())
                    qDebug() << "Loaded" << mAudioGapFrames.size() << "audio gap frame indices from .info";
        }

        // Parse mid-stream gap-fill ranges (from ttcut-demux repair).
        // Format: "start-end:ms" (filled duration known) or "start-end" (ms
        // unknown, e.g. legacy data) -> ms = -1.
        // Note: the flat "es_missing_frames" CSV list (individual frame
        // indices) is intentionally not parsed into a member — every index
        // it lists is already covered by an es_missing_ranges range, so the
        // ranges alone are sufficient for consumers.
        QString missingRangesStr = values.value("es_missing_ranges", "");
        if (!missingRangesStr.isEmpty()) {
            QStringList ranges = missingRangesStr.split(',');
            for (const QString& tok : ranges) {
                if (mEsMissingRanges.size() >= maxExtraFrames) break;
                QString rangePart = tok.section(':', 0, 0);
                bool okStart, okEnd;
                int start = rangePart.section('-', 0, 0).toInt(&okStart);
                int end   = rangePart.section('-', 1, 1).toInt(&okEnd);
                if (!okStart || !okEnd) continue;
                // Defense in depth: a malformed/inverted range from a
                // corrupted or hand-edited .info must not reach consumers
                // that assume start<=end (ttcut-demux's own emission
                // already sanitizes this, but don't trust the file blindly).
                if (end < start) continue;
                TTESRange r;
                r.start = start;
                r.end   = end;
                r.ms    = tok.contains(':') ? tok.section(':', 1, 1).toInt() : -1;
                mEsMissingRanges.append(r);
            }
            if (!mEsMissingRanges.isEmpty())
                if (TTSettings::instance()->logAVStream())
                    qDebug() << "Loaded" << mEsMissingRanges.size() << "missing-frame ranges from .info";
        }

        // Parse corrupt-but-retained frame ranges (from ttcut-demux repair).
        // Format: "start-end". No duration is reported -> ms is always -1.
        QString corruptRangesStr = values.value("corrupt_frame_ranges", "");
        if (!corruptRangesStr.isEmpty()) {
            QStringList ranges = corruptRangesStr.split(',');
            for (const QString& tok : ranges) {
                if (mCorruptRanges.size() >= maxExtraFrames) break;
                bool okStart, okEnd;
                int start = tok.section('-', 0, 0).toInt(&okStart);
                int end   = tok.section('-', 1, 1).toInt(&okEnd);
                if (!okStart || !okEnd) continue;
                // Defense in depth: same reasoning as the es_missing_ranges
                // guard above — reject an inverted range instead of trusting
                // the .info file blindly.
                if (end < start) continue;
                TTESRange r;
                r.start = start;
                r.end   = end;
                r.ms    = -1;
                mCorruptRanges.append(r);
            }
            if (!mCorruptRanges.isEmpty())
                if (TTSettings::instance()->logAVStream())
                    qDebug() << "Loaded" << mCorruptRanges.size() << "corrupt-frame ranges from .info";
        }

        // Legacy format: decode error regions (from old ffmpeg -err_detect check).
        // Clamp region count against malformed .info files (DoS guard).
        mDecodeErrors = values.value("decode_errors", "0").toInt();
        mDecodeErrorRegionCount = qBound(0,
            values.value("decode_error_regions", "0").toInt(), 4096);
        mRecommendProjectX = (values.value("recommend_projectx", "false") == "true");
        mHasWarnings = (mDecodeErrors > 0);

        mDecodeErrorRegions.clear();
        for (int i = 0; i < mDecodeErrorRegionCount; ++i) {
            QString regionStr = values.value(QString("error_region_%1").arg(i));
            if (regionStr.isEmpty()) continue;

            // Format: frame|time|count
            QStringList parts = regionStr.split('|');
            if (parts.size() >= 3) {
                TTDecodeErrorRegion region;
                region.frame = parts[0].toInt();
                region.time = parts[1];
                region.errorCount = parts[2].toInt();
                mDecodeErrorRegions.append(region);
            }
        }

        if (mDecodeErrors > 0) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("%1 decode errors in %2 regions").arg(mDecodeErrors).arg(mDecodeErrorRegions.size()));
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
// Per-track audio repair balance (0 when the track has no repair entry)
// ----------------------------------------------------------------------------
int TTESInfo::audioSilenceMs(int track) const
{
    if (track >= 0 && track < mAudioTracks.size()) {
        return mAudioTracks[track].silenceMs;
    }
    return 0;
}

int TTESInfo::audioRemovedMs(int track) const
{
    if (track >= 0 && track < mAudioTracks.size()) {
        return mAudioTracks[track].removedMs;
    }
    return 0;
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
    int undeg = baseName.indexOf('_');
    QString commonBase = (undeg > 0) ? baseName.left(undeg) : QString();
    for (const QString& infoFile : infoFiles) {
        // Match if either side is a prefix of the other, but require a
        // non-empty common base — otherwise infoBase.startsWith("") would
        // pick the first arbitrary .info file in the directory.
        QString infoBase = QFileInfo(infoFile).completeBaseName();
        if (baseName.startsWith(infoBase) ||
            (!commonBase.isEmpty() && infoBase.startsWith(commonBase))) {
            return dir + "/" + infoFile;
        }
    }

    return QString(); // Not found
}
