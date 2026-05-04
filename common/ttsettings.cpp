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
  // Task 12: muxOutputPath default mirrors common/ttcut.cpp:210 (QDir::homePath()).
  // audioOnlyFormat default mirrors common/ttcut.cpp:220 (TTCut::AOF_OriginalES).
  mMuxOutputPath = QDir::homePath();
  mAudioOnlyFormat = TTCut::AOF_OriginalES;
  // Task 13: cutDirPath default mirrors common/ttcut.cpp:234 (QDir::currentPath()).
  mCutDirPath = QDir::currentPath();
  // Mirror Task 13 runtime defaults to the legacy statics so first
  // construction has consistent state before load() runs.
  TTCut::cutDirPath   = mCutDirPath;
  TTCut::muxFileName  = mMuxFileName;
  TTCut::cutVideoName = mCutVideoName;
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

// ---- Encoder Codec-Specific group setters (Task 9) -------------------------
// Each setter early-outs on no-op assignment and mirrors the new value to the
// legacy TTCut::xxx static so unmigrated call sites observe consistent state.
// No signals — codec-switch UI reads these synchronously after Task 8's
// encoderCodecChanged(int).

void TTSettings::setMpeg2Preset(int v)
{
  if (mMpeg2Preset == v) return;
  mMpeg2Preset = v;
  TTCut::mpeg2Preset = v;
}

void TTSettings::setMpeg2Crf(int v)
{
  if (mMpeg2Crf == v) return;
  mMpeg2Crf = v;
  TTCut::mpeg2Crf = v;
}

void TTSettings::setMpeg2Profile(int v)
{
  if (mMpeg2Profile == v) return;
  mMpeg2Profile = v;
  TTCut::mpeg2Profile = v;
}

void TTSettings::setMpeg2Muxer(int v)
{
  if (mMpeg2Muxer == v) return;
  mMpeg2Muxer = v;
  TTCut::mpeg2Muxer = v;
}

void TTSettings::setH264Preset(int v)
{
  if (mH264Preset == v) return;
  mH264Preset = v;
  TTCut::h264Preset = v;
}

void TTSettings::setH264Crf(int v)
{
  if (mH264Crf == v) return;
  mH264Crf = v;
  TTCut::h264Crf = v;
}

void TTSettings::setH264Profile(int v)
{
  if (mH264Profile == v) return;
  mH264Profile = v;
  TTCut::h264Profile = v;
}

void TTSettings::setH264Muxer(int v)
{
  if (mH264Muxer == v) return;
  mH264Muxer = v;
  TTCut::h264Muxer = v;
}

void TTSettings::setH265Preset(int v)
{
  if (mH265Preset == v) return;
  mH265Preset = v;
  TTCut::h265Preset = v;
}

void TTSettings::setH265Crf(int v)
{
  if (mH265Crf == v) return;
  mH265Crf = v;
  TTCut::h265Crf = v;
}

void TTSettings::setH265Profile(int v)
{
  if (mH265Profile == v) return;
  mH265Profile = v;
  TTCut::h265Profile = v;
}

void TTSettings::setH265Muxer(int v)
{
  if (mH265Muxer == v) return;
  mH265Muxer = v;
  TTCut::h265Muxer = v;
}

void TTSettings::setMpeg2Target(int v)
{
  if (mMpeg2Target == v) return;
  mMpeg2Target = v;
  TTCut::mpeg2Target = v;
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
  TTCut::burstThresholdDb = v;
}

void TTSettings::setNormalizeAcmod(bool v)
{
  if (mNormalizeAcmod == v) return;
  mNormalizeAcmod = v;
  TTCut::normalizeAcmod = v;
}

void TTSettings::setAudioLanguagePreference(const QStringList& v)
{
  if (mAudioLanguagePreference == v) return;
  mAudioLanguagePreference = v;
  TTCut::audioLanguagePreference = v;
  emit audioLanguagePreferenceChanged(v);
}

void TTSettings::setQuickJumpIntervalSec(int v)
{
  if (mQuickJumpIntervalSec == v) return;
  mQuickJumpIntervalSec = v;
  TTCut::quickJumpIntervalSec = v;
}

void TTSettings::setScreenshotDir(const QString& v)
{
  if (mScreenshotDir == v) return;
  mScreenshotDir = v;
  TTCut::screenshotDir = v;
}

void TTSettings::setScreenshotProject(const QString& v)
{
  if (mScreenshotProject == v) return;
  mScreenshotProject = v;
  TTCut::screenshotProject = v;
}

