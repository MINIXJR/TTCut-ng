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

signals:
  // Per-group selective change signals added in tasks 4-13.
  // No signals declared for the Common Options group; no UI dependents need
  // change-notification yet.

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
};

#endif
