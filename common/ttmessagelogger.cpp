/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttmessagelogger.h"

#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>

#include <cstdarg>
#include <cstdio>

#include <zlib.h>

const int   TTMessageLogger::STD_LOG_MODE   = TTMessageLogger::SUMMARIZE;
int         TTMessageLogger::sLogMode        = TTMessageLogger::STD_LOG_MODE;
int         TTMessageLogger::sLogLevel       = TTMessageLogger::ALL;
const char* TTMessageLogger::SUM_FILE_NAME  = "logfile.log";

TTMessageLogger* TTMessageLogger::loggerInstance = nullptr;

namespace {

// Default log path lives under XDG cache. Falls back to QDir::tempPath()
// only if QStandardPaths returns empty (very early init / minimal env).
QString defaultLogPath()
{
    QString cacheDir = QStandardPaths::writableLocation(
        QStandardPaths::GenericCacheLocation);
    if (cacheDir.isEmpty()) {
        cacheDir = QDir::tempPath();
    }
    QDir().mkpath(cacheDir + "/ttcut-ng");
    return cacheDir + "/ttcut-ng/logfile.log";
}

QString formatVa(const char* fmt, va_list ap)
{
    return QString::vasprintf(fmt, ap);
}

}  // namespace

// -----------------------------------------------------------------------------
// Construction / singleton access
// -----------------------------------------------------------------------------
TTMessageLogger::TTMessageLogger(int mode)
    : mLogFile(nullptr)
    , mLogFilePath(defaultLogPath())
    , mLogFileOpenAttempted(false)
    , mLogEnabled(true)
{
    sLogMode = mode;
}

TTMessageLogger::~TTMessageLogger()
{
    if (mLogFile) {
        mLogFile->close();
        delete mLogFile;
        mLogFile = nullptr;
    }
}

TTMessageLogger* TTMessageLogger::getInstance(int mode)
{
    static std::once_flag onceFlag;
    std::call_once(onceFlag, [mode]() {
        loggerInstance = new TTMessageLogger(mode);
    });
    return loggerInstance;
}

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------
void TTMessageLogger::setLogFilePath(const QString& path)
{
    std::lock_guard<std::mutex> lock(mLogMutex);
    setLogFilePathLocked(path);
}

void TTMessageLogger::setLogFilePathLocked(const QString& path)
{
    // Empty → fall back to XDG default, THEN compare. Ohne diese Konvertierung
    // VOR dem Idempotenz-Check würde z.B. TTSettings::load() (das pro App-Start
    // dreimal mit "" aufruft, wenn der User keinen Pfad gesetzt hat) jedes Mal
    // mLogFileOpenAttempted zurücksetzen und damit eine erneute Logrotation
    // bei der nächsten writeMsg auslösen — Resultat: pro App-Start mehrere
    // .log.N-Backups einer einzigen Session.
    QString newPath = path.isEmpty() ? defaultLogPath() : path;
    if (newPath == mLogFilePath) return;

    mLogFilePath = newPath;
    if (mLogFile) {
        mLogFile->close();
        delete mLogFile;
        mLogFile = nullptr;
    }
    mLogFileOpenAttempted = false;
}

void TTMessageLogger::holdUntilConfigured()
{
    std::lock_guard<std::mutex> lock(mLogMutex);
    mHold = true;
}

void TTMessageLogger::configure(const QString& path, bool fileEnabled,
                                bool console, bool extended)
{
    std::lock_guard<std::mutex> lock(mLogMutex);
    setLogFilePathLocked(path);
    mLogEnabled = fileEnabled;
    sLogMode    = console ? (SUMMARIZE | CONSOLE) : SUMMARIZE;
    sLogLevel   = extended ? ALL : MINIMAL;

    if (!mHold) return;
    mHold = false;
    // The held lines passed the default level (ALL); filter them again with
    // the configured one before they reach the console or the file.
    for (const HeldLine& held : mHeld) {
        if (!passesLevel(held.type)) continue;
        if ((sLogMode & CONSOLE) && !held.onStderr) {
            fprintf(stderr, "%s\n", held.text.toUtf8().constData());
            fflush(stderr);
        }
        writeMsg(held.text);
    }
    mHeld.clear();
}

void TTMessageLogger::setLogModeConsole(bool console)
{
    std::lock_guard<std::mutex> lock(mLogMutex);
    sLogMode = console ? (SUMMARIZE | CONSOLE) : SUMMARIZE;
}

