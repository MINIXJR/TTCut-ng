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
  // Runtime defaults that depend on environment.
  mTempDirPath = QDir::tempPath();
  mLastDirPath = QDir::homePath();
  mProjectFileName = QString();
  mMuxOutputPath = QDir::homePath();
  mAudioOnlyFormat = TTCut::AOF_OriginalES;
  mCutDirPath = QDir::currentPath();
}

TTSettings::~TTSettings()
{
}

// ---- Common Options group setters (Task 4) ---------------------------------
// Each setter early-outs on no-op assignment.

void TTSettings::setFastSlider(bool v)
{
  if (mFastSlider == v) return;
  mFastSlider = v;
}

void TTSettings::setTempDirPath(const QString& v)
{
  if (mTempDirPath == v) return;
  mTempDirPath = v;
}

void TTSettings::setLastDirPath(const QString& v)
{
  if (mLastDirPath == v) return;
  mLastDirPath = v;
}

void TTSettings::setProjectFileName(const QString& v)
{
  if (mProjectFileName == v) return;
  mProjectFileName = v;
}

void TTSettings::setCutPreviewSeconds(int v)
{
  if (mCutPreviewSeconds == v) return;
  mCutPreviewSeconds = v;
}

void TTSettings::setPlaySkipFrames(int v)
{
  if (mPlaySkipFrames == v) return;
  mPlaySkipFrames = v;
}

void TTSettings::setSearchLength(int v)
{
  if (mSearchLength == v) return;
  mSearchLength = v;
}

void TTSettings::setSearchAccuracy(int v)
{
  if (mSearchAccuracy == v) return;
  mSearchAccuracy = v;
}

// ---- Navigation Steps group setters (Task 5) -------------------------------
// Each setter early-outs on no-op assignment.

void TTSettings::setStepSliderClick(int v)
{
  if (mStepSliderClick == v) return;
  mStepSliderClick = v;
}

void TTSettings::setStepPgUpDown(int v)
{
  if (mStepPgUpDown == v) return;
  mStepPgUpDown = v;
}

void TTSettings::setStepArrowKeys(int v)
{
  if (mStepArrowKeys == v) return;
  mStepArrowKeys = v;
}

void TTSettings::setStepPlusAlt(int v)
{
  if (mStepPlusAlt == v) return;
  mStepPlusAlt = v;
}

void TTSettings::setStepPlusCtrl(int v)
{
  if (mStepPlusCtrl == v) return;
  mStepPlusCtrl = v;
}

void TTSettings::setStepPlusShift(int v)
{
  if (mStepPlusShift == v) return;
  mStepPlusShift = v;
}

void TTSettings::setStepMouseWheel(int v)
{
  if (mStepMouseWheel == v) return;
  mStepMouseWheel = v;
}

// ---- Index Files & Logging group setters (Task 6) --------------------------
// Each setter early-outs on no-op assignment.

void TTSettings::setCreateVideoIDD(bool v)
{
  if (mCreateVideoIDD == v) return;
  mCreateVideoIDD = v;
}

void TTSettings::setCreateAudioIDD(bool v)
{
  if (mCreateAudioIDD == v) return;
  mCreateAudioIDD = v;
}

void TTSettings::setCreatePrevIDD(bool v)
{
  if (mCreatePrevIDD == v) return;
  mCreatePrevIDD = v;
}

void TTSettings::setCreateD2V(bool v)
{
  if (mCreateD2V == v) return;
  mCreateD2V = v;
}

void TTSettings::setReadVideoIDD(bool v)
{
  if (mReadVideoIDD == v) return;
  mReadVideoIDD = v;
}

void TTSettings::setReadAudioIDD(bool v)
{
  if (mReadAudioIDD == v) return;
  mReadAudioIDD = v;
}

void TTSettings::setReadPrevIDD(bool v)
{
  if (mReadPrevIDD == v) return;
  mReadPrevIDD = v;
}

void TTSettings::setCreateLogFile(bool v)
{
  if (mCreateLogFile == v) return;
  mCreateLogFile = v;
}

void TTSettings::setLogModeConsole(bool v)
{
  if (mLogModeConsole == v) return;
  mLogModeConsole = v;
}

void TTSettings::setLogModeExtended(bool v)
{
  if (mLogModeExtended == v) return;
  mLogModeExtended = v;
}

void TTSettings::setLogVideoIndexInfo(bool v)
{
  if (mLogVideoIndexInfo == v) return;
  mLogVideoIndexInfo = v;
}

