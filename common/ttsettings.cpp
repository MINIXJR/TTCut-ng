/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2026 / TTCut-ng                                    */
/*----------------------------------------------------------------------------*/

#include "ttsettings.h"
#include "ttcut.h"

#include <QDir>
#include <QSettings>

TTSettings* TTSettings::sInstance = nullptr;

TTSettings* TTSettings::instance()
{
  if (sInstance == nullptr) {
    sInstance = new TTSettings();
    sInstance->load();
  }
  return sInstance;
}

void TTSettings::setInstance(TTSettings* override)
{
  // Caller owns the override pointer's lifetime. We do NOT delete sInstance
  // when overriding because the test fixture pattern frequently passes a
  // stack/RAII-owned pointer.
  sInstance = override;
}

TTSettings::TTSettings(QObject* parent)
  : QObject(parent)
{
  // Runtime defaults that depend on environment (matches common/ttcut.cpp).
  mTempDirPath = QDir::tempPath();
  mLastDirPath = QDir::homePath();
  mProjectFileName = QString();
}

TTSettings::~TTSettings()
{
}

// ---- Common Options group setters (Task 4) ---------------------------------
// Each setter early-outs on no-op assignment and mirrors the new value to the
// legacy TTCut::xxx static so unmigrated call sites observe consistent state.

void TTSettings::setFastSlider(bool v)
{
  if (mFastSlider == v) return;
  mFastSlider = v;
  TTCut::fastSlider = v;
}

void TTSettings::setTempDirPath(const QString& v)
{
  if (mTempDirPath == v) return;
  mTempDirPath = v;
  TTCut::tempDirPath = v;
}

void TTSettings::setLastDirPath(const QString& v)
{
  if (mLastDirPath == v) return;
  mLastDirPath = v;
  TTCut::lastDirPath = v;
}

void TTSettings::setProjectFileName(const QString& v)
{
  if (mProjectFileName == v) return;
  mProjectFileName = v;
  TTCut::projectFileName = v;
}

void TTSettings::setCutPreviewSeconds(int v)
{
  if (mCutPreviewSeconds == v) return;
  mCutPreviewSeconds = v;
  TTCut::cutPreviewSeconds = v;
}

void TTSettings::setPlaySkipFrames(int v)
{
  if (mPlaySkipFrames == v) return;
  mPlaySkipFrames = v;
  TTCut::playSkipFrames = v;
}

void TTSettings::setSearchLength(int v)
{
  if (mSearchLength == v) return;
  mSearchLength = v;
  TTCut::searchLength = v;
}

void TTSettings::setSearchAccuracy(int v)
{
  if (mSearchAccuracy == v) return;
  mSearchAccuracy = v;
  TTCut::searchAccuracy = v;
}

// ---- Navigation Steps group setters (Task 5) -------------------------------
// Each setter early-outs on no-op assignment and mirrors the new value to the
// legacy TTCut::xxx static so unmigrated call sites observe consistent state.

void TTSettings::setStepSliderClick(int v)
{
  if (mStepSliderClick == v) return;
  mStepSliderClick = v;
  TTCut::stepSliderClick = v;
}

void TTSettings::setStepPgUpDown(int v)
{
  if (mStepPgUpDown == v) return;
  mStepPgUpDown = v;
  TTCut::stepPgUpDown = v;
}

void TTSettings::setStepArrowKeys(int v)
{
  if (mStepArrowKeys == v) return;
  mStepArrowKeys = v;
  TTCut::stepArrowKeys = v;
}

void TTSettings::setStepPlusAlt(int v)
{
  if (mStepPlusAlt == v) return;
  mStepPlusAlt = v;
  TTCut::stepPlusAlt = v;
}

void TTSettings::setStepPlusCtrl(int v)
{
  if (mStepPlusCtrl == v) return;
  mStepPlusCtrl = v;
  TTCut::stepPlusCtrl = v;
}

void TTSettings::setStepPlusShift(int v)
{
  if (mStepPlusShift == v) return;
  mStepPlusShift = v;
  TTCut::stepPlusShift = v;
}