// -----------------------------------------------------------------------------
// Per-type message methods (QString variants)
// -----------------------------------------------------------------------------
void TTMessageLogger::infoMsg(const QString& caller, int line, const QString& msgString)
{
    logMsg(INFO, caller, line, msgString);
}

void TTMessageLogger::warningMsg(const QString& caller, int line, const QString& msgString)
{
    logMsg(WARNING, caller, line, msgString);
}

void TTMessageLogger::errorMsg(const QString& caller, int line, const QString& msgString)
{
    logMsg(ERROR, caller, line, msgString);
}

void TTMessageLogger::fatalMsg(const QString& caller, int line, const QString& msgString)
{
    logMsg(FATAL, caller, line, msgString);
}

void TTMessageLogger::debugMsg(const QString& caller, int line, const QString& msgString)
{
    logMsg(DEBUG, caller, line, msgString);
}

// -----------------------------------------------------------------------------
// printf-style overloads — dynamic via QString::vasprintf (no truncation)
// -----------------------------------------------------------------------------
void TTMessageLogger::infoMsg(const QString& caller, int line, const char* msg, ...)
{
    va_list ap; va_start(ap, msg);
    QString s = formatVa(msg, ap);
    va_end(ap);
    logMsg(INFO, caller, line, s);
}

void TTMessageLogger::warningMsg(const QString& caller, int line, const char* msg, ...)
{
    va_list ap; va_start(ap, msg);
    QString s = formatVa(msg, ap);
    va_end(ap);
    logMsg(WARNING, caller, line, s);
}

void TTMessageLogger::errorMsg(const QString& caller, int line, const char* msg, ...)
{
    va_list ap; va_start(ap, msg);
    QString s = formatVa(msg, ap);
    va_end(ap);
    logMsg(ERROR, caller, line, s);
}

void TTMessageLogger::debugMsg(const QString& caller, int line, const char* msg, ...)
{
    va_list ap; va_start(ap, msg);
    QString s = formatVa(msg, ap);
    va_end(ap);
    logMsg(DEBUG, caller, line, s);
}

// -----------------------------------------------------------------------------
// Common write path
// -----------------------------------------------------------------------------
bool TTMessageLogger::passesLevel(MsgType type)
{
    if (type == ERROR || type == FATAL) return true;
    switch (sLogLevel) {
        case NONE:     return false;
        case MINIMAL:  return type == WARNING;
        case EXTENDED: return type == WARNING || type == INFO;
        default:       return true;
    }
}

void TTMessageLogger::logMsg(MsgType msgType, const QString& caller, int line,
                              const QString& msgString, bool show)
{
    // Serialize across threads: the new libav log callback runs on libav's
    // own decode/encode worker threads, so concurrent writes to mLogFile and
    // racey ensureLogFileOpen would otherwise interleave / double-init.
    std::lock_guard<std::mutex> lock(mLogMutex);

    QString msgTypeStr;
    QFileInfo fInfo(caller);
    QString msgCaller = fInfo.baseName();

    if (!passesLevel(msgType)) return;

    if (msgType == INFO)    msgTypeStr = "info";
    if (msgType == WARNING) msgTypeStr = "warning";
    if (msgType == ERROR)   msgTypeStr = "error";
    if (msgType == FATAL)   msgTypeStr = "fatal";
    if (msgType == DEBUG)   msgTypeStr = "debug";

    QString logMsgStr = (line > 0)
        ? QString("[%1][%2][%3:%4] %5").arg(msgTypeStr).arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(msgCaller).arg(line).arg(msgString)
        : QString("[%1][%2][%3] %4").arg(msgTypeStr).arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(msgCaller).arg(msgString);

    // TODO: implement message window display
    (void)show;

    const bool onStderr = (sLogMode & CONSOLE) || msgType == ERROR || msgType == FATAL;
    if (onStderr) {
        // Direct stderr write (not qDebug) — with the Qt message handler
        // installed in main(), qDebug would re-enter ttQtMessageHandler →
        // debugMsg → logMsg(DEBUG, ...), duplicating every ERROR entry as
        // a [debug][ttmessagelogger:NNN] line in the file.
        fprintf(stderr, "%s\n", logMsgStr.toUtf8().constData());
        fflush(stderr);
    }

    if (mHold) {
        mHeld.push_back({msgType, logMsgStr, onStderr});
        return;
    }
    writeMsg(logMsgStr);
}

