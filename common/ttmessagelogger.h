/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2006                                                      */
/* FILE     : ttmessagelogger.h                                               */
/*----------------------------------------------------------------------------*/

// -----------------------------------------------------------------------------
// TTMESSAGELOGGER
// -----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*----------------------------------------------------------------------------*/


#ifndef TTMESSAGELOGGER_H
#define TTMESSAGELOGGER_H

#include <QString>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <mutex>

class TTMessageLogger
{
  private:
    TTMessageLogger(int mode=STD_LOG_MODE);

  public:
    static TTMessageLogger* getInstance(int mode=STD_LOG_MODE);
    ~TTMessageLogger();

    // Override the default log file path (e.g. from main() before any
    // logging happens). Pass an empty string to fall back to the default.
    void setLogFilePath(const QString& path);

    void enableLogFile(bool enable);
    void setLogModeConsole(bool console);
    void setLogModeExtended(bool extended);

    void setLogMode(int mode);
    void setLogLevel(int level);

    void infoMsg(QString caller, int line, QString msgString);
    void warningMsg(QString caller, int line, QString msgString);
    void errorMsg(QString caller, int line, QString msgString);
    void fatalMsg(QString caller, int line, QString msgString);
    void debugMsg(QString caller, int line, QString msgString);

    // printf-style overloads (caller, line, fmt, ...)
    void infoMsg(QString caller, int line, const char* msg, ...);
    void warningMsg(QString caller, int line, const char* msg, ...);
    void errorMsg(QString caller, int line, const char* msg, ...);
    void fatalMsg(QString caller, int line, const char* msg, ...);
    void debugMsg(QString caller, int line, const char* msg, ...);
    void showErrorMsg(QString caller, int line, const char* msg, ...);
    void showFatalMsg(QString caller, int line, const char* msg, ...);

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

    void logMsg( MsgType type, QString caller, int line, QString msgString, bool show=false);
    void writeMsg(QString msgString);

  private:
    void   ensureLogFileOpen();   // lazy open on first writeMsg call

    QFile*  logfile;
    QString mLogFilePath;
    bool    mLogFileOpenAttempted;
    static TTMessageLogger* loggerInstance;
    bool   logEnabled;
    bool   logConsole;
    bool   logExtended;

    static       int   logMode;
    static       int   logLevel;
    static const int   STD_LOG_MODE;
    static const char* SUM_FILE_NAME;
};
#endif //TTMESSAGELOGGER_H
