/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : ttmkvmergeprovider.cpp                                          */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2025  */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTMKVMERGEPROVIDER
// Provider class for mkvmerge (mkvtoolnix) implementation
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

#include "ttmkvmergeprovider.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QDir>

// Decode VDR's Windows-1252 hex encoding (#XX) in filenames
static QChar win1252ToUnicode(unsigned char byte)
{
    // 0x80-0x9F: Windows-1252 has printable chars where Latin-1 has control codes
    static const ushort map[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,  // 80-87
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,  // 88-8F
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,  // 90-97
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178   // 98-9F
    };
    if (byte >= 0x80 && byte <= 0x9F)
        return QChar(map[byte - 0x80]);
    return QChar(byte);  // Latin-1 == Unicode for all other byte values
}

// Decode VDR filename: #XX → character (Windows-1252), _ → space
static QString decodeVdrName(const QString& name)
{
    QString result;
    result.reserve(name.size());

    for (int i = 0; i < name.size(); ++i) {
        if (name[i] == QChar('#') && i + 2 < name.size()) {
            bool ok;
            uint val = name.mid(i + 1, 2).toUInt(&ok, 16);
            if (ok && val >= 0x20) {
                result += win1252ToUnicode(static_cast<unsigned char>(val));
                i += 2;
                continue;
            }
        }
        result += (name[i] == QChar('_')) ? QChar(' ') : name[i];
    }

    return result;
}

// Standard mkvmerge paths to check
static const QStringList sMkvMergePaths = {
    "/usr/bin/mkvmerge",
    "/usr/local/bin/mkvmerge",
    "/opt/mkvtoolnix/mkvmerge"
};

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
TTMkvMergeProvider::TTMkvMergeProvider()
    : QObject()
    , mProcess(nullptr)
    , mAudioSyncOffsetMs(0)
{
}

// -----------------------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------------------
TTMkvMergeProvider::~TTMkvMergeProvider()
{
    if (mProcess) {
        if (mProcess->state() != QProcess::NotRunning) {
            mProcess->kill();
            mProcess->waitForFinished(3000);
        }
        delete mProcess;
    }
}

// -----------------------------------------------------------------------------
// Check if mkvmerge is available
// -----------------------------------------------------------------------------
bool TTMkvMergeProvider::isAvailable() const
{
    return isMkvMergeInstalled();
}

// -----------------------------------------------------------------------------
// Check if mkvmerge is installed
// -----------------------------------------------------------------------------
bool TTMkvMergeProvider::isMkvMergeInstalled()
{
    return QFile::exists(mkvMergePath());
}

// -----------------------------------------------------------------------------
// Get mkvmerge version
// -----------------------------------------------------------------------------
QString TTMkvMergeProvider::mkvMergeVersion()
{
    QString path = mkvMergePath();
    if (path.isEmpty()) {
        return QString();
    }

    QProcess proc;
    proc.start(path, QStringList() << "--version");

    if (!proc.waitForStarted(3000) || !proc.waitForFinished(5000)) {
        return QString();
    }

    QString output = QString::fromUtf8(proc.readAllStandardOutput());
    // Parse version from output like "mkvmerge v64.0.0 ('Willows') 64-bit"
    QRegularExpression re("mkvmerge v([\\d\\.]+)");
    QRegularExpressionMatch match = re.match(output);
    if (match.hasMatch()) {
        return match.captured(1);
    }

    return output.trimmed();
}

// -----------------------------------------------------------------------------
// Get mkvmerge path
// -----------------------------------------------------------------------------
QString TTMkvMergeProvider::mkvMergePath()
{
    for (const QString& path : sMkvMergePaths) {
        if (QFile::exists(path)) {
            return path;
        }
    }
    return QString();
}

// -----------------------------------------------------------------------------
// Set default duration for a track type
// -----------------------------------------------------------------------------
void TTMkvMergeProvider::setDefaultDuration(const QString& trackType, const QString& duration)
{
    Q_UNUSED(trackType);
    Q_UNUSED(duration);
    // This would set --default-duration for specific track types
    // e.g., "video" -> "--default-duration 0:25fps"
}

// -----------------------------------------------------------------------------
// Set track name
// -----------------------------------------------------------------------------
void TTMkvMergeProvider::setTrackName(int trackId, const QString& name)
{
    mTrackOptions[trackId].name = name;
}

// -----------------------------------------------------------------------------
// Set track language
// -----------------------------------------------------------------------------
void TTMkvMergeProvider::setLanguage(int trackId, const QString& lang)
{
    mTrackOptions[trackId].language = lang;
}

// -----------------------------------------------------------------------------
// Set chapter file
// -----------------------------------------------------------------------------
void TTMkvMergeProvider::setChapterFile(const QString& chapterFile)
{
    mChapterFile = chapterFile;
}

