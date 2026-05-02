/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttmessagelogger.cpp                                             */
/*----------------------------------------------------------------------------*/

/*!
 * The TTMessageLogger class provides runtime logging.  The first
 * getInstance() call creates the (singleton) object but does NOT open
 * the log file yet — the file is opened lazily on the first writeMsg
 * call so that QCoreApplication / main() has had a chance to set the
 * working directory and configure the path via setLogFilePath().
 */

#include "ttmessagelogger.h"

#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>

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
    if (path == mLogFilePath) return;

    // If the file is already open, close it; the next write reopens at the
    // new path. If the user passes an empty string, fall back to default.
    mLogFilePath = path.isEmpty() ? defaultLogPath() : path;
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

    if (logMode & CONSOLE || msgType == ERROR)
        qDebug("%s", logMsgStr.toUtf8().data());

    writeMsg(logMsgStr);
}

void TTMessageLogger::ensureLogFileOpen()
{
    if (mLogFileOpenAttempted) return;
    mLogFileOpenAttempted = true;

    QFile* f = new QFile(mLogFilePath);
    // Truncate any previous run's file (matches pre-refactor behaviour).
    if (!f->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qDebug("TTMessageLogger: cannot open log file %s",
               mLogFilePath.toUtf8().constData());
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