void TTSettings::setStepMouseWheel(int v)
{
  if (mStepMouseWheel == v) return;
  mStepMouseWheel = v;
  TTCut::stepMouseWheel = v;
}

// ---- Index Files & Logging group setters (Task 6) --------------------------
// Each setter early-outs on no-op assignment and mirrors the new value to the
// legacy TTCut::xxx static so unmigrated call sites observe consistent state.

void TTSettings::setCreateVideoIDD(bool v)
{
  if (mCreateVideoIDD == v) return;
  mCreateVideoIDD = v;
  TTCut::createVideoIDD = v;
}

void TTSettings::setCreateAudioIDD(bool v)
{
  if (mCreateAudioIDD == v) return;
  mCreateAudioIDD = v;
  TTCut::createAudioIDD = v;
}

void TTSettings::setCreatePrevIDD(bool v)
{
  if (mCreatePrevIDD == v) return;
  mCreatePrevIDD = v;
  TTCut::createPrevIDD = v;
}

void TTSettings::setCreateD2V(bool v)
{
  if (mCreateD2V == v) return;
  mCreateD2V = v;
  TTCut::createD2V = v;
}

void TTSettings::setReadVideoIDD(bool v)
{
  if (mReadVideoIDD == v) return;
  mReadVideoIDD = v;
  TTCut::readVideoIDD = v;
}

void TTSettings::setReadAudioIDD(bool v)
{
  if (mReadAudioIDD == v) return;
  mReadAudioIDD = v;
  TTCut::readAudioIDD = v;
}

void TTSettings::setReadPrevIDD(bool v)
{
  if (mReadPrevIDD == v) return;
  mReadPrevIDD = v;
  TTCut::readPrevIDD = v;
}

void TTSettings::setCreateLogFile(bool v)
{
  if (mCreateLogFile == v) return;
  mCreateLogFile = v;
  TTCut::createLogFile = v;
}

void TTSettings::setLogModeConsole(bool v)
{
  if (mLogModeConsole == v) return;
  mLogModeConsole = v;
  TTCut::logModeConsole = v;
}

void TTSettings::setLogModeExtended(bool v)
{
  if (mLogModeExtended == v) return;
  mLogModeExtended = v;
  TTCut::logModeExtended = v;
}

void TTSettings::setLogVideoIndexInfo(bool v)
{
  if (mLogVideoIndexInfo == v) return;
  mLogVideoIndexInfo = v;
  TTCut::logVideoIndexInfo = v;
}

void TTSettings::setLogAudioIndexInfo(bool v)
{
  if (mLogAudioIndexInfo == v) return;
  mLogAudioIndexInfo = v;
  TTCut::logAudioIndexInfo = v;
}

// ---- Recent Files group setter (Task 7) ------------------------------------
// First setter in Phase B that emits a change-notification signal. Mutating
// call sites (append/prepend/removeAll) must read-modify-write through this
// setter so the signal fires and the legacy mirror stays consistent.

void TTSettings::setRecentFileList(const QStringList& v)
{
  if (mRecentFileList == v) return;
  mRecentFileList = v;
  TTCut::recentFileList = v;
  emit recentFilesChanged(v);
}

// ---- Encoder Generic group setters (Task 8) --------------------------------
// Each setter early-outs on no-op assignment and mirrors the new value to the
// legacy TTCut::xxx static so unmigrated call sites observe consistent state.
// setEncoderCodec also emits encoderCodecChanged(int) for non-dialog
// subscribers. encoderPreset/Crf/Profile have NO QSettings load/save
// round-trip — they are transient working values persisted in the .ttcut
// project file (data/ttcutprojectdata.cpp). The setters still mirror to the
// legacy statics so the project-file persistence path works during the
// Strangler migration window.

void TTSettings::setEncoderMode(bool v)
{
  if (mEncoderMode == v) return;
  mEncoderMode = v;
  TTCut::encoderMode = v;
}