// -----------------------------------------------------------------------------
// Set audio language tags (ISO 639-2/B)
// -----------------------------------------------------------------------------
void TTMkvMergeProvider::setAudioLanguages(const QStringList& languages)
{
    mAudioLanguages = languages;
}

// -----------------------------------------------------------------------------
// Set subtitle language tags (ISO 639-2/B)
// -----------------------------------------------------------------------------
void TTMkvMergeProvider::setSubtitleLanguages(const QStringList& languages)
{
    mSubtitleLanguages = languages;
}

// -----------------------------------------------------------------------------
// Set A/V sync offset for audio tracks (in milliseconds)
// Positive offset = audio delayed, negative = audio earlier
// -----------------------------------------------------------------------------
void TTMkvMergeProvider::setAudioSyncOffset(int offsetMs)
{
    mAudioSyncOffsetMs = offsetMs;
    if (offsetMs != 0) {
        qDebug() << "TTMkvMergeProvider: A/V sync offset set to" << offsetMs << "ms";
    }
}

// -----------------------------------------------------------------------------
// Main muxing function
// -----------------------------------------------------------------------------
bool TTMkvMergeProvider::mux(const QString& outputFile,
                              const QString& videoFile,
                              const QStringList& audioFiles,
                              const QStringList& subtitleFiles)
{
    if (!isAvailable()) {
        setError("mkvmerge is not installed");
        return false;
    }

    if (videoFile.isEmpty() || !QFile::exists(videoFile)) {
        setError(QString("Video file not found: %1").arg(videoFile));
        return false;
    }

    QStringList args = buildCommandLine(outputFile, videoFile, audioFiles, subtitleFiles);

    qDebug() << "mkvmerge command:" << args.join(" ");

    mProcess = new QProcess(this);
    connect(mProcess, &QProcess::readyReadStandardOutput, this, &TTMkvMergeProvider::onReadyReadStandardOutput);
    connect(mProcess, &QProcess::readyReadStandardError, this, &TTMkvMergeProvider::onReadyReadStandardError);

    mProcess->start(mkvMergePath(), args);

    if (!mProcess->waitForStarted(5000)) {
        setError("mkvmerge failed to start");
        delete mProcess;
        mProcess = nullptr;
        return false;
    }

    // Wait for completion (with generous timeout for large files)
    if (!mProcess->waitForFinished(600000)) {  // 10 minutes
        setError("mkvmerge timed out");
        mProcess->kill();
        delete mProcess;
        mProcess = nullptr;
        return false;
    }

    int exitCode = mProcess->exitCode();
    delete mProcess;
    mProcess = nullptr;

    if (exitCode != 0 && exitCode != 1) {  // mkvmerge returns 1 for warnings
        // Error was already set in stderr handler
        if (mLastError.isEmpty()) {
            setError(QString("mkvmerge failed with exit code %1").arg(exitCode));
        }
        return false;
    }

    qDebug() << "mkvmerge completed successfully:" << outputFile;
    return true;
}

// -----------------------------------------------------------------------------
// Build command line arguments
// -----------------------------------------------------------------------------
QStringList TTMkvMergeProvider::buildCommandLine(const QString& outputFile,
                                                   const QString& videoFile,
                                                   const QStringList& audioFiles,
                                                   const QStringList& subtitleFiles)
{
    QStringList args;

    // Output file
    args << "-o" << outputFile;

    // Title from video filename (decode VDR #XX encoding, _ → space)
    QString title = decodeVdrName(QFileInfo(videoFile).completeBaseName());
    if (!title.isEmpty()) {
        args << "--title" << title;
    }

    // Video file
    args << videoFile;

    // Audio files with optional sync offset and language
    // Language from explicit list takes priority, filename regex as fallback
    QRegularExpression langRe("_([a-z]{3})(?:_\\d+)?$");
    int audioTrackId = 1;
    for (int i = 0; i < audioFiles.size(); i++) {
        const QString& audio = audioFiles[i];
        if (QFile::exists(audio)) {
            // Prefer explicit language from data model
            QString lang;
            if (i < mAudioLanguages.size() && !mAudioLanguages[i].isEmpty()) {
                lang = mAudioLanguages[i];
            } else {
                // Fallback: extract from filename (e.g., "Show_deu.ac3" → "deu")
                QRegularExpressionMatch langMatch = langRe.match(QFileInfo(audio).completeBaseName());
                if (langMatch.hasMatch()) {
                    lang = langMatch.captured(1);
                }
            }
            if (!lang.isEmpty()) {
                args << "--language" << QString("0:%1").arg(lang);
            }
            // Apply A/V sync offset if set
            if (mAudioSyncOffsetMs != 0) {
                args << "--sync" << QString("%1:%2").arg(audioTrackId).arg(-mAudioSyncOffsetMs);
            }
            args << audio;
            audioTrackId++;
        }
    }

    // Subtitle files with language
    for (int i = 0; i < subtitleFiles.size(); i++) {
        const QString& sub = subtitleFiles[i];
        if (QFile::exists(sub)) {
            QString lang;
            if (i < mSubtitleLanguages.size() && !mSubtitleLanguages[i].isEmpty()) {
                lang = mSubtitleLanguages[i];
            } else {
                QRegularExpressionMatch langMatch = langRe.match(QFileInfo(sub).completeBaseName());
                if (langMatch.hasMatch()) {
                    lang = langMatch.captured(1);
                }
            }
            if (!lang.isEmpty()) {
                args << "--language" << QString("0:%1").arg(lang);
            }
            args << sub;
        }
    }

    // Chapter file if set
    if (!mChapterFile.isEmpty() && QFile::exists(mChapterFile)) {
        args << "--chapters" << mChapterFile;
    }

    return args;
}

