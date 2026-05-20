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
#include <QProcess>

#include <cstdarg>
#include <cstdio>

const int   TTMessageLogger::STD_LOG_MODE   = TTMessageLogger::SUMMARIZE;
int         TTMessageLogger::logMode        = TTMessageLogger::STD_LOG_MODE;
int         TTMessageLogger::logLevel       = TTMessageLogger::ALL;
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
    : logfile(nullptr)
    , mLogFilePath(defaultLogPath())
    , mLogFileOpenAttempted(false)
    , logEnabled(true)
    , logConsole(false)
    , logExtended(false)
{
    logMode = mode;
}

TTMessageLogger::~TTMessageLogger()
{
    if (logfile) {
        logfile->close();
        delete logfile;
        logfile = nullptr;
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
    // Empty → fall back to XDG default, THEN compare. Ohne diese Konvertierung
    // VOR dem Idempotenz-Check würde z.B. TTSettings::load() (das pro App-Start
    // dreimal mit "" aufruft, wenn der User keinen Pfad gesetzt hat) jedes Mal
    // mLogFileOpenAttempted zurücksetzen und damit eine erneute Logrotation
    // bei der nächsten writeMsg auslösen — Resultat: pro App-Start mehrere
    // .log.N-Backups einer einzigen Session.
    QString newPath = path.isEmpty() ? defaultLogPath() : path;
    if (newPath == mLogFilePath) return;

    mLogFilePath = newPath;
    if (logfile) {
        logfile->close();
        delete logfile;
        logfile = nullptr;
    }
    mLogFileOpenAttempted = false;
}

void TTMessageLogger::enableLogFile(bool enable)
{
    // File-write toggle ONLY — does not touch logLevel any more
    // (consumers configure logLevel via setLogLevel / setLogModeExtended).
    logEnabled = enable;
}

void TTMessageLogger::setLogModeConsole(bool console)
{
    if (console) {
        logMode = SUMMARIZE | CONSOLE;
    } else {
        logMode = SUMMARIZE;
    }
    logConsole = console;
}

void TTMessageLogger::setLogModeExtended(bool extended)
{
    logExtended = extended;
    logLevel = (logExtended) ? ALL : MINIMAL;
}

void TTMessageLogger::setLogMode(int mode)
{
    logMode = mode;
}

void TTMessageLogger::setLogLevel(int level)
{
    logLevel = level;
}

// -----------------------------------------------------------------------------
// Per-type message methods (QString variants)
// -----------------------------------------------------------------------------
void TTMessageLogger::infoMsg(QString caller, int line, QString msgString)
{
    logMsg(INFO, caller, line, msgString);
}

void TTMessageLogger::warningMsg(QString caller, int line, QString msgString)
{
    logMsg(WARNING, caller, line, msgString);
}

void TTMessageLogger::errorMsg(QString caller, int line, QString msgString)
{
    logMsg(ERROR, caller, line, msgString);
}

void TTMessageLogger::fatalMsg(QString caller, int line, QString msgString)
{
    logMsg(FATAL, caller, line, msgString);
}

void TTMessageLogger::debugMsg(QString caller, int line, QString msgString)
{
    logMsg(DEBUG, caller, line, msgString);
}

// -----------------------------------------------------------------------------
// printf-style overloads — dynamic via QString::vasprintf (no truncation)
// -----------------------------------------------------------------------------
void TTMessageLogger::infoMsg(QString caller, int line, const char* msg, ...)
{
    va_list ap; va_start(ap, msg);
    QString s = formatVa(msg, ap);
    va_end(ap);
    logMsg(INFO, caller, line, s);
}

void TTMessageLogger::warningMsg(QString caller, int line, const char* msg, ...)
{
    va_list ap; va_start(ap, msg);
    QString s = formatVa(msg, ap);
    va_end(ap);
    logMsg(WARNING, caller, line, s);
}

void TTMessageLogger::errorMsg(QString caller, int line, const char* msg, ...)
{
    va_list ap; va_start(ap, msg);
    QString s = formatVa(msg, ap);
    va_end(ap);
    logMsg(ERROR, caller, line, s);
}

void TTMessageLogger::fatalMsg(QString caller, int line, const char* msg, ...)
{
    va_list ap; va_start(ap, msg);
    QString s = formatVa(msg, ap);
    va_end(ap);
    logMsg(FATAL, caller, line, s);
}

void TTMessageLogger::debugMsg(QString caller, int line, const char* msg, ...)
{
    va_list ap; va_start(ap, msg);
    QString s = formatVa(msg, ap);
    va_end(ap);
    logMsg(DEBUG, caller, line, s);
}

void TTMessageLogger::showErrorMsg(QString caller, int line, const char* msg, ...)
{
    va_list ap; va_start(ap, msg);
    QString s = formatVa(msg, ap);
    va_end(ap);
    logMsg(ERROR, caller, line, s, true);
}

void TTMessageLogger::showFatalMsg(QString caller, int line, const char* msg, ...)
{
    va_list ap; va_start(ap, msg);
    QString s = formatVa(msg, ap);
    va_end(ap);
    logMsg(FATAL, caller, line, s, true);
}

// -----------------------------------------------------------------------------
// Common write path
// -----------------------------------------------------------------------------
void TTMessageLogger::logMsg(MsgType msgType, QString caller, int line,
                              QString msgString, bool show)
{
    // Serialize across threads: the new libav log callback runs on libav's
    // own decode/encode worker threads, so concurrent writes to logfile and
    // racey ensureLogFileOpen would otherwise interleave / double-init.
    std::lock_guard<std::mutex> lock(mLogMutex);

    QString msgTypeStr;
    QFileInfo fInfo(caller);
    QString msgCaller = fInfo.baseName();

    if ((logLevel == NONE) && ((msgType != ERROR) && (msgType != FATAL))) return;

    if ((logLevel == MINIMAL) && ((msgType != ERROR) && (msgType != FATAL) &&
                                  (msgType != WARNING))) return;

    if ((logLevel == EXTENDED) && ((msgType != ERROR) && (msgType != FATAL) &&
                                   (msgType != WARNING) && (msgType != INFO))) return;

    if (msgType == INFO)    msgTypeStr = "info";
    if (msgType == WARNING) msgTypeStr = "warning";
    if (msgType == ERROR)   msgTypeStr = "error";
    if (msgType == DEBUG)   msgTypeStr = "debug";

    QString logMsgStr = (line > 0)
        ? QString("[%1][%2][%3:%4] %5").arg(msgTypeStr).arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(msgCaller).arg(line).arg(msgString)
        : QString("[%1][%2][%3] %4").arg(msgTypeStr).arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(msgCaller).arg(msgString);

    // TODO: implement message window display
    (void)show;

    if (logMode & CONSOLE || msgType == ERROR) {
        // Direct stderr write (not qDebug) — with the Qt message handler
        // installed in main(), qDebug would re-enter ttQtMessageHandler →
        // debugMsg → logMsg(DEBUG, ...), duplicating every ERROR entry as
        // a [debug][ttmessagelogger:NNN] line in the file.
        fprintf(stderr, "%s\n", logMsgStr.toUtf8().constData());
        fflush(stderr);
    }

    writeMsg(logMsgStr);
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
        QProcess gz;
        gz.setStandardOutputFile(lvl2gz, QIODevice::Truncate);
        gz.start("gzip", QStringList() << "-c" << lvl1);
        gz.waitForFinished(30000);
        if (gz.exitStatus() == QProcess::NormalExit && gz.exitCode() == 0) {
            QFile::remove(lvl1);
        } else {
            // gzip schiefgegangen: rohes Move statt komprimiertes Backup,
            // damit die History nicht verloren geht.
            QFile::remove(lvl2gz);
            QFile::rename(lvl1, lvl2gz + ".uncompressed");
        }
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
    logfile = f;
}

void TTMessageLogger::writeMsg(QString msgString)
{
    if (!logEnabled) return;          // file writes suppressed (LOW-1 fix)

    ensureLogFileOpen();              // lazy open (MEDIUM-2 fix)
    if (!logfile) return;             // open failed earlier — silent skip

    QByteArray bytes = msgString.toUtf8();
    bytes.append('\n');
    logfile->write(bytes);
    logfile->flush();
}