void TTSettings::setLogAudioIndexInfo(bool v)
{
  if (mLogAudioIndexInfo == v) return;
  mLogAudioIndexInfo = v;
}

// ---- Recent Files group setter (Task 7) ------------------------------------
// Emits a change-notification signal. Mutating call sites
// (append/prepend/removeAll) must read-modify-write through this setter so
// the signal fires.

void TTSettings::setRecentFileList(const QStringList& v)
{
  if (mRecentFileList == v) return;
  mRecentFileList = v;
  emit recentFilesChanged(v);
}

// ---- Encoder Generic group setters (Task 8) --------------------------------
// Each setter early-outs on no-op assignment.
// setEncoderCodec also emits encoderCodecChanged(int) for non-dialog
// subscribers. encoderPreset/Crf/Profile have NO QSettings load/save
// round-trip — they are transient working values persisted in the .ttcut
// project file (data/ttcutprojectdata.cpp).

void TTSettings::setEncoderMode(bool v)
{
  if (mEncoderMode == v) return;
  mEncoderMode = v;
}

void TTSettings::setEncoderCodec(int v)
{
  if (mEncoderCodec == v) return;
  mEncoderCodec = v;
  emit encoderCodecChanged(v);
}

void TTSettings::setEncoderPreset(int v)
{
  if (mEncoderPreset == v) return;
  mEncoderPreset = v;
}

void TTSettings::setEncoderCrf(int v)
{
  if (mEncoderCrf == v) return;
  mEncoderCrf = v;
}

void TTSettings::setEncoderProfile(int v)
{
  if (mEncoderProfile == v) return;
  mEncoderProfile = v;
}

void TTSettings::setPreviewPreset(int v)
{
  if (mPreviewPreset == v) return;
  mPreviewPreset = v;
}

// ---- Encoder Codec-Specific group setters (Task 9) -------------------------
// Each setter early-outs on no-op assignment.
// No signals — codec-switch UI reads these synchronously after Task 8's
// encoderCodecChanged(int).

void TTSettings::setMpeg2Preset(int v)
{
  if (mMpeg2Preset == v) return;
  mMpeg2Preset = v;
}

void TTSettings::setMpeg2Crf(int v)
{
  if (mMpeg2Crf == v) return;
  mMpeg2Crf = v;
}

void TTSettings::setMpeg2Profile(int v)
{
  if (mMpeg2Profile == v) return;
  mMpeg2Profile = v;
}

void TTSettings::setMpeg2Muxer(int v)
{
  if (mMpeg2Muxer == v) return;
  mMpeg2Muxer = v;
}

void TTSettings::setH264Preset(int v)
{
  if (mH264Preset == v) return;
  mH264Preset = v;
}

void TTSettings::setH264Crf(int v)
{
  if (mH264Crf == v) return;
  mH264Crf = v;
}

void TTSettings::setH264Profile(int v)
{
  if (mH264Profile == v) return;
  mH264Profile = v;
}

void TTSettings::setH264Muxer(int v)
{
  if (mH264Muxer == v) return;
  mH264Muxer = v;
}

void TTSettings::setH265Preset(int v)
{
  if (mH265Preset == v) return;
  mH265Preset = v;
}

void TTSettings::setH265Crf(int v)
{
  if (mH265Crf == v) return;
  mH265Crf = v;
}

void TTSettings::setH265Profile(int v)
{
  if (mH265Profile == v) return;
  mH265Profile = v;
}

void TTSettings::setH265Muxer(int v)
{
  if (mH265Muxer == v) return;
  mH265Muxer = v;
}

void TTSettings::setMpeg2Target(int v)
{
  if (mMpeg2Target == v) return;
  mMpeg2Target = v;
}

// ---- Audio/QuickJump/Screenshot group setters (Task 10) --------------------
// Four fields extend /Settings/Common (Task 4). Two fields open a new
// /Settings/Screenshot block — newly persisted per Task 1 inventory.
// setAudioLanguagePreference emits audioLanguagePreferenceChanged so the
// audio-list sort comparator can re-sort reactively. The other 5 setters
// use the standard pattern.

void TTSettings::setBurstThresholdDb(int v)
{
  if (mBurstThresholdDb == v) return;
  mBurstThresholdDb = v;
}

void TTSettings::setNormalizeAcmod(bool v)
{
  if (mNormalizeAcmod == v) return;
  mNormalizeAcmod = v;
}

