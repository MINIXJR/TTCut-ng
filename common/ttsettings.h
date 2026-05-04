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

  // ----- Encoder Generic group setters (Task 8) ---------------------------
  // Setters mirror to legacy TTCut::xxx during the migration window so call
  // sites that have not been migrated yet still observe consistent state.
  // setEncoderCodec also emits encoderCodecChanged(int) so non-dialog
  // subscribers can react to codec switches; the existing
  // TTCutSettingsEncoder::codecChanged signal continues to drive the
  // settings-dialog UI and is unchanged by this task.
  //
  // NOTE: encoderPreset, encoderCrf, encoderProfile are TRANSIENT working
  // values copied at runtime from the codec-specific settings
  // (mpeg2/h264/h265 Preset/Crf/Profile — Task 9). They are persisted in
  // the .ttcut project file (data/ttcutprojectdata.cpp), NOT in QSettings,
  // and therefore have no load()/save() round-trip in TTSettings — only
  // in-memory storage with mirror-write setters.
  bool    encoderMode() const        { return mEncoderMode; }
  void    setEncoderMode(bool v);

  int     encoderCodec() const       { return mEncoderCodec; }
  void    setEncoderCodec(int v);

  int     encoderPreset() const      { return mEncoderPreset; }
  void    setEncoderPreset(int v);

  int     encoderCrf() const         { return mEncoderCrf; }
  void    setEncoderCrf(int v);

  int     encoderProfile() const     { return mEncoderProfile; }
  void    setEncoderProfile(int v);

  int     previewPreset() const      { return mPreviewPreset; }
  void    setPreviewPreset(int v);

  // ----- Encoder Codec-Specific group setters (Task 9) --------------------
  // Setters mirror to legacy TTCut::xxx during the migration window so call
  // sites that have not been migrated yet still observe consistent state.
  // No signals — the codec-switch UI in TTCutSettingsEncoder/Muxer reads
  // these fields synchronously after setEncoderCodec() emits
  // encoderCodecChanged(int) (Task 8); no further per-field notification
  // is needed.
  int     mpeg2Preset() const        { return mMpeg2Preset; }
  void    setMpeg2Preset(int v);

  int     mpeg2Crf() const           { return mMpeg2Crf; }
  void    setMpeg2Crf(int v);

  int     mpeg2Profile() const       { return mMpeg2Profile; }
  void    setMpeg2Profile(int v);

  int     mpeg2Muxer() const         { return mMpeg2Muxer; }
  void    setMpeg2Muxer(int v);

  int     h264Preset() const         { return mH264Preset; }
  void    setH264Preset(int v);

  int     h264Crf() const            { return mH264Crf; }
  void    setH264Crf(int v);

  int     h264Profile() const        { return mH264Profile; }
  void    setH264Profile(int v);

  int     h264Muxer() const          { return mH264Muxer; }
  void    setH264Muxer(int v);

  int     h265Preset() const         { return mH265Preset; }
  void    setH265Preset(int v);

  int     h265Crf() const            { return mH265Crf; }
  void    setH265Crf(int v);

  int     h265Profile() const        { return mH265Profile; }
  void    setH265Profile(int v);

  int     h265Muxer() const          { return mH265Muxer; }
  void    setH265Muxer(int v);

  // mpeg2Target lives in /Settings/Muxer (Task 12 will fill the rest of
  // the Muxer group).
  int     mpeg2Target() const        { return mMpeg2Target; }
  void    setMpeg2Target(int v);

  // ----- Audio/QuickJump/Screenshot group setters (Task 10) ---------------
  // Four fields extend the existing /Settings/Common block (Task 4). Two
  // fields open a NEW /Settings/Screenshot block — those are first-time
  // persistence keys per the Task 1 inventory recommendation.
  // setAudioLanguagePreference emits audioLanguagePreferenceChanged so the
  // audio-list sort comparator can re-sort reactively when the user edits
  // the preference list. The other 5 setters use the standard pattern.
  int     burstThresholdDb() const   { return mBurstThresholdDb; }
  void    setBurstThresholdDb(int v);

  bool    normalizeAcmod() const     { return mNormalizeAcmod; }
  void    setNormalizeAcmod(bool v);

  const QStringList& audioLanguagePreference() const { return mAudioLanguagePreference; }
  void    setAudioLanguagePreference(const QStringList& v);

  int     quickJumpIntervalSec() const { return mQuickJumpIntervalSec; }
  void    setQuickJumpIntervalSec(int v);

  const QString& screenshotDir() const     { return mScreenshotDir; }
  void    setScreenshotDir(const QString& v);

  const QString& screenshotProject() const { return mScreenshotProject; }
  void    setScreenshotProject(const QString& v);

  // ----- Detection Thresholds group (Task 11) -----------------------------
  // Twelve fields across two existing legacy persistence groups. Three
  // nav-threshold fields extend /Settings/Navigation (Tasks 4-5). Seven
  // StreamPoint fields share /Settings/StreamPoints. Two ExtraFrame fields
  // share /Settings/Common with the Audio/QuickJump fields (Task 10) —
  // their on-disk keys (`ExtraFrameClusterGap/`, `ExtraFrameClusterOffset/`)
  // carry no `Sec` suffix; preserved verbatim from gui/ttcutsettings.cpp
  // for round-trip compatibility with already-installed user settings.
  // No signals — none of the 12 fields have reactive UI dependents.
  // Setters mirror to legacy TTCut::xxx during the migration window so
  // unmigrated call sites continue to observe consistent state.
  float   navBlackThreshold() const  { return mNavBlackThreshold; }
  void    setNavBlackThreshold(float v);

  float   navSceneThreshold() const  { return mNavSceneThreshold; }
  void    setNavSceneThreshold(float v);

  float   navLogoThreshold() const   { return mNavLogoThreshold; }
  void    setNavLogoThreshold(float v);

  bool    spDetectSilence() const      { return mSpDetectSilence; }
  void    setSpDetectSilence(bool v);

  int     spSilenceThresholdDb() const { return mSpSilenceThresholdDb; }
  void    setSpSilenceThresholdDb(int v);

  float   spSilenceMinDuration() const { return mSpSilenceMinDuration; }
  void    setSpSilenceMinDuration(float v);

  bool    spDetectAudioChange() const  { return mSpDetectAudioChange; }
  void    setSpDetectAudioChange(bool v);

  bool    spDetectAspectChange() const { return mSpDetectAspectChange; }
  void    setSpDetectAspectChange(bool v);

  bool    spDetectPillarbox() const    { return mSpDetectPillarbox; }
  void    setSpDetectPillarbox(bool v);

  int     spPillarboxThreshold() const { return mSpPillarboxThreshold; }
  void    setSpPillarboxThreshold(int v);

  int     extraFrameClusterGapSec() const    { return mExtraFrameClusterGapSec; }
  void    setExtraFrameClusterGapSec(int v);

  int     extraFrameClusterOffsetSec() const { return mExtraFrameClusterOffsetSec; }
  void    setExtraFrameClusterOffsetSec(int v);

  // ----- Muxer group (Task 12) --------------------------------------------
  // Twelve fields extend the existing /Settings/Muxer block (Task 9 already
  // populated mpeg2Target). Setters mirror to legacy TTCut::xxx during the
  // migration window so call sites that have not been migrated yet still
  // observe consistent state. setOutputContainer additionally emits
  // outputContainerChanged(int) so non-dialog subscribers (e.g. cut-target
  // file-extension logic) can react to container switches uniformly.
  int     muxMode() const                  { return mMuxMode; }
  void    setMuxMode(int v);

  QString muxProg() const                  { return mMuxProg; }
  void    setMuxProg(const QString& v);

  QString muxProgPath() const              { return mMuxProgPath; }
  void    setMuxProgPath(const QString& v);

  QString muxProgCmd() const               { return mMuxProgCmd; }
  void    setMuxProgCmd(const QString& v);

  QString muxOutputPath() const            { return mMuxOutputPath; }
  void    setMuxOutputPath(const QString& v);

  bool    muxDeleteES() const              { return mMuxDeleteES; }
  void    setMuxDeleteES(bool v);

  bool    muxPause() const                 { return mMuxPause; }
  void    setMuxPause(bool v);

  int     outputContainer() const          { return mOutputContainer; }
  void    setOutputContainer(int v);

  bool    mkvCreateChapters() const        { return mMkvCreateChapters; }
  void    setMkvCreateChapters(bool v);

  int     mkvChapterInterval() const       { return mMkvChapterInterval; }
  void    setMkvChapterInterval(int v);

  int     audioOnlyFormat() const          { return mAudioOnlyFormat; }
  void    setAudioOnlyFormat(int v);

  int     audioOnlyBitrateKbps() const     { return mAudioOnlyBitrateKbps; }
  void    setAudioOnlyBitrateKbps(int v);

  // ----- Cut Settings & Chapter group (Task 13) ---------------------------
  // Eleven fields total. Nine round-trip through QSettings across two NEW
  // sub-groups of /Settings:
  //   - /Settings/CutOptions (8 fields): cutDirPath, cutAddSuffix,
  //     cutWriteMaxBitrate, cutWriteSeqEnd, correctCutTimeCode,
  //     correctCutBitRate, createCutIDD, readCutIDD
  //   - /Settings/Chapter (1 field): spumuxChapter
  // Two fields are NOT persisted in QSettings — the Strangler keeps them
  // mirrored anyway so call sites observe consistent state:
  //   - muxFileName: de-facto constant ("muxscript.sh"); zero write sites
  //     in code, no load/save round-trip
  //   - cutVideoName: per-project value, persisted in the .ttcut project
  //     file via data/ttcutprojectdata.cpp, NOT in QSettings; no load/save
  //     round-trip
  // No signals — none of these fields have reactive UI dependents.
  // Setters mirror to legacy TTCut::xxx during the migration window so
  // unmigrated call sites continue to observe consistent state.
  // load() additionally replicates the cutDirPath fallback-to-currentPath
  // validation from gui/ttcutsettings.cpp:245-246.
  // Ground truth for on-disk keys: gui/ttcutsettings.cpp lines 211-247
  // (read) and 381-397 (write).
  QString muxFileName() const                  { return mMuxFileName; }
  void    setMuxFileName(const QString& v);

  QString cutDirPath() const                   { return mCutDirPath; }
  void    setCutDirPath(const QString& v);

  QString cutVideoName() const                 { return mCutVideoName; }
  void    setCutVideoName(const QString& v);

  bool    cutAddSuffix() const                 { return mCutAddSuffix; }
  void    setCutAddSuffix(bool v);

  bool    cutWriteMaxBitrate() const           { return mCutWriteMaxBitrate; }
  void    setCutWriteMaxBitrate(bool v);

  bool    cutWriteSeqEnd() const               { return mCutWriteSeqEnd; }
  void    setCutWriteSeqEnd(bool v);

  bool    correctCutTimeCode() const           { return mCorrectCutTimeCode; }
  void    setCorrectCutTimeCode(bool v);

  bool    correctCutBitRate() const            { return mCorrectCutBitRate; }
  void    setCorrectCutBitRate(bool v);

  bool    createCutIDD() const                 { return mCreateCutIDD; }
  void    setCreateCutIDD(bool v);

  bool    readCutIDD() const                   { return mReadCutIDD; }
  void    setReadCutIDD(bool v);

  bool    spumuxChapter() const                { return mSpumuxChapter; }
  void    setSpumuxChapter(bool v);