// -----------------------------------------------------------------------------
// Handle stdout from mkvmerge
// -----------------------------------------------------------------------------
void TTMkvMergeProvider::onReadyReadStandardOutput()
{
    if (!mProcess) return;

    QString output = QString::fromUtf8(mProcess->readAllStandardOutput());
    emit processOutput(output);

    // Parse progress from output (mkvmerge outputs "Progress: XX%")
    QRegularExpression re("Progress:\\s*(\\d+)%");
    QRegularExpressionMatch match = re.match(output);
    if (match.hasMatch()) {
        int percent = match.captured(1).toInt();
        emit progressChanged(percent, QString("Muxing: %1%").arg(percent));
    }
}

// -----------------------------------------------------------------------------
// Handle stderr from mkvmerge
// -----------------------------------------------------------------------------
void TTMkvMergeProvider::onReadyReadStandardError()
{
    if (!mProcess) return;

    QString error = QString::fromUtf8(mProcess->readAllStandardError());
    if (!error.trimmed().isEmpty()) {
        qDebug() << "mkvmerge stderr:" << error;
        // Only set as error if it's actually an error message
        if (error.contains("Error:", Qt::CaseInsensitive)) {
            setError(error.trimmed());
        }
    }
}

// -----------------------------------------------------------------------------
// Set error message
// -----------------------------------------------------------------------------
void TTMkvMergeProvider::setError(const QString& error)
{
    mLastError = error;
    qDebug() << "TTMkvMergeProvider error:" << error;
}

// -----------------------------------------------------------------------------
// Generate chapter file for MKV
// Creates a simple chapter file in OGM/Matroska format
// Returns the path to the generated chapter file, or empty string on failure
// -----------------------------------------------------------------------------
QString TTMkvMergeProvider::generateChapterFile(qint64 durationMs, int intervalMinutes,
                                                  const QString& outputDir)
{
    if (durationMs <= 0 || intervalMinutes <= 0) {
        qDebug() << "Invalid parameters for chapter generation";
        return QString();
    }

    qint64 intervalMs = static_cast<qint64>(intervalMinutes) * 60 * 1000;

    // Generate chapter file path
    QString chapterFilePath = QDir(outputDir).filePath("chapters.txt");

    QFile chapterFile(chapterFilePath);
    if (!chapterFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "Failed to create chapter file:" << chapterFilePath;
        return QString();
    }

    QTextStream out(&chapterFile);

    int chapterNum = 1;
    qint64 currentTime = 0;

    while (currentTime < durationMs) {
        // Convert milliseconds to HH:MM:SS.mmm format
        int hours = currentTime / (1000 * 60 * 60);
        int minutes = (currentTime / (1000 * 60)) % 60;
        int seconds = (currentTime / 1000) % 60;
        int millis = currentTime % 1000;

        // Write chapter entry in simple chapter format
        // CHAPTER01=00:00:00.000
        // CHAPTER01NAME=Chapter 1
        out << QString("CHAPTER%1=%2:%3:%4.%5\n")
               .arg(chapterNum, 2, 10, QChar('0'))
               .arg(hours, 2, 10, QChar('0'))
               .arg(minutes, 2, 10, QChar('0'))
               .arg(seconds, 2, 10, QChar('0'))
               .arg(millis, 3, 10, QChar('0'));
        out << QString("CHAPTER%1NAME=Chapter %1\n")
               .arg(chapterNum, 2, 10, QChar('0'));

        chapterNum++;
        currentTime += intervalMs;
    }

    chapterFile.close();

    qDebug() << "Generated chapter file with" << (chapterNum - 1) << "chapters:" << chapterFilePath;
    return chapterFilePath;
}