void TTSettings::setAudioLanguagePreference(const QStringList& v)
{
  if (mAudioLanguagePreference == v) return;
  mAudioLanguagePreference = v;
  emit audioLanguagePreferenceChanged(v);
}

void TTSettings::setQuickJumpIntervalSec(int v)
{
  if (mQuickJumpIntervalSec == v) return;
  mQuickJumpIntervalSec = v;
}

void TTSettings::setScreenshotDir(const QString& v)
{
  if (mScreenshotDir == v) return;
  mScreenshotDir = v;
}

void TTSettings::setScreenshotProject(const QString& v)
{
  if (mScreenshotProject == v) return;
  mScreenshotProject = v;
}

// ---- Detection Thresholds group setters (Task 11) --------------------------
// Twelve setters: 3 nav-thresholds extend /Settings/Navigation, 7 sp* fields
// share /Settings/StreamPoints, 2 extraFrame* fields share /Settings/Common
// (legacy keys without `Sec` suffix). Each setter early-outs on no-op
// assignment. No signals — none of the 12 fields have reactive UI dependents.

void TTSettings::setNavBlackThreshold(float v)
{
  if (mNavBlackThreshold == v) return;
  mNavBlackThreshold = v;
}

void TTSettings::setNavSceneThreshold(float v)
{
  if (mNavSceneThreshold == v) return;
  mNavSceneThreshold = v;
}

void TTSettings::setNavLogoThreshold(float v)
{
  if (mNavLogoThreshold == v) return;
  mNavLogoThreshold = v;
}

void TTSettings::setSpDetectSilence(bool v)
{
  if (mSpDetectSilence == v) return;
  mSpDetectSilence = v;
}

void TTSettings::setSpSilenceThresholdDb(int v)
{
  if (mSpSilenceThresholdDb == v) return;
  mSpSilenceThresholdDb = v;
}

void TTSettings::setSpSilenceMinDuration(float v)
{
  if (mSpSilenceMinDuration == v) return;
  mSpSilenceMinDuration = v;
}

void TTSettings::setSpDetectAudioChange(bool v)
{
  if (mSpDetectAudioChange == v) return;
  mSpDetectAudioChange = v;
}

void TTSettings::setSpDetectAspectChange(bool v)
{
  if (mSpDetectAspectChange == v) return;
  mSpDetectAspectChange = v;
}

void TTSettings::setSpDetectPillarbox(bool v)
{
  if (mSpDetectPillarbox == v) return;
  mSpDetectPillarbox = v;
}

void TTSettings::setSpPillarboxThreshold(int v)
{
  if (mSpPillarboxThreshold == v) return;
  mSpPillarboxThreshold = v;
}

void TTSettings::setExtraFrameClusterGapSec(int v)
{
  if (mExtraFrameClusterGapSec == v) return;
  mExtraFrameClusterGapSec = v;
}

void TTSettings::setExtraFrameClusterOffsetSec(int v)
{
  if (mExtraFrameClusterOffsetSec == v) return;
  mExtraFrameClusterOffsetSec = v;
}

// ---- Muxer group setters (Task 12) -----------------------------------------
// Twelve setters extend the existing /Settings/Muxer block (Task 9 already
// populated mpeg2Target). Each setter early-outs on no-op assignment.
// setOutputContainer also emits outputContainerChanged(int) so non-dialog
// subscribers can react to container switches uniformly.

void TTSettings::setMuxMode(int v)
{
  if (mMuxMode == v) return;
  mMuxMode = v;
}

void TTSettings::setMuxProg(const QString& v)
{
  if (mMuxProg == v) return;
  mMuxProg = v;
}

void TTSettings::setMuxProgPath(const QString& v)
{
  if (mMuxProgPath == v) return;
  mMuxProgPath = v;
}

void TTSettings::setMuxProgCmd(const QString& v)
{
  if (mMuxProgCmd == v) return;
  mMuxProgCmd = v;
}

void TTSettings::setMuxOutputPath(const QString& v)
{
  if (mMuxOutputPath == v) return;
  mMuxOutputPath = v;
}

void TTSettings::setMuxDeleteES(bool v)
{
  if (mMuxDeleteES == v) return;
  mMuxDeleteES = v;
}

void TTSettings::setMuxPause(bool v)
{
  if (mMuxPause == v) return;
  mMuxPause = v;
}

void TTSettings::setOutputContainer(int v)
{
  if (mOutputContainer == v) return;
  mOutputContainer = v;
  emit outputContainerChanged(v);
}

