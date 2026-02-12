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
// Provider class for mkvmerge (mkvtoolnix) integration
// Used for MKV container output
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
#include <QProcess>
#include <QObject>
#include <QMap>

// -----------------------------------------------------------------------------
// TTMkvMergeProvider
// Wraps mkvmerge for MKV container creation
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

    // Availability check
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
    // Positive = audio starts later, negative = audio starts earlier
    void setAudioSyncOffset(int offsetMs);

    // Video sync offset in milliseconds (for B-frame reordering compensation)
    // Shifts video track timestamps to start at 0
    void setVideoSyncOffset(int offsetMs);

    // Check mkvmerge installation
    static bool isMkvMergeInstalled();
    static QString mkvMergeVersion();
    static QString mkvMergePath();

    // Chapter generation
    static QString generateChapterFile(qint64 durationMs, int intervalMinutes,
                                        const QString& outputDir);

signals:
    void progressChanged(int percent, const QString& message);
    void processOutput(const QString& output);

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();

private:
    QProcess* mProcess;
    QString mLastError;
    QString mChapterFile;
    int mAudioSyncOffsetMs;
    int mVideoSyncOffsetMs;
    QStringList mAudioLanguages;
    QStringList mSubtitleLanguages;

    struct TrackOption {
        QString name;
        QString language;
        QString defaultDuration;
    };
    QMap<int, TrackOption> mTrackOptions;

    void setError(const QString& error);
    QStringList buildCommandLine(const QString& outputFile,
                                  const QString& videoFile,
                                  const QStringList& audioFiles,
                                  const QStringList& subtitleFiles);
};

#endif // TTMKVMERGEPROVIDER_H