// ---- Detection Thresholds group setters (Task 11) --------------------------
// Twelve setters: 3 nav-thresholds extend /Settings/Navigation, 7 sp* fields
// share /Settings/StreamPoints, 2 extraFrame* fields share /Settings/Common
// (legacy keys without `Sec` suffix). Each setter early-outs on no-op
// assignment and mirrors the new value to the legacy TTCut::xxx static so
// unmigrated call sites observe consistent state. No signals — none of the
// 12 fields have reactive UI dependents.

void TTSettings::setNavBlackThreshold(float v)
{
  if (mNavBlackThreshold == v) return;
  mNavBlackThreshold = v;
  TTCut::navBlackThreshold = v;
}

void TTSettings::setNavSceneThreshold(float v)
{
  if (mNavSceneThreshold == v) return;
  mNavSceneThreshold = v;
  TTCut::navSceneThreshold = v;
}

void TTSettings::setNavLogoThreshold(float v)
{
  if (mNavLogoThreshold == v) return;
  mNavLogoThreshold = v;
  TTCut::navLogoThreshold = v;
}

void TTSettings::setSpDetectSilence(bool v)
{
  if (mSpDetectSilence == v) return;
  mSpDetectSilence = v;
  TTCut::spDetectSilence = v;
}

void TTSettings::setSpSilenceThresholdDb(int v)
{
  if (mSpSilenceThresholdDb == v) return;
  mSpSilenceThresholdDb = v;
  TTCut::spSilenceThresholdDb = v;
}

void TTSettings::setSpSilenceMinDuration(float v)
{
  if (mSpSilenceMinDuration == v) return;
  mSpSilenceMinDuration = v;
  TTCut::spSilenceMinDuration = v;
}

void TTSettings::setSpDetectAudioChange(bool v)
{
  if (mSpDetectAudioChange == v) return;
  mSpDetectAudioChange = v;
  TTCut::spDetectAudioChange = v;
}

void TTSettings::setSpDetectAspectChange(bool v)
{
  if (mSpDetectAspectChange == v) return;
  mSpDetectAspectChange = v;
  TTCut::spDetectAspectChange = v;
}

void TTSettings::setSpDetectPillarbox(bool v)
{
  if (mSpDetectPillarbox == v) return;
  mSpDetectPillarbox = v;
  TTCut::spDetectPillarbox = v;
}

void TTSettings::setSpPillarboxThreshold(int v)
{
  if (mSpPillarboxThreshold == v) return;
  mSpPillarboxThreshold = v;
  TTCut::spPillarboxThreshold = v;
}

void TTSettings::setExtraFrameClusterGapSec(int v)
{
  if (mExtraFrameClusterGapSec == v) return;
  mExtraFrameClusterGapSec = v;
  TTCut::extraFrameClusterGapSec = v;
}

void TTSettings::setExtraFrameClusterOffsetSec(int v)
{
  if (mExtraFrameClusterOffsetSec == v) return;
  mExtraFrameClusterOffsetSec = v;
  TTCut::extraFrameClusterOffsetSec = v;
}

// ---- Muxer group setters (Task 12) -----------------------------------------
// Twelve setters extend the existing /Settings/Muxer block (Task 9 already
// populated mpeg2Target). Each setter early-outs on no-op assignment and
// mirrors the new value to the legacy TTCut::xxx static so unmigrated call
// sites observe consistent state. setOutputContainer also emits
// outputContainerChanged(int) so non-dialog subscribers can react to
// container switches uniformly.

void TTSettings::setMuxMode(int v)
{
  if (mMuxMode == v) return;
  mMuxMode = v;
  TTCut::muxMode = v;
}

void TTSettings::setMuxProg(const QString& v)
{
  if (mMuxProg == v) return;
  mMuxProg = v;
  TTCut::muxProg = v;
}

void TTSettings::setMuxProgPath(const QString& v)
{
  if (mMuxProgPath == v) return;
  mMuxProgPath = v;
  TTCut::muxProgPath = v;
}

void TTSettings::setMuxProgCmd(const QString& v)
{
  if (mMuxProgCmd == v) return;
  mMuxProgCmd = v;
  TTCut::muxProgCmd = v;
}

void TTSettings::setMuxOutputPath(const QString& v)
{
  if (mMuxOutputPath == v) return;
  mMuxOutputPath = v;
  TTCut::muxOutputPath = v;
}

void TTSettings::setMuxDeleteES(bool v)
{
  if (mMuxDeleteES == v) return;
  mMuxDeleteES = v;
  TTCut::muxDeleteES = v;
}

void TTSettings::setMuxPause(bool v)
{
  if (mMuxPause == v) return;
  mMuxPause = v;
  TTCut::muxPause = v;
}