void TTSettings::setMkvCreateChapters(bool v)
{
  if (mMkvCreateChapters == v) return;
  mMkvCreateChapters = v;
}

void TTSettings::setMkvChapterInterval(int v)
{
  if (mMkvChapterInterval == v) return;
  mMkvChapterInterval = v;
}

void TTSettings::setAudioOnlyFormat(int v)
{
  if (mAudioOnlyFormat == v) return;
  mAudioOnlyFormat = v;
}

void TTSettings::setAudioOnlyBitrateKbps(int v)
{
  if (mAudioOnlyBitrateKbps == v) return;
  mAudioOnlyBitrateKbps = v;
}

// ---- Cut Settings & Chapter group setters (Task 13) ------------------------
// Ten setters across two QSettings sub-groups (CutOptions, Chapter) plus one
// non-persisted in-memory field (cutVideoName, per-project in .ttcut). Each
// setter early-outs on no-op assignment. No signals — none of these fields
// have reactive UI dependents.

void TTSettings::setCutDirPath(const QString& v)
{
  if (mCutDirPath == v) return;
  mCutDirPath = v;
}

void TTSettings::setCutVideoName(const QString& v)
{
  if (mCutVideoName == v) return;
  mCutVideoName = v;
}

void TTSettings::setCutAddSuffix(bool v)
{
  if (mCutAddSuffix == v) return;
  mCutAddSuffix = v;
}

void TTSettings::setCutWriteMaxBitrate(bool v)
{
  if (mCutWriteMaxBitrate == v) return;
  mCutWriteMaxBitrate = v;
}

void TTSettings::setCutWriteSeqEnd(bool v)
{
  if (mCutWriteSeqEnd == v) return;
  mCutWriteSeqEnd = v;
}

void TTSettings::setCorrectCutTimeCode(bool v)
{
  if (mCorrectCutTimeCode == v) return;
  mCorrectCutTimeCode = v;
}

void TTSettings::setCorrectCutBitRate(bool v)
{
  if (mCorrectCutBitRate == v) return;
  mCorrectCutBitRate = v;
}

void TTSettings::setCreateCutIDD(bool v)
{
  if (mCreateCutIDD == v) return;
  mCreateCutIDD = v;
}

void TTSettings::setReadCutIDD(bool v)
{
  if (mReadCutIDD == v) return;
  mReadCutIDD = v;
}

void TTSettings::setSpumuxChapter(bool v)
{
  if (mSpumuxChapter == v) return;
  mSpumuxChapter = v;
}

