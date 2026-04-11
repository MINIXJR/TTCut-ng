/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : ttmkvmergeprovider.h                                            */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2025  */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTMKVMERGEPROVIDER
// MKV muxer using libav matroska output format
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*                                                                            */
/* This program is distributed in the hope that it will be useful, but WITHOUT*/
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.                                          */
/* See the GNU General Public License for more details.                       */
/*                                                                            */
/* You should have received a copy of the GNU General Public License along    */
/* with this program; if not, write to the Free Software Foundation,          */
/* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.              */
/*----------------------------------------------------------------------------*/

#ifndef TTMKVMERGEPROVIDER_H
#define TTMKVMERGEPROVIDER_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QObject>
#include <QMap>

// -----------------------------------------------------------------------------
// TTMkvMergeProvider
// MKV container output using libav matroska muxer (no external binary needed)
// -----------------------------------------------------------------------------
class TTMkvMergeProvider : public QObject
{
    Q_OBJECT

public:
    TTMkvMergeProvider();
    virtual ~TTMkvMergeProvider();

    // Main muxing function
    bool mux(const QString& outputFile,
             const QString& videoFile,
             const QStringList& audioFiles,
             const QStringList& subtitleFiles = QStringList());

    // Always available (libav is linked at build time)
    bool isAvailable() const;
    QString lastError() const { return mLastError; }

    // MKV-specific options
    void setDefaultDuration(const QString& trackType, const QString& duration);
    void setTrackName(int trackId, const QString& name);
    void setLanguage(int trackId, const QString& lang);
    void setChapterFile(const QString& chapterFile);

    // Language tags for audio/subtitle tracks (ISO 639-2/B)
    void setAudioLanguages(const QStringList& languages);
    void setSubtitleLanguages(const QStringList& languages);

    // A/V sync offset in milliseconds (from .info file)
    void setAudioSyncOffset(int offsetMs);
    void setVideoSyncOffset(int offsetMs);

    // Total duration for chapter end calculation (avoids INT64_MAX overflow)
    void setTotalDurationMs(qint64 durationMs);

    // PAFF: H.264 field-coded stream — 2 field packets per frame in ES.
    // log2MaxFrameNum needed to parse field_pic_flag from slice headers.
    void setIsPAFF(bool paff, int log2MaxFrameNum = 4) {
        mIsPAFF = paff;
        mH264Log2MaxFrameNum = log2MaxFrameNum;
    }

    // Compatibility stubs (always available — libav is built-in)
    static bool isMkvMergeInstalled();
    static QString mkvMergeVersion();
    static QString mkvMergePath();

    // Chapter generation
    static QString generateChapterFile(qint64 durationMs, int intervalMinutes,
                                        const QString& outputDir);

signals:
    void progressChanged(int percent, const QString& message);

private:
    QString mLastError;
    QString mChapterFile;
    int mAudioSyncOffsetMs;
    int mVideoSyncOffsetMs;
    qint64 mTotalDurationMs;
    bool mIsPAFF;
    int mH264Log2MaxFrameNum;
    QStringList mAudioLanguages;
    QStringList mSubtitleLanguages;

    struct TrackOption {
        QString name;
        QString language;
        QString defaultDuration;
    };
    QMap<int, TrackOption> mTrackOptions;

    void setError(const QString& error);
};

#endif // TTMKVMERGEPROVIDER_H