void TTSettings::setEncoderCodec(int v)
{
  if (mEncoderCodec == v) return;
  mEncoderCodec = v;
  TTCut::encoderCodec = v;
  emit encoderCodecChanged(v);
}

void TTSettings::setEncoderPreset(int v)
{
  if (mEncoderPreset == v) return;
  mEncoderPreset = v;
  TTCut::encoderPreset = v;
}

void TTSettings::setEncoderCrf(int v)
{
  if (mEncoderCrf == v) return;
  mEncoderCrf = v;
  TTCut::encoderCrf = v;
}

void TTSettings::setEncoderProfile(int v)
{
  if (mEncoderProfile == v) return;
  mEncoderProfile = v;
  TTCut::encoderProfile = v;
}

void TTSettings::setPreviewPreset(int v)
{
  if (mPreviewPreset == v) return;
  mPreviewPreset = v;
  TTCut::previewPreset = v;
}

void TTSettings::load()
{
  // Match TTCutSettings persistence target (QSettings("TTCut-ng", "TTCut-ng"))
  // so both code paths read/write the same on-disk file during the
  // Strangler-pattern migration window.
  QSettings settings("TTCut-ng", "TTCut-ng");
  settings.beginGroup("/Settings");
  // Per-group field loads added in tasks 4-13. Each task enters its own
  // sub-group (e.g. "Common", "Navigation") via beginGroup/endGroup using
  // the EXACT key strings inventoried in
  // docs/superpowers/plans/2026-05-03-task-1-qsettings-inventory.md
  // — that key-compatibility invariant lets the user's existing settings
  // survive the refactor.

  // ----- Navigation group (Tasks 4-5) ----------------------------------
  // fastSlider (Task 4) + step fields (Task 5) share /Settings/Navigation.
  // Legacy code entered the group twice; TTSettings consolidates to one.
  settings.beginGroup("Navigation");
  mFastSlider = settings.value("FastSlider/", mFastSlider).toBool();
  TTCut::fastSlider = mFastSlider;
  mStepSliderClick = settings.value("StepSliderClick/", mStepSliderClick).toInt();
  TTCut::stepSliderClick = mStepSliderClick;
  mStepPgUpDown    = settings.value("StepPgUpDown/",    mStepPgUpDown).toInt();
  TTCut::stepPgUpDown    = mStepPgUpDown;
  mStepArrowKeys   = settings.value("StepArrowKeys/",   mStepArrowKeys).toInt();
  TTCut::stepArrowKeys   = mStepArrowKeys;
  mStepPlusAlt     = settings.value("StepPlusAlt/",     mStepPlusAlt).toInt();
  TTCut::stepPlusAlt     = mStepPlusAlt;
  mStepPlusCtrl    = settings.value("StepPlusCtrl/",    mStepPlusCtrl).toInt();
  TTCut::stepPlusCtrl    = mStepPlusCtrl;
  mStepPlusShift   = settings.value("StepPlusShift/",   mStepPlusShift).toInt();
  TTCut::stepPlusShift   = mStepPlusShift;
  mStepMouseWheel  = settings.value("StepMouseWheel/",  mStepMouseWheel).toInt();
  TTCut::stepMouseWheel  = mStepMouseWheel;
  settings.endGroup();

  settings.beginGroup("Common");
  mTempDirPath     = settings.value("TempDirPath/",     mTempDirPath).toString();
  mLastDirPath     = settings.value("LastDirPath/",     mLastDirPath).toString();
  mProjectFileName = settings.value("ProjectFileName/", mProjectFileName).toString();
  TTCut::tempDirPath     = mTempDirPath;
  TTCut::lastDirPath     = mLastDirPath;
  TTCut::projectFileName = mProjectFileName;
  settings.endGroup();

  settings.beginGroup("Preview");
  mCutPreviewSeconds = settings.value("PreviewSeconds/", mCutPreviewSeconds).toInt();
  mPlaySkipFrames    = settings.value("SkipFrames/",     mPlaySkipFrames).toInt();
  TTCut::cutPreviewSeconds = mCutPreviewSeconds;
  TTCut::playSkipFrames    = mPlaySkipFrames;
  settings.endGroup();

  settings.beginGroup("Search");
  mSearchLength   = settings.value("Length/",   mSearchLength).toInt();
  mSearchAccuracy = settings.value("Accuracy/", mSearchAccuracy).toInt();
  TTCut::searchLength   = mSearchLength;
  TTCut::searchAccuracy = mSearchAccuracy;
  settings.endGroup();

  // ----- Index Files group (Task 6) ------------------------------------
  // NOTE: ReadAudioIDD has NO trailing slash — pre-existing legacy quirk
  // preserved verbatim from the Task 1 inventory to keep already-installed
  // user settings round-tripping. The 6 sibling keys all carry a slash.
  settings.beginGroup("IndexFiles");
  mCreateVideoIDD = settings.value("CreateVideoIDD/", mCreateVideoIDD).toBool();
  mCreateAudioIDD = settings.value("CreateAudioIDD/", mCreateAudioIDD).toBool();
  mCreatePrevIDD  = settings.value("CreatePrevIDD/",  mCreatePrevIDD).toBool();
  mCreateD2V      = settings.value("CreateD2V/",      mCreateD2V).toBool();
  mReadVideoIDD   = settings.value("ReadVideoIDD/",   mReadVideoIDD).toBool();
  mReadAudioIDD   = settings.value("ReadAudioIDD",    mReadAudioIDD).toBool();   // NO slash
  mReadPrevIDD    = settings.value("ReadPrevIDD/",    mReadPrevIDD).toBool();
  TTCut::createVideoIDD = mCreateVideoIDD;
  TTCut::createAudioIDD = mCreateAudioIDD;
  TTCut::createPrevIDD  = mCreatePrevIDD;
  TTCut::createD2V      = mCreateD2V;
  TTCut::readVideoIDD   = mReadVideoIDD;
  TTCut::readAudioIDD   = mReadAudioIDD;
  TTCut::readPrevIDD    = mReadPrevIDD;
  settings.endGroup();

  // ----- Logging group (Task 6) ----------------------------------------
  settings.beginGroup("LogFile");
  mCreateLogFile     = settings.value("CreateLogFile/",     mCreateLogFile).toBool();
  mLogModeConsole    = settings.value("LogModeConsole/",    mLogModeConsole).toBool();
  mLogModeExtended   = settings.value("LogModeExtended/",   mLogModeExtended).toBool();
  mLogVideoIndexInfo = settings.value("LogVideoIndexInfo/", mLogVideoIndexInfo).toBool();
  mLogAudioIndexInfo = settings.value("LogAudioIndexInfo/", mLogAudioIndexInfo).toBool();
  TTCut::createLogFile     = mCreateLogFile;
  TTCut::logModeConsole    = mLogModeConsole;
  TTCut::logModeExtended   = mLogModeExtended;
  TTCut::logVideoIndexInfo = mLogVideoIndexInfo;
  TTCut::logAudioIndexInfo = mLogAudioIndexInfo;
  settings.endGroup();

  // ----- Recent Files group (Task 7) -----------------------------------
  settings.beginGroup("RecentFiles");
  mRecentFileList = settings.value("RecentFiles/", QStringList{}).toStringList();
  TTCut::recentFileList = mRecentFileList;
  settings.endGroup();

  // ----- Encoder Generic group (Task 8) --------------------------------
  // Only 3 of the 6 fields round-trip through QSettings. The other 3
  // (encoderPreset, encoderCrf, encoderProfile) are transient working
  // values copied at runtime from the codec-specific settings (Task 9)
  // and persisted in the .ttcut project file
  // (data/ttcutprojectdata.cpp). See header comment for rationale.
  settings.beginGroup("Encoder");
  mEncoderMode   = settings.value("EncoderMode/",   mEncoderMode).toBool();
  mEncoderCodec  = settings.value("EncoderCodec/",  mEncoderCodec).toInt();
  mPreviewPreset = settings.value("PreviewPreset/", mPreviewPreset).toInt();
  TTCut::encoderMode   = mEncoderMode;
  TTCut::encoderCodec  = mEncoderCodec;
  TTCut::previewPreset = mPreviewPreset;
  settings.endGroup();

  settings.endGroup();
}