// Compresses src into dst (gzip format) in-process; false on any error, with
// dst removed. Runs under the logger's mutex, so no child process and no
// timeout (the former `gzip` process could block every logging thread for
// up to 30 s, and without `gzip` on the PATH sessions were lost).
static bool gzipFile(const QString& src, const QString& dst)
{
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) return false;
    const QByteArray data = in.readAll();
    if (in.error() != QFileDevice::NoError) return false;

    gzFile out = gzopen(QFile::encodeName(dst).constData(), "wb");
    if (!out) return false;
    bool ok = data.isEmpty()
           || gzwrite(out, data.constData(), static_cast<unsigned>(data.size())) == data.size();
    if (gzclose(out) != Z_OK) ok = false;
    if (!ok) QFile::remove(dst);
    return ok;
}

static void rotateLogFile(const QString& path)
{
    // Logrotate-Style: behält die letzten Sessions in derselben Verzeichnis.
    //   <path>          (neu, Text)            ← current run
    //   <path>.1        (vorherige Session, Text)
    //   <path>.2.gz     (älter, gzip-komprimiert)
    //   …
    //   <path>.kMaxBackups.gz (ältester)
    // Älter als kMaxBackups wird verworfen.
    if (path.isEmpty()) return;
    constexpr int kMaxBackups = 10;

    // 1) Ältesten Backup wegwerfen wenn voll
    QFile::remove(QString("%1.%2.gz").arg(path).arg(kMaxBackups));

    // 2) Komprimierte Backups durchschieben: .N.gz → .(N+1).gz, von oben nach unten
    for (int n = kMaxBackups - 1; n >= 2; --n) {
        QString from = QString("%1.%2.gz").arg(path).arg(n);
        QString to   = QString("%1.%2.gz").arg(path).arg(n + 1);
        if (QFile::exists(from)) {
            QFile::remove(to);
            QFile::rename(from, to);
        }
    }

    // 3) <path>.1 (Text der vorletzten Session) → <path>.2.gz (komprimieren)
    const QString lvl1   = path + ".1";
    const QString lvl2gz = path + ".2.gz";
    if (QFile::exists(lvl1)) {
        QFile::remove(lvl2gz);
        // A failed compression (disk full, no write permission) drops this
        // older session: keeping it under another name would leave a file no
        // later rotation moves, and step 4 needs the .1 slot.
        if (!gzipFile(lvl1, lvl2gz)) {
            fprintf(stderr, "TTMessageLogger: cannot compress %s, previous session dropped\n",
                    lvl1.toUtf8().constData());
            fflush(stderr);
        }
        QFile::remove(lvl1);
    }

    // 4) <path> (Text der letzten Session) → <path>.1
    if (QFile::exists(path)) {
        QFile::remove(lvl1);
        QFile::rename(path, lvl1);
    }
}

void TTMessageLogger::ensureLogFileOpen()
{
    if (mLogFileOpenAttempted) return;
    mLogFileOpenAttempted = true;

    // Logrotate vor jedem App-Start: aktuell + .1 als Text,
    // ältere komprimiert als .2.gz … .10.gz, danach verworfen.
    rotateLogFile(mLogFilePath);

    QFile* f = new QFile(mLogFilePath);
    // Truncate any previous run's file (matches pre-refactor behaviour).
    if (!f->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        // Direct stderr (not qDebug) — we run under mLogMutex via logMsg →
        // writeMsg → ensureLogFileOpen, and qDebug would re-enter
        // ttQtMessageHandler → debugMsg → logMsg, deadlocking on the
        // non-recursive mutex.
        fprintf(stderr, "TTMessageLogger: cannot open log file %s\n",
                mLogFilePath.toUtf8().constData());
        fflush(stderr);
        delete f;
        return;
    }
    mLogFile = f;
}

void TTMessageLogger::writeMsg(const QString& msgString)
{
    if (!mLogEnabled) return;          // file writes suppressed (LOW-1 fix)

    ensureLogFileOpen();              // lazy open (MEDIUM-2 fix)
    if (!mLogFile) return;             // open failed earlier — silent skip

    QByteArray bytes = msgString.toUtf8();
    bytes.append('\n');
    mLogFile->write(bytes);
    mLogFile->flush();
}
