/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// -----------------------------------------------------------------------------
// TTMESSAGELOGGER
// -----------------------------------------------------------------------------


#ifndef TTMESSAGELOGGER_H
#define TTMESSAGELOGGER_H

#include <QString>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <mutex>
#include <vector>

class TTMessageLogger
{
  private:
    TTMessageLogger(int mode=STD_LOG_MODE);

  public:
    static TTMessageLogger* getInstance(int mode=STD_LOG_MODE);
    ~TTMessageLogger();

    // Override the default log file path (harnesses; the application sets it
    // through configure()). Pass an empty string to fall back to the default.
    void setLogFilePath(const QString& path);
    const QString& logFilePath() const { return mLogFilePath; }

    // Keep lines in memory instead of opening - and rotating - the log file
    // until configure() says whether a file is wanted at all. main() calls it
    // first thing; harnesses do not and log with the defaults.
    void holdUntilConfigured();
    // The settings' log options, applied together (TTSettings::
    // applyLogSettings). Releases held lines through the configured level,
    // console mode and file switch.
    void configure(const QString& path, bool fileEnabled, bool console, bool extended);
    void setLogModeConsole(bool console);

    void infoMsg(const QString& caller, int line, const QString& msgString);
    void warningMsg(const QString& caller, int line, const QString& msgString);
    void errorMsg(const QString& caller, int line, const QString& msgString);
    void fatalMsg(const QString& caller, int line, const QString& msgString);
    void debugMsg(const QString& caller, int line, const QString& msgString);

    // printf-style overloads (caller, line, fmt, ...)
    void infoMsg(const QString& caller, int line, const char* msg, ...);
    void warningMsg(const QString& caller, int line, const char* msg, ...);
    void errorMsg(const QString& caller, int line, const char* msg, ...);
    void debugMsg(const QString& caller, int line, const char* msg, ...);

    enum MsgType
    {
      INFO,
      WARNING,
      ERROR,
      FATAL,
      DEBUG
    };

    enum LogMode
    {
      SUMMARIZE  = 0x02,
      CONSOLE    = 0x04
    };

    enum LogLevel
    {
      ALL,         // FATAL+ERROR+WARNING+INFO+DEBUG
      EXTENDED,    // FATAL+ERROR+WARNING+INFO
      MINIMAL,     // FATAL+ERROR+WARNING
      NONE         // FATAL+ERROR
    };

    void logMsg( MsgType type, const QString& caller, int line, const QString& msgString, bool show=false);

  private:
    struct HeldLine
    {
      MsgType type     = INFO;
      QString text;
      bool    onStderr = false;   // already written to stderr when it was logged
    };

    static bool passesLevel(MsgType type);
    void   setLogFilePathLocked(const QString& path);
    void   writeMsg(const QString& msgString);
    void   ensureLogFileOpen();   // lazy open on first writeMsg call

    QFile*  mLogFile;
    QString mLogFilePath;
    bool    mLogFileOpenAttempted;
    std::mutex mLogMutex;            // serialize logMsg across threads
                                     // (libav callback runs on libav's
                                     // decode/encode worker threads)
    static TTMessageLogger* loggerInstance;
    bool   mLogEnabled;
    bool   mHold = false;
    std::vector<HeldLine> mHeld;

    static       int   sLogMode;
    static       int   sLogLevel;
    static const int   STD_LOG_MODE;
    static const char* SUM_FILE_NAME;
};
// Records an error text in a class's mLastError and logs it once under the
// caller's file/line as "<className> error: <text>" — WARNING by default,
// ERROR when asError is set. Shared by the libav-facing classes that each
// keep their own last-error string (TTFFmpegWrapper, TTESSmartCut, ...).
inline void ttSetLastError(QString& lastError, const char* file, int line,
                           const char* className, const QString& error,
                           bool asError = false)
{
  lastError = error;
  const QString msg = QString("%1 error: %2").arg(className, error);
  if (asError)
    TTMessageLogger::getInstance()->errorMsg(file, line, msg);
  else
    TTMessageLogger::getInstance()->warningMsg(file, line, msg);
}

#endif //TTMESSAGELOGGER_H
