/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2026 / TTCut-ng                                    */
/*----------------------------------------------------------------------------*/

#ifndef TTSETTINGS_H
#define TTSETTINGS_H

#include <QObject>
#include <QString>
#include <QStringList>

class TTSettings : public QObject
{
  Q_OBJECT

public:
  // Singleton access. Lazy-creates a default instance on first call.
  // setInstance() lets tests inject a fixture; pass nullptr to discard
  // and re-create on next instance() call.
  static TTSettings* instance();
  static void setInstance(TTSettings* override);

  TTSettings(QObject* parent = nullptr);
  ~TTSettings() override;

  // Persistence
  void load();              // QSettings -> fields
  void save();              // fields -> QSettings
  void resetToDefaults();   // re-initialise to compile-time defaults

  // ----- Common Options group (Task 4) -------------------------------------
  // Setters mirror to legacy TTCut::xxx during the migration window so call
  // sites that have not been migrated yet still observe consistent state.
  bool    fastSlider() const         { return mFastSlider; }
  void    setFastSlider(bool v);

  QString tempDirPath() const        { return mTempDirPath; }
  void    setTempDirPath(const QString& v);

  QString lastDirPath() const        { return mLastDirPath; }
  void    setLastDirPath(const QString& v);

  QString projectFileName() const    { return mProjectFileName; }
  void    setProjectFileName(const QString& v);

  int     cutPreviewSeconds() const  { return mCutPreviewSeconds; }
  void    setCutPreviewSeconds(int v);

  int     playSkipFrames() const     { return mPlaySkipFrames; }
  void    setPlaySkipFrames(int v);

  int     searchLength() const       { return mSearchLength; }
  void    setSearchLength(int v);

  int     searchAccuracy() const     { return mSearchAccuracy; }
  void    setSearchAccuracy(int v);

  // ----- Navigation Steps group (Task 5) ----------------------------------
  // Setters mirror to legacy TTCut::xxx during the migration window so call
  // sites that have not been migrated yet still observe consistent state.
  int     stepSliderClick() const    { return mStepSliderClick; }
  void    setStepSliderClick(int v);

  int     stepPgUpDown() const       { return mStepPgUpDown; }
  void    setStepPgUpDown(int v);

  int     stepArrowKeys() const      { return mStepArrowKeys; }
  void    setStepArrowKeys(int v);

  int     stepPlusAlt() const        { return mStepPlusAlt; }
  void    setStepPlusAlt(int v);

  int     stepPlusCtrl() const       { return mStepPlusCtrl; }
  void    setStepPlusCtrl(int v);

  int     stepPlusShift() const      { return mStepPlusShift; }
  void    setStepPlusShift(int v);

  int     stepMouseWheel() const     { return mStepMouseWheel; }
  void    setStepMouseWheel(int v);

  // ----- Index Files & Logging group (Task 6) -----------------------------
  // Setters mirror to legacy TTCut::xxx during the migration window so call
  // sites that have not been migrated yet still observe consistent state.
  bool    createVideoIDD() const     { return mCreateVideoIDD; }
  void    setCreateVideoIDD(bool v);

  bool    createAudioIDD() const     { return mCreateAudioIDD; }
  void    setCreateAudioIDD(bool v);

  bool    createPrevIDD() const      { return mCreatePrevIDD; }
  void    setCreatePrevIDD(bool v);

  bool    createD2V() const          { return mCreateD2V; }
  void    setCreateD2V(bool v);

  bool    readVideoIDD() const       { return mReadVideoIDD; }
  void    setReadVideoIDD(bool v);

  bool    readAudioIDD() const       { return mReadAudioIDD; }
  void    setReadAudioIDD(bool v);

  bool    readPrevIDD() const        { return mReadPrevIDD; }
  void    setReadPrevIDD(bool v);

  bool    createLogFile() const      { return mCreateLogFile; }
  void    setCreateLogFile(bool v);

  bool    logModeConsole() const     { return mLogModeConsole; }
  void    setLogModeConsole(bool v);

  bool    logModeExtended() const    { return mLogModeExtended; }
  void    setLogModeExtended(bool v);

  bool    logVideoIndexInfo() const  { return mLogVideoIndexInfo; }
  void    setLogVideoIndexInfo(bool v);

  bool    logAudioIndexInfo() const  { return mLogAudioIndexInfo; }
  void    setLogAudioIndexInfo(bool v);

  // ----- Recent Files group (Task 7) --------------------------------------
  // Setter mirrors to legacy TTCut::xxx during the migration window so call
  // sites that have not been migrated yet still observe consistent state,
  // and additionally emits recentFilesChanged() so subscribed UI elements
  // (recent-files menu) can refresh without ad-hoc refresh hooks.
  const QStringList& recentFileList() const { return mRecentFileList; }
  void    setRecentFileList(const QStringList& v);

signals:
  // Per-group selective change signals added in tasks 4-13.
  // Tasks 4-6 declare none (no UI dependents need change-notification).
  // Task 7 adds the first signal: the recent-files menu is the canonical
  // subscriber, but follow-up commits will wire that up.
  void recentFilesChanged(const QStringList& list);

private:
  static TTSettings* sInstance;

  // ----- Common Options group (Task 4) -------------------------------------
  // Defaults match common/ttcut.cpp lines 94-105 verbatim.
  bool    mFastSlider        = false;
  QString mTempDirPath;          // initialised to QDir::tempPath() in ctor
  QString mLastDirPath;          // initialised to QDir::homePath() in ctor
  QString mProjectFileName;      // empty by default
  int     mCutPreviewSeconds = 25;
  int     mPlaySkipFrames    = 0;
  int     mSearchLength      = 45;
  int     mSearchAccuracy    = 1;

  // ----- Navigation Steps group (Task 5) -----------------------------------
  // Defaults match common/ttcut.cpp lines 108-114 verbatim.
  int     mStepSliderClick   =  40;
  int     mStepPgUpDown      =  80;
  int     mStepArrowKeys     =   1;
  int     mStepPlusAlt       = 100;
  int     mStepPlusCtrl      = 200;
  int     mStepPlusShift     = 200;
  int     mStepMouseWheel    = 120;

  // ----- Index Files & Logging group (Task 6) ------------------------------
  // Defaults match common/ttcut.cpp lines 117-130 verbatim.
  bool    mCreateVideoIDD    = true;
  bool    mCreateAudioIDD    = true;
  bool    mCreatePrevIDD     = false;
  bool    mCreateD2V         = false;
  bool    mReadVideoIDD      = true;
  bool    mReadAudioIDD      = true;
  bool    mReadPrevIDD       = false;
  bool    mCreateLogFile     = true;
  bool    mLogModeConsole    = false;
  bool    mLogModeExtended   = true;
  bool    mLogVideoIndexInfo = false;
  bool    mLogAudioIndexInfo = false;

  // ----- Recent Files group (Task 7) ---------------------------------------
  // Default matches common/ttcut.cpp line 133 (`QStringList TTCut::recentFileList;`
  // — empty list, no initializer).
  QStringList mRecentFileList;
};

#endif