signals:
  // Per-group selective change signals added in tasks 4-13.
  // Tasks 4-6 declare none (no UI dependents need change-notification).
  // Task 7 adds the first signal: the recent-files menu is the canonical
  // subscriber, but follow-up commits will wire that up.
  void recentFilesChanged(const QStringList& list);

  // Task 8: emitted by setEncoderCodec so non-dialog subscribers (e.g.
  // muxer settings page, project-load handlers) can react to codec
  // switches uniformly. The settings dialog keeps its own codecChanged
  // signal for intra-dialog wiring.
  void encoderCodecChanged(int v);

  // Task 10: emitted by setAudioLanguagePreference so the audio-list sort
  // comparator (TTAudioItem::operator<) and any list views observing the
  // ordering can re-sort reactively when the user edits the preference
  // list. Mutating call sites must read-modify-write through the setter
  // so the signal fires and the legacy mirror stays consistent.
  void audioLanguagePreferenceChanged(const QStringList& v);

  // Task 12: emitted by setOutputContainer so non-dialog subscribers
  // (e.g. cut-target file-extension logic, mux-target path resolution)
  // can react to container switches uniformly. The settings dialog wires
  // its own intra-dialog signals separately and is unchanged by this task.
  void outputContainerChanged(int v);

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

  // ----- Encoder Generic group (Task 8) ------------------------------------
  // Defaults match common/ttcut.cpp lines 142-148 + 169 verbatim.
  // mEncoderPreset/Crf/Profile are TRANSIENT working values — see the
  // commentary above the public setters for the rationale.
  bool    mEncoderMode    = true;
  int     mEncoderCodec   = 0;   // 0=MPEG-2, 1=H.264, 2=H.265
  int     mEncoderPreset  = 4;   // transient working value (project-file persisted)
  int     mEncoderCrf     = 2;   // transient working value (project-file persisted)
  int     mEncoderProfile = 0;   // transient working value (project-file persisted)
  int     mPreviewPreset  = 0;   // ultrafast (preview speed over quality)

  // ----- Encoder Codec-Specific group (Task 9) -----------------------------
  // Defaults match common/ttcut.cpp lines 151-166 (Encoder) and 206 (Muxer).
  int     mMpeg2Preset  = 4;     // fast
  int     mMpeg2Crf     = 2;     // qscale for MPEG-2 (2-31, lower=better)
  int     mMpeg2Profile = 0;     // Main Profile
  int     mMpeg2Muxer   = 0;     // mplex (TS/PS)
  int     mH264Preset   = 4;     // fast
  int     mH264Crf      = 18;    // CRF 18 (high quality for cut points)
  int     mH264Profile  = 2;     // high profile
  int     mH264Muxer    = 1;     // mkvmerge (MKV)
  int     mH265Preset   = 4;     // fast
  int     mH265Crf      = 20;    // CRF 20 (high quality for cut points)
  int     mH265Profile  = 0;     // main profile
  int     mH265Muxer    = 1;     // mkvmerge (MKV)

  // /Settings/Muxer group — Task 12 will add the rest.
  int     mMpeg2Target  = 7;

  // ----- Audio/QuickJump/Screenshot group (Task 10) -----------------------
  // Defaults match common/ttcut.cpp lines 172-181 verbatim.
  // burstThresholdDb/normalizeAcmod/audioLanguagePreference/
  // quickJumpIntervalSec extend /Settings/Common (Task 4).
  // screenshotDir/screenshotProject open a NEW /Settings/Screenshot block
  // — they are newly persisted, were non-persistent statics before per the
  // Task 1 inventory recommendation.
  int         mBurstThresholdDb     = -30;
  bool        mNormalizeAcmod       = true;
  QStringList mAudioLanguagePreference;     // empty = use system locale
  int         mQuickJumpIntervalSec = 30;
  QString     mScreenshotDir;               // empty by default
  QString     mScreenshotProject;           // empty by default

  // ----- Detection Thresholds group (Task 11) ------------------------------
  // Defaults match common/ttcut.cpp lines 184-199 verbatim.
  // navBlackThreshold/SceneThreshold/LogoThreshold extend /Settings/Navigation
  // (Tasks 4-5). The seven sp* fields share /Settings/StreamPoints. The two
  // extraFrame* fields share /Settings/Common (legacy keys without `Sec`
  // suffix — see public-section comment for detail).
  float mNavBlackThreshold        = 0.980f;
  float mNavSceneThreshold        = 0.300f;
  float mNavLogoThreshold         = 0.500f;
  bool  mSpDetectSilence          = true;
  int   mSpSilenceThresholdDb     = -75;
  float mSpSilenceMinDuration     = 0.3f;
  bool  mSpDetectAudioChange      = true;
  bool  mSpDetectAspectChange     = true;
  bool  mSpDetectPillarbox        = true;
  int   mSpPillarboxThreshold     = 20;
  int   mExtraFrameClusterGapSec    = 5;
  int   mExtraFrameClusterOffsetSec = 2;

  // ----- Muxer group (Task 12) ---------------------------------------------
  // Defaults match common/ttcut.cpp lines 205-221 verbatim.
  // mMuxOutputPath is initialised to QDir::homePath() in the ctor (matches
  // the runtime-dependent init pattern of mTempDirPath/mLastDirPath).
  // mMpeg2Target (Task 9) already lives in /Settings/Muxer above.
  int     mMuxMode             = 0;
  QString mMuxProg             = "mplex";
  QString mMuxProgPath         = "/usr/local/bin/";
  QString mMuxProgCmd          = "-f 8";
  QString mMuxOutputPath;          // initialised to QDir::homePath() in ctor
  bool    mMuxDeleteES         = false;
  bool    mMuxPause            = true;
  int     mOutputContainer     = 1;   // 1=MKV (default for modern codecs)
  bool    mMkvCreateChapters   = true;
  int     mMkvChapterInterval  = 5;
  int     mAudioOnlyFormat;        // initialised to TTCut::AOF_OriginalES in ctor
  int     mAudioOnlyBitrateKbps = 0;  // 0 = match source bitrate

  // ----- Cut Settings & Chapter group (Task 13) ----------------------------
  // Defaults match common/ttcut.cpp lines 227 + 233-242 verbatim. mCutDirPath
  // is initialised to QDir::currentPath() in the ctor (matches the
  // runtime-dependent init pattern of mTempDirPath/mLastDirPath).
  // mMuxFileName is a de-facto constant ("muxscript.sh"); mCutVideoName is
  // per-project (persisted in .ttcut, not QSettings) — both have setters but
  // no QSettings load/save round-trip.
  bool    mCutAddSuffix         = true;
  bool    mCutWriteMaxBitrate   = false;
  bool    mCutWriteSeqEnd       = false;
  bool    mCorrectCutTimeCode   = false;
  bool    mCorrectCutBitRate    = false;
  bool    mCreateCutIDD         = false;
  bool    mReadCutIDD           = false;
  bool    mSpumuxChapter        = false;
  QString mMuxFileName          = "muxscript.sh";
  QString mCutVideoName;           // empty by default
  QString mCutDirPath;             // initialised to QDir::currentPath() in ctor
};

#endif