void TTSettings::load()
{
  // QSettings persistence target (legacy app/org name preserved for
  // round-trip with already-installed user settings).
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
  mStepSliderClick = settings.value("StepSliderClick/", mStepSliderClick).toInt();
  mStepPgUpDown    = settings.value("StepPgUpDown/",    mStepPgUpDown).toInt();
  mStepArrowKeys   = settings.value("StepArrowKeys/",   mStepArrowKeys).toInt();
  mStepPlusAlt     = settings.value("StepPlusAlt/",     mStepPlusAlt).toInt();
  mStepPlusCtrl    = settings.value("StepPlusCtrl/",    mStepPlusCtrl).toInt();
  mStepPlusShift   = settings.value("StepPlusShift/",   mStepPlusShift).toInt();
  mStepMouseWheel  = settings.value("StepMouseWheel/",  mStepMouseWheel).toInt();
  // ----- Detection Thresholds (Task 11) -----
  mNavBlackThreshold = settings.value("BlackThreshold/", mNavBlackThreshold).toFloat();
  mNavSceneThreshold = settings.value("SceneThreshold/", mNavSceneThreshold).toFloat();
  mNavLogoThreshold  = settings.value("LogoThreshold/",  mNavLogoThreshold).toFloat();
  settings.endGroup();

  // ----- Stream Points group (Task 11) --------------------------------
  // Seven sp* fields persisted via /Settings/StreamPoints. Group + key
  // strings preserved verbatim from the previous persistence layer so
  // already-installed user settings round-trip.
  settings.beginGroup("StreamPoints");
  mSpDetectSilence      = settings.value("DetectSilence/",      mSpDetectSilence).toBool();
  mSpSilenceThresholdDb = settings.value("SilenceThresholdDb/", mSpSilenceThresholdDb).toInt();
  mSpSilenceMinDuration = settings.value("SilenceMinDuration/", mSpSilenceMinDuration).toFloat();
  mSpDetectAudioChange  = settings.value("DetectAudioChange/",  mSpDetectAudioChange).toBool();
  mSpDetectAspectChange = settings.value("DetectAspectChange/", mSpDetectAspectChange).toBool();
  mSpDetectPillarbox    = settings.value("DetectPillarbox/",    mSpDetectPillarbox).toBool();
  mSpPillarboxThreshold = settings.value("PillarboxThreshold/", mSpPillarboxThreshold).toInt();
  settings.endGroup();

  settings.beginGroup("Common");
  mTempDirPath     = settings.value("TempDirPath/",     mTempDirPath).toString();
  mLastDirPath     = settings.value("LastDirPath/",     mLastDirPath).toString();
  mProjectFileName = settings.value("ProjectFileName/", mProjectFileName).toString();
  // ----- Audio/QuickJump fields (Task 10) -----------------------------
  // Four fields extend the Common block. screenshotDir/Project live in
  // their own Screenshot block (below) per the Task 1 inventory.
  mBurstThresholdDb       = settings.value("BurstThresholdDb/",       mBurstThresholdDb).toInt();
  mNormalizeAcmod         = settings.value("NormalizeAcmod/",         mNormalizeAcmod).toBool();
  mAudioLanguagePreference = settings.value("AudioLanguagePreference/", QStringList{}).toStringList();
  mQuickJumpIntervalSec   = settings.value("QuickJumpInterval/",      mQuickJumpIntervalSec).toInt();
  // ----- Extra Frame fields (Task 11) ---------------------------------
  // Two extraFrame* fields share /Settings/Common with the Audio/QuickJump
  // block. Legacy keys are `ExtraFrameClusterGap/` and
  // `ExtraFrameClusterOffset/` (gui/ttcutsettings.cpp lines 80-81) — note
  // there is no `Sec` suffix on disk. Reused verbatim for round-trip
  // compatibility.
  mExtraFrameClusterGapSec    = settings.value("ExtraFrameClusterGap/",    mExtraFrameClusterGapSec).toInt();
  mExtraFrameClusterOffsetSec = settings.value("ExtraFrameClusterOffset/", mExtraFrameClusterOffsetSec).toInt();
  settings.endGroup();

  // ----- Screenshot group (Task 10) ------------------------------------
  // NEW persistence keys — the screenshotDir/Project fields were
  // non-persistent TTCut statics before. Per Task 1 inventory, they
  // round-trip through `/Settings/Screenshot` with keys `Dir/` and
  // `Project/`. First-time load picks up empty defaults.
  settings.beginGroup("Screenshot");
  mScreenshotDir     = settings.value("Dir/",     mScreenshotDir).toString();
  mScreenshotProject = settings.value("Project/", mScreenshotProject).toString();
  settings.endGroup();

  settings.beginGroup("Preview");
  mCutPreviewSeconds = settings.value("PreviewSeconds/", mCutPreviewSeconds).toInt();
  mPlaySkipFrames    = settings.value("SkipFrames/",     mPlaySkipFrames).toInt();
  settings.endGroup();

  settings.beginGroup("Search");
  mSearchLength   = settings.value("Length/",   mSearchLength).toInt();
  mSearchAccuracy = settings.value("Accuracy/", mSearchAccuracy).toInt();
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
  settings.endGroup();

  // ----- Logging group (Task 6) ----------------------------------------
  settings.beginGroup("LogFile");
  mCreateLogFile     = settings.value("CreateLogFile/",     mCreateLogFile).toBool();
  mLogModeConsole    = settings.value("LogModeConsole/",    mLogModeConsole).toBool();
  mLogModeExtended   = settings.value("LogModeExtended/",   mLogModeExtended).toBool();
  mLogVideoIndexInfo = settings.value("LogVideoIndexInfo/", mLogVideoIndexInfo).toBool();
  mLogAudioIndexInfo = settings.value("LogAudioIndexInfo/", mLogAudioIndexInfo).toBool();
  settings.endGroup();

  // ----- Recent Files group (Task 7) -----------------------------------
  settings.beginGroup("RecentFiles");
  mRecentFileList = settings.value("RecentFiles/", QStringList{}).toStringList();
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
  // ----- Encoder Codec-Specific group (Task 9) -------------------------
  // 12 codec-specific fields share /Settings/Encoder with the Task 8
  // generic fields. mpeg2Target lives in /Settings/Muxer (block below).
  mMpeg2Preset  = settings.value("Mpeg2Preset/",  mMpeg2Preset).toInt();
  mMpeg2Crf     = settings.value("Mpeg2Crf/",     mMpeg2Crf).toInt();
  mMpeg2Profile = settings.value("Mpeg2Profile/", mMpeg2Profile).toInt();
  mMpeg2Muxer   = settings.value("Mpeg2Muxer/",   mMpeg2Muxer).toInt();
  mH264Preset   = settings.value("H264Preset/",   mH264Preset).toInt();
  mH264Crf      = settings.value("H264Crf/",      mH264Crf).toInt();
  mH264Profile  = settings.value("H264Profile/",  mH264Profile).toInt();
  mH264Muxer    = settings.value("H264Muxer/",    mH264Muxer).toInt();
  mH265Preset   = settings.value("H265Preset/",   mH265Preset).toInt();
  mH265Crf      = settings.value("H265Crf/",      mH265Crf).toInt();
  mH265Profile  = settings.value("H265Profile/",  mH265Profile).toInt();
  mH265Muxer    = settings.value("H265Muxer/",    mH265Muxer).toInt();
  // Legacy migration: the MP4 option (value 2) was removed; remap any stale
  // codec-specific Muxer values from existing user configs. Mirrors the
  // sibling migrations in former gui/ttcutsettings.cpp:196-198 (the matching
  // outputContainer migration is in the Muxer block below).
  if (mMpeg2Muxer == 2) mMpeg2Muxer = 1;
  if (mH264Muxer  == 2) mH264Muxer  = 1;
  if (mH265Muxer  == 2) mH265Muxer  = 1;
  // Replicate legacy initialisation: copy codec-specific Preset/Crf/Profile
  // into the transient encoder* working values based on encoderCodec.
  // Without this the cut pipeline reads compile-time defaults instead of
  // the user's codec-specific settings until the Settings dialog is opened.
  // Mirrors the switch in former gui/ttcutsettings.cpp:168-184.
  switch (mEncoderCodec) {
    case 0:  mEncoderPreset = mMpeg2Preset; mEncoderCrf = mMpeg2Crf; mEncoderProfile = mMpeg2Profile; break;
    case 1:  mEncoderPreset = mH264Preset;  mEncoderCrf = mH264Crf;  mEncoderProfile = mH264Profile;  break;
    case 2:  mEncoderPreset = mH265Preset;  mEncoderCrf = mH265Crf;  mEncoderProfile = mH265Profile;  break;
  }
  settings.endGroup();

  // ----- Muxer group (Tasks 9 + 12) ------------------------------------
  // Task 9 populated mpeg2Target; Task 12 fills out the remaining 12
  // fields. Key strings preserved verbatim from gui/ttcutsettings.cpp
  // (lines 187-209) for round-trip compatibility with already-installed
  // user settings.
  // NOTE: muxOutputPath persists under "MuxOutputDir/" (NOT
  // "MuxOutputPath/") — the on-disk key differs from the field name.
  settings.beginGroup("Muxer");
  mMpeg2Target = settings.value("Mpeg2Target/", mMpeg2Target).toInt();
  mMuxMode             = settings.value("MuxMode/",             mMuxMode).toInt();
  mMuxProg             = settings.value("MuxProg/",             mMuxProg).toString();
  mMuxProgPath         = settings.value("MuxProgPath/",         mMuxProgPath).toString();
  mMuxProgCmd          = settings.value("MuxProgCmd/",          mMuxProgCmd).toString();
  mMuxOutputPath       = settings.value("MuxOutputDir/",        mMuxOutputPath).toString();   // key is "MuxOutputDir/"
  mMuxDeleteES         = settings.value("MuxDeleteES/",         mMuxDeleteES).toBool();
  mMuxPause            = settings.value("MuxPause/",            mMuxPause).toBool();
  mOutputContainer     = settings.value("OutputContainer/",     mOutputContainer).toInt();
  // Legacy migration: the MP4 option (value 2) was removed; remap any
  // stale persisted value of 2 to MKV (1). Mirrors the migration in former
  // gui/ttcutsettings.cpp:193-195. Sibling codec-Muxer migrations are in
  // the Encoder block above.
  if (mOutputContainer == 2) mOutputContainer = 1;
  mMkvCreateChapters   = settings.value("MkvCreateChapters/",   mMkvCreateChapters).toBool();
  mMkvChapterInterval  = settings.value("MkvChapterInterval/",  mMkvChapterInterval).toInt();
  mAudioOnlyFormat     = settings.value("AudioOnlyFormat/",     mAudioOnlyFormat).toInt();
  mAudioOnlyBitrateKbps = settings.value("AudioOnlyBitrateKbps/", mAudioOnlyBitrateKbps).toInt();
  settings.endGroup();

  // ----- Cut Options group (Task 13) -----------------------------------
  // Sub-group of /Settings. Eight fields. NOTE: the on-disk key for
  // correctCutBitRate is `CorrectBitrate/` (lowercase 'r') — preserved
  // verbatim from the previous persistence layer so already-installed user
  // settings round-trip.
  // After reading the directory back from disk we apply a fallback
  // validation: if the path no longer exists, fall back to QDir::currentPath().
  settings.beginGroup("CutOptions");
  mCutDirPath          = settings.value("DirPath/",         mCutDirPath).toString();
  mCutAddSuffix        = settings.value("AddSuffix/",       mCutAddSuffix).toBool();
  mCutWriteMaxBitrate  = settings.value("WriteMaxBitrate/", mCutWriteMaxBitrate).toBool();
  mCutWriteSeqEnd      = settings.value("WriteSeqEnd/",     mCutWriteSeqEnd).toBool();
  mCorrectCutTimeCode  = settings.value("CorrectTimeCode/", mCorrectCutTimeCode).toBool();
  mCorrectCutBitRate   = settings.value("CorrectBitrate/",  mCorrectCutBitRate).toBool();   // lowercase 'r'
  mCreateCutIDD        = settings.value("CreateIDD/",       mCreateCutIDD).toBool();
  mReadCutIDD          = settings.value("ReadIDD/",         mReadCutIDD).toBool();
  if (!QDir(mCutDirPath).exists()) mCutDirPath = QDir::currentPath();
  settings.endGroup();

  // ----- Chapter group (Task 13) ---------------------------------------
  // NEW sub-group of /Settings. One field — spumuxChapter — per the
  // legacy gui/ttcutsettings.cpp:213-215 read block.
  settings.beginGroup("Chapter");
  mSpumuxChapter = settings.value("SpumuxChapter/", mSpumuxChapter).toBool();
  settings.endGroup();

  // ----- Orphan-key cleanup --------------------------------------------
  // Existing user configs may carry keys from removed/renamed fields.
  // Verified zero readers in the codebase as of 2026-05-04: removing them
  // here so the next save() writes a clean file. List is intentionally
  // explicit (no wildcard) — accidental deletion of a future field would
  // be silently destructive.
  static const struct { const char* group; const char* key; } orphanKeys[] = {
    { "CutOptions",   "VideoName/" },         // pre-Phase-B persisted cutVideoName
    { "Navigation",   "StepQuickJump/" },     // replaced by Common\QuickJumpInterval/
    { "StreamPoints", "DetectBlackFrames/" }, // black-frame detection moved to Navigation
    { "StreamPoints", "DetectSceneChange/" }, // scene-change detection moved to Navigation
    { "StreamPoints", "BlackThreshold/" },    // threshold moved to Navigation
    { "StreamPoints", "BlackMinDuration/" },  // legacy
    { "StreamPoints", "SceneThreshold/" },    // threshold moved to Navigation
    { "StreamPoints", "MinDistance/" },       // legacy
  };
  for (const auto& o : orphanKeys) {
    settings.beginGroup(o.group);
    settings.remove(o.key);
    settings.endGroup();
  }

  settings.endGroup();
}