void TTSettings::setOutputContainer(int v)
{
  if (mOutputContainer == v) return;
  mOutputContainer = v;
  TTCut::outputContainer = v;
  emit outputContainerChanged(v);
}

void TTSettings::setMkvCreateChapters(bool v)
{
  if (mMkvCreateChapters == v) return;
  mMkvCreateChapters = v;
  TTCut::mkvCreateChapters = v;
}

void TTSettings::setMkvChapterInterval(int v)
{
  if (mMkvChapterInterval == v) return;
  mMkvChapterInterval = v;
  TTCut::mkvChapterInterval = v;
}

void TTSettings::setAudioOnlyFormat(int v)
{
  if (mAudioOnlyFormat == v) return;
  mAudioOnlyFormat = v;
  TTCut::audioOnlyFormat = v;
}

void TTSettings::setAudioOnlyBitrateKbps(int v)
{
  if (mAudioOnlyBitrateKbps == v) return;
  mAudioOnlyBitrateKbps = v;
  TTCut::audioOnlyBitrateKbps = v;
}

// ---- Cut Settings & Chapter group setters (Task 13) ------------------------
// Eleven setters across two NEW QSettings sub-groups (CutOptions, Chapter)
// plus two non-persisted in-memory fields (muxFileName, cutVideoName). Each
// setter early-outs on no-op assignment and mirrors the new value to the
// legacy TTCut::xxx static so unmigrated call sites observe consistent state.
// No signals — none of these fields have reactive UI dependents.

void TTSettings::setMuxFileName(const QString& v)
{
  if (mMuxFileName == v) return;
  mMuxFileName = v;
  TTCut::muxFileName = v;
}

void TTSettings::setCutDirPath(const QString& v)
{
  if (mCutDirPath == v) return;
  mCutDirPath = v;
  TTCut::cutDirPath = v;
}

void TTSettings::setCutVideoName(const QString& v)
{
  if (mCutVideoName == v) return;
  mCutVideoName = v;
  TTCut::cutVideoName = v;
}

void TTSettings::setCutAddSuffix(bool v)
{
  if (mCutAddSuffix == v) return;
  mCutAddSuffix = v;
  TTCut::cutAddSuffix = v;
}

void TTSettings::setCutWriteMaxBitrate(bool v)
{
  if (mCutWriteMaxBitrate == v) return;
  mCutWriteMaxBitrate = v;
  TTCut::cutWriteMaxBitrate = v;
}

void TTSettings::setCutWriteSeqEnd(bool v)
{
  if (mCutWriteSeqEnd == v) return;
  mCutWriteSeqEnd = v;
  TTCut::cutWriteSeqEnd = v;
}

void TTSettings::setCorrectCutTimeCode(bool v)
{
  if (mCorrectCutTimeCode == v) return;
  mCorrectCutTimeCode = v;
  TTCut::correctCutTimeCode = v;
}

void TTSettings::setCorrectCutBitRate(bool v)
{
  if (mCorrectCutBitRate == v) return;
  mCorrectCutBitRate = v;
  TTCut::correctCutBitRate = v;
}

void TTSettings::setCreateCutIDD(bool v)
{
  if (mCreateCutIDD == v) return;
  mCreateCutIDD = v;
  TTCut::createCutIDD = v;
}

void TTSettings::setReadCutIDD(bool v)
{
  if (mReadCutIDD == v) return;
  mReadCutIDD = v;
  TTCut::readCutIDD = v;
}