void TTSettings::save()
{
  // Match TTCutSettings persistence target — see load().
  QSettings settings("TTCut-ng", "TTCut-ng");
  settings.beginGroup("/Settings");
  // Per-group field saves added in tasks 4-13.

  // ----- Navigation group (Tasks 4-5) ----------------------------------
  // fastSlider (Task 4) + step fields (Task 5) share /Settings/Navigation.
  settings.beginGroup("Navigation");
  settings.setValue("FastSlider/",      mFastSlider);
  settings.setValue("StepSliderClick/", mStepSliderClick);
  settings.setValue("StepPgUpDown/",    mStepPgUpDown);
  settings.setValue("StepArrowKeys/",   mStepArrowKeys);
  settings.setValue("StepPlusAlt/",     mStepPlusAlt);
  settings.setValue("StepPlusCtrl/",    mStepPlusCtrl);
  settings.setValue("StepPlusShift/",   mStepPlusShift);
  settings.setValue("StepMouseWheel/",  mStepMouseWheel);
  settings.endGroup();

  settings.beginGroup("Common");
  settings.setValue("TempDirPath/",     mTempDirPath);
  settings.setValue("LastDirPath/",     mLastDirPath);
  settings.setValue("ProjectFileName/", mProjectFileName);
  settings.endGroup();

  settings.beginGroup("Preview");
  settings.setValue("PreviewSeconds/", mCutPreviewSeconds);
  settings.setValue("SkipFrames/",     mPlaySkipFrames);
  settings.endGroup();

  settings.beginGroup("Search");
  settings.setValue("Length/",   mSearchLength);
  settings.setValue("Accuracy/", mSearchAccuracy);
  settings.endGroup();

  // ----- Index Files group (Task 6) ------------------------------------
  // ReadAudioIDD intentionally has NO trailing slash — see load().
  settings.beginGroup("IndexFiles");
  settings.setValue("CreateVideoIDD/", mCreateVideoIDD);
  settings.setValue("CreateAudioIDD/", mCreateAudioIDD);
  settings.setValue("CreatePrevIDD/",  mCreatePrevIDD);
  settings.setValue("CreateD2V/",      mCreateD2V);
  settings.setValue("ReadVideoIDD/",   mReadVideoIDD);
  settings.setValue("ReadAudioIDD",    mReadAudioIDD);   // NO slash
  settings.setValue("ReadPrevIDD/",    mReadPrevIDD);
  settings.endGroup();

  // ----- Logging group (Task 6) ----------------------------------------
  settings.beginGroup("LogFile");
  settings.setValue("CreateLogFile/",     mCreateLogFile);
  settings.setValue("LogModeConsole/",    mLogModeConsole);
  settings.setValue("LogModeExtended/",   mLogModeExtended);
  settings.setValue("LogVideoIndexInfo/", mLogVideoIndexInfo);
  settings.setValue("LogAudioIndexInfo/", mLogAudioIndexInfo);
  settings.endGroup();

  // ----- Recent Files group (Task 7) -----------------------------------
  settings.beginGroup("RecentFiles");
  settings.setValue("RecentFiles/", mRecentFileList);
  settings.endGroup();

  // ----- Encoder Generic group (Task 8) --------------------------------
  // Only 3 of the 6 fields round-trip through QSettings — see load().
  settings.beginGroup("Encoder");
  settings.setValue("EncoderMode/",   mEncoderMode);
  settings.setValue("EncoderCodec/",  mEncoderCodec);
  settings.setValue("PreviewPreset/", mPreviewPreset);
  settings.endGroup();

  settings.endGroup();
}

void TTSettings::resetToDefaults()
{
  // Per-group field resets added in tasks 4-13. For now this is a no-op.
}