void TTSettings::save()
{
  // QSettings persistence target — see load().
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
  // ----- Detection Thresholds (Task 11) -----
  settings.setValue("BlackThreshold/", mNavBlackThreshold);
  settings.setValue("SceneThreshold/", mNavSceneThreshold);
  settings.setValue("LogoThreshold/",  mNavLogoThreshold);
  settings.endGroup();

  // ----- Stream Points group (Task 11) --------------------------------
  settings.beginGroup("StreamPoints");
  settings.setValue("DetectSilence/",      mSpDetectSilence);
  settings.setValue("SilenceThresholdDb/", mSpSilenceThresholdDb);
  settings.setValue("SilenceMinDuration/", mSpSilenceMinDuration);
  settings.setValue("DetectAudioChange/",  mSpDetectAudioChange);
  settings.setValue("DetectAspectChange/", mSpDetectAspectChange);
  settings.setValue("DetectPillarbox/",    mSpDetectPillarbox);
  settings.setValue("PillarboxThreshold/", mSpPillarboxThreshold);
  settings.endGroup();

  settings.beginGroup("Common");
  settings.setValue("TempDirPath/",     mTempDirPath);
  settings.setValue("LastDirPath/",     mLastDirPath);
  settings.setValue("ProjectFileName/", mProjectFileName);
  // ----- Audio/QuickJump fields (Task 10) -----------------------------
  settings.setValue("BurstThresholdDb/",        mBurstThresholdDb);
  settings.setValue("NormalizeAcmod/",          mNormalizeAcmod);
  settings.setValue("AudioLanguagePreference/", mAudioLanguagePreference);
  settings.setValue("QuickJumpInterval/",       mQuickJumpIntervalSec);
  // ----- Extra Frame fields (Task 11) ---------------------------------
  // Legacy keys (no `Sec` suffix on disk) — see load().
  settings.setValue("ExtraFrameClusterGap/",    mExtraFrameClusterGapSec);
  settings.setValue("ExtraFrameClusterOffset/", mExtraFrameClusterOffsetSec);
  settings.endGroup();

  // ----- Screenshot group (Task 10 — first-time persisted) -------------
  settings.beginGroup("Screenshot");
  settings.setValue("Dir/",     mScreenshotDir);
  settings.setValue("Project/", mScreenshotProject);
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
  // ----- Encoder Codec-Specific group (Task 9) -------------------------
  settings.setValue("Mpeg2Preset/",  mMpeg2Preset);
  settings.setValue("Mpeg2Crf/",     mMpeg2Crf);
  settings.setValue("Mpeg2Profile/", mMpeg2Profile);
  settings.setValue("Mpeg2Muxer/",   mMpeg2Muxer);
  settings.setValue("H264Preset/",   mH264Preset);
  settings.setValue("H264Crf/",      mH264Crf);
  settings.setValue("H264Profile/",  mH264Profile);
  settings.setValue("H264Muxer/",    mH264Muxer);
  settings.setValue("H265Preset/",   mH265Preset);
  settings.setValue("H265Crf/",      mH265Crf);
  settings.setValue("H265Profile/",  mH265Profile);
  settings.setValue("H265Muxer/",    mH265Muxer);
  settings.endGroup();

  // ----- Muxer group (Tasks 9 + 12) ------------------------------------
  // NOTE: muxOutputPath persists under "MuxOutputDir/" (NOT
  // "MuxOutputPath/") — the on-disk key differs from the field name.
  settings.beginGroup("Muxer");
  settings.setValue("Mpeg2Target/",         mMpeg2Target);
  settings.setValue("MuxMode/",             mMuxMode);
  settings.setValue("MuxProg/",             mMuxProg);
  settings.setValue("MuxProgPath/",         mMuxProgPath);
  settings.setValue("MuxProgCmd/",          mMuxProgCmd);
  settings.setValue("MuxOutputDir/",        mMuxOutputPath);   // key is "MuxOutputDir/"
  settings.setValue("MuxDeleteES/",         mMuxDeleteES);
  settings.setValue("MuxPause/",            mMuxPause);
  settings.setValue("OutputContainer/",     mOutputContainer);
  settings.setValue("MkvCreateChapters/",   mMkvCreateChapters);
  settings.setValue("MkvChapterInterval/",  mMkvChapterInterval);
  settings.setValue("AudioOnlyFormat/",     mAudioOnlyFormat);
  settings.setValue("AudioOnlyBitrateKbps/", mAudioOnlyBitrateKbps);
  settings.endGroup();

  // ----- Cut Options group (Task 13) -----------------------------------
  // CorrectBitrate/ key is lowercase 'r' — see load().
  settings.beginGroup("CutOptions");
  settings.setValue("DirPath/",         mCutDirPath);
  settings.setValue("AddSuffix/",       mCutAddSuffix);
  settings.setValue("WriteMaxBitrate/", mCutWriteMaxBitrate);
  settings.setValue("WriteSeqEnd/",     mCutWriteSeqEnd);
  settings.setValue("CorrectTimeCode/", mCorrectCutTimeCode);
  settings.setValue("CorrectBitrate/",  mCorrectCutBitRate);   // lowercase 'r'
  settings.setValue("CreateIDD/",       mCreateCutIDD);
  settings.setValue("ReadIDD/",         mReadCutIDD);
  settings.endGroup();

  // ----- Chapter group (Task 13) ---------------------------------------
  settings.beginGroup("Chapter");
  settings.setValue("SpumuxChapter/", mSpumuxChapter);
  settings.endGroup();

  settings.endGroup();
}

void TTSettings::resetToDefaults()
{
  // Per-group field resets added in tasks 4-13. For now this is a no-op.
}