void TTSettings::setSpumuxChapter(bool v)
{
  if (mSpumuxChapter == v) return;
  mSpumuxChapter = v;
  TTCut::spumuxChapter = v;
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
  // ----- Detection Thresholds (Task 11) -----
  mNavBlackThreshold = settings.value("BlackThreshold/", mNavBlackThreshold).toFloat();
  mNavSceneThreshold = settings.value("SceneThreshold/", mNavSceneThreshold).toFloat();
  mNavLogoThreshold  = settings.value("LogoThreshold/",  mNavLogoThreshold).toFloat();
  TTCut::navBlackThreshold = mNavBlackThreshold;
  TTCut::navSceneThreshold = mNavSceneThreshold;
  TTCut::navLogoThreshold  = mNavLogoThreshold;
  settings.endGroup();

  // ----- Stream Points group (Task 11) --------------------------------
  // Seven sp* fields persisted via /Settings/StreamPoints in the legacy
  // TTCutSettings path (gui/ttcutsettings.cpp lines 94-102). TTSettings
  // reuses the exact same group + key strings so already-installed user
  // settings round-trip across the migration window.
  settings.beginGroup("StreamPoints");
  mSpDetectSilence      = settings.value("DetectSilence/",      mSpDetectSilence).toBool();
  mSpSilenceThresholdDb = settings.value("SilenceThresholdDb/", mSpSilenceThresholdDb).toInt();
  mSpSilenceMinDuration = settings.value("SilenceMinDuration/", mSpSilenceMinDuration).toFloat();
  mSpDetectAudioChange  = settings.value("DetectAudioChange/",  mSpDetectAudioChange).toBool();
  mSpDetectAspectChange = settings.value("DetectAspectChange/", mSpDetectAspectChange).toBool();
  mSpDetectPillarbox    = settings.value("DetectPillarbox/",    mSpDetectPillarbox).toBool();
  mSpPillarboxThreshold = settings.value("PillarboxThreshold/", mSpPillarboxThreshold).toInt();
  TTCut::spDetectSilence      = mSpDetectSilence;
  TTCut::spSilenceThresholdDb = mSpSilenceThresholdDb;
  TTCut::spSilenceMinDuration = mSpSilenceMinDuration;
  TTCut::spDetectAudioChange  = mSpDetectAudioChange;
  TTCut::spDetectAspectChange = mSpDetectAspectChange;
  TTCut::spDetectPillarbox    = mSpDetectPillarbox;
  TTCut::spPillarboxThreshold = mSpPillarboxThreshold;
  settings.endGroup();

  settings.beginGroup("Common");
  mTempDirPath     = settings.value("TempDirPath/",     mTempDirPath).toString();
  mLastDirPath     = settings.value("LastDirPath/",     mLastDirPath).toString();
  mProjectFileName = settings.value("ProjectFileName/", mProjectFileName).toString();
  TTCut::tempDirPath     = mTempDirPath;
  TTCut::lastDirPath     = mLastDirPath;
  TTCut::projectFileName = mProjectFileName;
  // ----- Audio/QuickJump fields (Task 10) -----------------------------
  // Four fields extend the Common block. screenshotDir/Project live in
  // their own Screenshot block (below) per the Task 1 inventory.
  mBurstThresholdDb       = settings.value("BurstThresholdDb/",       mBurstThresholdDb).toInt();
  mNormalizeAcmod         = settings.value("NormalizeAcmod/",         mNormalizeAcmod).toBool();
  mAudioLanguagePreference = settings.value("AudioLanguagePreference/", QStringList{}).toStringList();
  mQuickJumpIntervalSec   = settings.value("QuickJumpInterval/",      mQuickJumpIntervalSec).toInt();
  TTCut::burstThresholdDb       = mBurstThresholdDb;
  TTCut::normalizeAcmod         = mNormalizeAcmod;
  TTCut::audioLanguagePreference = mAudioLanguagePreference;
  TTCut::quickJumpIntervalSec   = mQuickJumpIntervalSec;
  // ----- Extra Frame fields (Task 11) ---------------------------------
  // Two extraFrame* fields share /Settings/Common with the Audio/QuickJump
  // block. Legacy keys are `ExtraFrameClusterGap/` and
  // `ExtraFrameClusterOffset/` (gui/ttcutsettings.cpp lines 80-81) — note
  // there is no `Sec` suffix on disk. Reused verbatim for round-trip
  // compatibility.
  mExtraFrameClusterGapSec    = settings.value("ExtraFrameClusterGap/",    mExtraFrameClusterGapSec).toInt();
  mExtraFrameClusterOffsetSec = settings.value("ExtraFrameClusterOffset/", mExtraFrameClusterOffsetSec).toInt();
  TTCut::extraFrameClusterGapSec    = mExtraFrameClusterGapSec;
  TTCut::extraFrameClusterOffsetSec = mExtraFrameClusterOffsetSec;
  settings.endGroup();

  // ----- Screenshot group (Task 10) ------------------------------------
  // NEW persistence keys — the screenshotDir/Project fields were
  // non-persistent TTCut statics before. Per Task 1 inventory, they
  // round-trip through `/Settings/Screenshot` with keys `Dir/` and
  // `Project/`. First-time load picks up empty defaults.
  settings.beginGroup("Screenshot");
  mScreenshotDir     = settings.value("Dir/",     mScreenshotDir).toString();
  mScreenshotProject = settings.value("Project/", mScreenshotProject).toString();
  TTCut::screenshotDir     = mScreenshotDir;
  TTCut::screenshotProject = mScreenshotProject;
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
  TTCut::mpeg2Preset  = mMpeg2Preset;
  TTCut::mpeg2Crf     = mMpeg2Crf;
  TTCut::mpeg2Profile = mMpeg2Profile;
  TTCut::mpeg2Muxer   = mMpeg2Muxer;
  TTCut::h264Preset   = mH264Preset;
  TTCut::h264Crf      = mH264Crf;
  TTCut::h264Profile  = mH264Profile;
  TTCut::h264Muxer    = mH264Muxer;
  TTCut::h265Preset   = mH265Preset;
  TTCut::h265Crf      = mH265Crf;
  TTCut::h265Profile  = mH265Profile;
  TTCut::h265Muxer    = mH265Muxer;
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
  TTCut::mpeg2Target = mMpeg2Target;
  mMuxMode             = settings.value("MuxMode/",             mMuxMode).toInt();
  mMuxProg             = settings.value("MuxProg/",             mMuxProg).toString();
  mMuxProgPath         = settings.value("MuxProgPath/",         mMuxProgPath).toString();
  mMuxProgCmd          = settings.value("MuxProgCmd/",          mMuxProgCmd).toString();
  mMuxOutputPath       = settings.value("MuxOutputDir/",        mMuxOutputPath).toString();   // key is "MuxOutputDir/"
  mMuxDeleteES         = settings.value("MuxDeleteES/",         mMuxDeleteES).toBool();
  mMuxPause            = settings.value("MuxPause/",            mMuxPause).toBool();
  mOutputContainer     = settings.value("OutputContainer/",     mOutputContainer).toInt();
  // Legacy migration: the MP4 option (value 2) was removed; remap any
  // stale persisted value of 2 to MKV (1). Mirrors the migration in
  // gui/ttcutsettings.cpp:193-195. The sibling mpeg2/h264/h265 Muxer ==2
  // migrations belong to Task 9 and are intentionally NOT replicated here.
  if (mOutputContainer == 2) mOutputContainer = 1;
  mMkvCreateChapters   = settings.value("MkvCreateChapters/",   mMkvCreateChapters).toBool();
  mMkvChapterInterval  = settings.value("MkvChapterInterval/",  mMkvChapterInterval).toInt();
  mAudioOnlyFormat     = settings.value("AudioOnlyFormat/",     mAudioOnlyFormat).toInt();
  mAudioOnlyBitrateKbps = settings.value("AudioOnlyBitrateKbps/", mAudioOnlyBitrateKbps).toInt();
  TTCut::muxMode             = mMuxMode;
  TTCut::muxProg             = mMuxProg;
  TTCut::muxProgPath         = mMuxProgPath;
  TTCut::muxProgCmd          = mMuxProgCmd;
  TTCut::muxOutputPath       = mMuxOutputPath;
  TTCut::muxDeleteES         = mMuxDeleteES;
  TTCut::muxPause            = mMuxPause;
  TTCut::outputContainer     = mOutputContainer;
  TTCut::mkvCreateChapters   = mMkvCreateChapters;
  TTCut::mkvChapterInterval  = mMkvChapterInterval;
  TTCut::audioOnlyFormat     = mAudioOnlyFormat;
  TTCut::audioOnlyBitrateKbps = mAudioOnlyBitrateKbps;
  settings.endGroup();

  // ----- Cut Options group (Task 13) -----------------------------------
  // NEW sub-group of /Settings. Eight fields per the legacy
  // gui/ttcutsettings.cpp:219-228 read block. NOTE: the on-disk key for
  // correctCutBitRate is `CorrectBitrate/` (lowercase 'r') — preserved
  // verbatim from the legacy code so already-installed user settings
  // round-trip across the migration window.
  // After reading the directory back from disk we replicate the
  // gui/ttcutsettings.cpp:245-246 fallback validation: if the path no
  // longer exists, fall back to QDir::currentPath() and re-mirror.
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
  TTCut::cutDirPath         = mCutDirPath;
  TTCut::cutAddSuffix       = mCutAddSuffix;
  TTCut::cutWriteMaxBitrate = mCutWriteMaxBitrate;
  TTCut::cutWriteSeqEnd     = mCutWriteSeqEnd;
  TTCut::correctCutTimeCode = mCorrectCutTimeCode;
  TTCut::correctCutBitRate  = mCorrectCutBitRate;
  TTCut::createCutIDD       = mCreateCutIDD;
  TTCut::readCutIDD         = mReadCutIDD;
  settings.endGroup();

  // ----- Chapter group (Task 13) ---------------------------------------
  // NEW sub-group of /Settings. One field — spumuxChapter — per the
  // legacy gui/ttcutsettings.cpp:213-215 read block.
  settings.beginGroup("Chapter");
  mSpumuxChapter = settings.value("SpumuxChapter/", mSpumuxChapter).toBool();
  TTCut::spumuxChapter = mSpumuxChapter;
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
