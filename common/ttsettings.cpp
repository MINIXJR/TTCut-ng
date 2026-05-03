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

  // ----- Common Options group (Task 4) ---------------------------------
  // fastSlider lives in /Settings/Navigation per legacy layout.
  settings.beginGroup("Navigation");
  mFastSlider = settings.value("FastSlider/", mFastSlider).toBool();
  TTCut::fastSlider = mFastSlider;
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

  settings.endGroup();
}

void TTSettings::save()
{
  // Match TTCutSettings persistence target — see load().
  QSettings settings("TTCut-ng", "TTCut-ng");
  settings.beginGroup("/Settings");
  // Per-group field saves added in tasks 4-13.

  // ----- Common Options group (Task 4) ---------------------------------
  settings.beginGroup("Navigation");
  settings.setValue("FastSlider/", mFastSlider);
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

  settings.endGroup();
}

void TTSettings::resetToDefaults()
{
  // Per-group field resets added in tasks 4-13. For now this is a no-op.
}
