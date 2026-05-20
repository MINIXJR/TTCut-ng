/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTCUTAVCUTDLG
// ----------------------------------------------------------------------------


#include "ttcutavcutdlg.h"

#include "../common/ttsettings.h"

#include <sys/statvfs.h>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QIcon>
#include <QMessageBox>
#include <QStandardItemModel>
#include <QStyle>

/* /////////////////////////////////////////////////////////////////////////////
 * Constructor
 */
TTCutAVCutDlg::TTCutAVCutDlg(QWidget* parent, bool audioOnly)
    : QDialog(parent)
{
  setupUi(this);

  // Audio-only mode: show the audio-format selector.
  gbAudioOnly->setVisible(audioOnly);
  if (audioOnly) {
    TTCut::populateAudioOnlyFormatCombo(cbAudioOnlyFormat);
    setWindowTitle(tr("Audio Cut Options"));
  }

  // message logger instance
  log = TTMessageLogger::getInstance();

  // Use theme icons with Qt standard icon fallback for cross-platform support
  QStyle* style = QApplication::style();
  btnDirOpen->setIcon(QIcon::fromTheme("folder-open", style->standardIcon(QStyle::SP_DirOpenIcon)));
  okButton->setIcon(QIcon::fromTheme("dialog-ok", style->standardIcon(QStyle::SP_DialogOkButton)));
  cancelButton->setIcon(QIcon::fromTheme("dialog-cancel", style->standardIcon(QStyle::SP_DialogCancelButton)));

  // signals and slot connection
  // ------------------------------------------------------------------
  connect(btnDirOpen,       &QPushButton::clicked, this, &TTCutAVCutDlg::onDirectoryOpen);
  connect(okButton,         &QPushButton::clicked, this, &TTCutAVCutDlg::onDlgStart);
  connect(cancelButton,     &QPushButton::clicked, this, &TTCutAVCutDlg::onDlgCancel);
  connect(btnResetDefaults, &QPushButton::clicked, this, &TTCutAVCutDlg::onResetDefaults);

  // Populate container and mux-target combos
  populateMuxerProg();
  populateMuxTarget();

  // rbCreateMuxScript / rbMuxStreams — load UI from working set (transient,
  // initialised from App-Default by load() and overwritten by .ttcut on
  // project load). Persisting happens in setGlobalData() on OK.
  int muxMode = TTSettings::instance()->workingMuxMode();
  rbCreateMuxScript->setChecked(muxMode == 1);
  rbMuxStreams->setChecked(muxMode == 0);

  // gbMuxOptions: MKV chapters + delete ES — load from working set.
  cbMkvCreateChapters->setChecked(TTSettings::instance()->workingMkvCreateChapters());
  sbMkvChapterInterval->setValue(TTSettings::instance()->workingMkvChapterInterval());
  cbDeleteES->setChecked(TTSettings::instance()->workingMuxDeleteES());

  // cbMuxTarget — load from working set.

  // Connect encoder codec changes to cut-dialog visibility logic
  connect(encodingPage, &TTCutSettingsEncoder::codecChanged, this,
          &TTCutAVCutDlg::onCodecChangedForVisibility);
  connect(cbMuxerProg, qOverload<int>(&QComboBox::currentIndexChanged), this,
          &TTCutAVCutDlg::onMuxerProgChanged);

  // Live filename updates: suffix toggle and codec change.
  connect(cbAddSuffix,  &QCheckBox::toggled,                 this, &TTCutAVCutDlg::updateOutputFilename);
  connect(encodingPage, &TTCutSettingsEncoder::codecChanged, this, &TTCutAVCutDlg::updateOutputFilename);

  // Set encoder to Override-mode (codec selector disabled, preview-preset hidden)
  encodingPage->setMode(TTCutSettingsEncoder::Override);

  // set the tabs data
  // ------------------------------------------------------------------
  setCommonData();
  encodingPage->setTabData();

  // Initial: codec-dependent visibility
  onCodecChangedForVisibility(TTSettings::instance()->encoderCodec());

  // Free-space footer (one-shot)
  updateFreeSpaceLine();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Destructor
 */
TTCutAVCutDlg::~TTCutAVCutDlg()
{
  // nothing to do
}


/* /////////////////////////////////////////////////////////////////////////////
 * Save the tabs data
 */
void TTCutAVCutDlg::setGlobalData()
{
  getCommonData();
  encodingPage->getTabData();

  TTSettings* s = TTSettings::instance();

  // Mux/Audio options write to the WORKING set (transient, per-cut and
  // per-project). The persistent App-Defaults are only updated via the
  // Settings dialog — Cut-Dialog overrides leave them untouched, so a user's
  // dialog defaults stay sacrosanct across cuts and project loads.
  s->setWorkingMuxMode(rbCreateMuxScript->isChecked() ? 1 : 0);
  s->setWorkingMkvCreateChapters(cbMkvCreateChapters->isChecked());
  s->setWorkingMkvChapterInterval(sbMkvChapterInterval->value());
  s->setWorkingMuxDeleteES(cbDeleteES->isChecked());
  s->setWorkingMpeg2Target(cbMuxTarget->currentIndex());

  // Output container: working set + per-codec sticky App-Default. The
  // per-codec sticky (mpeg2Muxer/h264Muxer/h265Muxer) is App-level — a
  // deliberate cross-cut preference (e.g. 'H.264 → MKV') — so it lives in
  // the persistent App-Defaults and IS updated on OK.
  int container = cbMuxerProg->currentData().toInt();
  s->setWorkingOutputContainer(container);
  switch (s->encoderCodec()) {
    case 0: s->setMpeg2Muxer(container); break;
    case 1: s->setH264Muxer(container);  break;
    case 2: s->setH265Muxer(container);  break;
    default: break;
  }

  if (gbAudioOnly->isVisible()) {
    s->setWorkingAudioOnlyFormat(cbAudioOnlyFormat->currentData().toInt());
  }
}


/* /////////////////////////////////////////////////////////////////////////////
 * Exit, saving changes; start A/V cut
 */
void TTCutAVCutDlg::onResetDefaults()
{
  // Reset the working set to mirror the App-Defaults from the Settings
  // dialog. This is the same sync that TTSettings::load() and
  // setEncoderCodec() perform — we replay it here on user request so an
  // override that drifted (Cut-Dialog edits, .ttcut project values) can be
  // wiped without a project reload.
  TTSettings* s = TTSettings::instance();

  // Encoder transient: codec-specific App-Default.
  switch (s->encoderCodec()) {
    case 0:
      s->setEncoderCrf(s->mpeg2Crf());
      break;
    case 1:
      s->setEncoderPreset(s->h264Preset());
      s->setEncoderCrf(s->h264Crf());
      s->setEncoderProfile(s->h264Profile());
      break;
    case 2:
      s->setEncoderPreset(s->h265Preset());
      s->setEncoderCrf(s->h265Crf());
      s->setEncoderProfile(s->h265Profile());
      break;
  }

  // Mux/Audio working set: persistent App-Defaults.
  s->setWorkingMkvCreateChapters(s->mkvCreateChapters());
  s->setWorkingMkvChapterInterval(s->mkvChapterInterval());
  s->setWorkingMuxDeleteES(s->muxDeleteES());
  s->setWorkingMpeg2Target(s->mpeg2Target());
  s->setWorkingMuxMode(s->muxMode());
  s->setWorkingAudioOnlyFormat(s->audioOnlyFormat());
  // Container: codec-specific App-Default sticky.
  switch (s->encoderCodec()) {
    case 0: s->setWorkingOutputContainer(s->mpeg2Muxer()); break;
    case 1: s->setWorkingOutputContainer(s->h264Muxer());  break;
    case 2: s->setWorkingOutputContainer(s->h265Muxer());  break;
    default: s->setWorkingOutputContainer(s->outputContainer()); break;
  }

  // Reload the UI from the freshly reset working set.
  int muxMode = s->workingMuxMode();
  rbCreateMuxScript->setChecked(muxMode == 1);
  rbMuxStreams->setChecked(muxMode == 0);
  cbMkvCreateChapters->setChecked(s->workingMkvCreateChapters());
  sbMkvChapterInterval->setValue(s->workingMkvChapterInterval());
  cbDeleteES->setChecked(s->workingMuxDeleteES());
  cbMuxTarget->setCurrentIndex(s->workingMpeg2Target());
  int containerIdx = cbMuxerProg->findData(s->workingOutputContainer());
  if (containerIdx >= 0) cbMuxerProg->setCurrentIndex(containerIdx);
  if (gbAudioOnly->isVisible()) {
    int aoIdx = cbAudioOnlyFormat->findData(s->workingAudioOnlyFormat());
    if (aoIdx >= 0) cbAudioOnlyFormat->setCurrentIndex(aoIdx);
  }
  encodingPage->setTabData();
  updateMuxerVisibility();
  updateOutputFilename();
}

void TTCutAVCutDlg::onDlgStart()
{
  // Existiert die Ausgabedatei bereits? UI-Pfad + UI-Dateiname mit der
  // sichtbaren Container-Extension (.mkv/.mpg). Falls ja: explizit fragen.
  QString outputFile = QDir(leOutputPath->text()).absoluteFilePath(leOutputFile->text());
  if (QFile::exists(outputFile)) {
    QMessageBox::StandardButton ret = QMessageBox::question(this,
        tr("File exists"),
        tr("The file\n\n  %1\n\nalready exists. Overwrite?").arg(outputFile),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (ret != QMessageBox::Yes) {
      return;  // Dialog bleibt offen, User kann Pfad/Namen anpassen
    }
  }

  setGlobalData();
  done(QDialog::Accepted);
}


/* /////////////////////////////////////////////////////////////////////////////
 * Exit, discard changes
 */
void TTCutAVCutDlg::onDlgCancel()
{
  done(QDialog::Rejected);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Select a directory for the cut result
 */
void TTCutAVCutDlg::onDirectoryOpen()
{
  QString str_dir = QFileDialog::getExistingDirectory( this,
      "Select cut-result directory",
      TTSettings::instance()->cutDirPath(),
      (QFileDialog::DontResolveSymlinks |
       QFileDialog::ShowDirsOnly) );

  if ( !str_dir.isEmpty() )
  {
    TTSettings::instance()->setCutDirPath(str_dir);
    TTSettings::instance()->setMuxOutputPath(str_dir);
    leOutputPath->setText( TTSettings::instance()->cutDirPath() );
    qApp->processEvents();
  }

  updateFreeSpaceLine();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Set the tab data from global parameter
 */
void TTCutAVCutDlg::setCommonData()
{
  if ( !QDir(TTSettings::instance()->cutDirPath()).exists() )
    TTSettings::instance()->setCutDirPath(QDir::currentPath());

  // cut output filename and output path
  leOutputFile->setText( TTSettings::instance()->cutVideoName() );
  leOutputPath->setText( TTSettings::instance()->cutDirPath() );

  // add "_cut" suffix option
  cbAddSuffix->setChecked(TTSettings::instance()->cutAddSuffix());

  // Populate the field with suffix + extension from current state.
  updateOutputFilename();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Get tab data and set global parameter
 */
void TTCutAVCutDlg::getCommonData()
{
  QString displayName  = leOutputFile->text();
  TTSettings::instance()->setCutDirPath(leOutputPath->text());
  TTSettings::instance()->setCutAddSuffix(cbAddSuffix->isChecked());

  if (!QDir(TTSettings::instance()->cutDirPath()).exists())
    TTSettings::instance()->setCutDirPath(QDir::currentPath());

  // The UI field holds the final container extension (.mkv or .mpg).
  // The downstream cut pipeline uses TTSettings::cutVideoName() as the path
  // for the *intermediate* elementary-stream file, which needs a
  // codec-specific ES extension (.m2v / .h264 / .h265). Strip the UI
  // container extension here and re-attach the ES one. Read the container
  // from the live UI (setOutputContainer happens in setGlobalData() below).
  QFileInfo fi(displayName);
  QString base = fi.completeBaseName();
  int container = cbMuxerProg->currentData().toInt();
  TTSettings::instance()->setCutVideoName(base + "." + expectedEsExtension(container,
                                                         TTSettings::instance()->encoderCodec()));
}

/* /////////////////////////////////////////////////////////////////////////////
 * Populate cbMuxerProg combo (MKV/MPG)
 */
void TTCutAVCutDlg::populateMuxerProg()
{
  cbMuxerProg->blockSignals(true);
  cbMuxerProg->clear();
  cbMuxerProg->insertItem(0, "MKV (libav)", 1);
  cbMuxerProg->insertItem(1, "MPG (mplex)", 0);
  // Read workingOutputContainer. TTSettings::setEncoderCodec() initialises
  // this from the per-codec App-Default (mpeg2Muxer/h264Muxer/h265Muxer),
  // and .ttcut project load overwrites it via deserializeSettings(). So
  // the per-codec sticky preference and per-project override both reach
  // the UI through the same working slot.
  int container = TTSettings::instance()->workingOutputContainer();
  int idx = cbMuxerProg->findData(container);
  cbMuxerProg->setCurrentIndex(idx >= 0 ? idx : 0);
  cbMuxerProg->blockSignals(false);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Populate cbMuxTarget combo (MPEG-2 mplex targets)
 */
void TTCutAVCutDlg::populateMuxTarget()
{
  cbMuxTarget->clear();
  cbMuxTarget->insertItem(0, "Generic MPEG1 (f0)");
  cbMuxTarget->insertItem(1, "VCD (f1)");
  cbMuxTarget->insertItem(2, "user-rate VCD (f2)");
  cbMuxTarget->insertItem(3, "Generic MPEG2 (f3)");
  cbMuxTarget->insertItem(4, "SVCD (f4)");
  cbMuxTarget->insertItem(5, "user-rate SVCD (f5)");
  cbMuxTarget->insertItem(6, "VCD Stills (f6)");
  cbMuxTarget->insertItem(7, "DVD with NAV sectors (f8)");
  cbMuxTarget->insertItem(8, "DVD (f9)");
  cbMuxTarget->setCurrentIndex(TTSettings::instance()->workingMpeg2Target());
}

/* /////////////////////////////////////////////////////////////////////////////
 * Slot: cbMuxerProg selection changed
 */
void TTCutAVCutDlg::onMuxerProgChanged(int /*index*/)
{
  // Live UI feedback only — persisting the container (and the per-codec
  // sticky preference) happens in setGlobalData() on OK, so a Cancel/X
  // discard leaves the App-Default unchanged.
  updateMuxerVisibility();
  updateOutputFilename();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Slot: encoder codec changed — update MPG availability + visibility
 */
void TTCutAVCutDlg::onCodecChangedForVisibility(int codecIndex)
{
  // Disable MPG row for H.264/H.265
  bool mpgSupported = (codecIndex == 0);
  QStandardItemModel* model = qobject_cast<QStandardItemModel*>(cbMuxerProg->model());
  if (model) {
    QStandardItem* mpgItem = nullptr;
    for (int i = 0; i < cbMuxerProg->count(); ++i) {
      if (cbMuxerProg->itemData(i).toInt() == 0) {
        mpgItem = model->item(i);
        break;
      }
    }
    if (mpgItem) {
      Qt::ItemFlags f = mpgItem->flags();
      mpgItem->setFlags(mpgSupported ? (f | Qt::ItemIsEnabled) : (f & ~Qt::ItemIsEnabled));
    }
  }

  // Switch container to the codec's per-codec sticky preference (so a
  // user-codec-switch in the dialog re-picks the right preferred container,
  // e.g. MPEG-2 → MPG, H.264 → MKV).
  TTSettings* s = TTSettings::instance();
  int desired;
  switch (codecIndex) {
    case 0:  desired = s->mpeg2Muxer(); break;
    case 1:  desired = s->h264Muxer();  break;
    case 2:  desired = s->h265Muxer();  break;
    default: desired = s->outputContainer(); break;
  }
  // Fall back to MKV if the preferred container is MPG but the codec
  // doesn't support it (H.264/H.265 with MPG is not a valid combination).
  if (!mpgSupported && desired == 0) desired = 1;
  int desiredIdx = cbMuxerProg->findData(desired);
  if (desiredIdx >= 0 && desiredIdx != cbMuxerProg->currentIndex()) {
    cbMuxerProg->setCurrentIndex(desiredIdx);
  }

  // Belt-and-braces: even if the per-codec pref was MKV but UI somehow holds
  // MPG, force it back when the codec doesn't support MPG.
  if (!mpgSupported && cbMuxerProg->currentData().toInt() == 0) {
    int mkvIdx = cbMuxerProg->findData(1);
    if (mkvIdx >= 0) cbMuxerProg->setCurrentIndex(mkvIdx);
  }

  updateMuxerVisibility();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Update muxer-related widget visibility based on current container + codec
 */
void TTCutAVCutDlg::updateMuxerVisibility()
{
  int container = cbMuxerProg->currentData().toInt();
  bool isMpg = (container == 0);
  bool isMkv = (container == 1);

  cbMuxTarget->setVisible(isMpg);
  laMuxTarget->setVisible(isMpg);
  gbMuxMode->setVisible(isMpg);

  cbMkvCreateChapters->setVisible(isMkv);
  sbMkvChapterInterval->setVisible(isMkv);
  laMkvInterval->setVisible(isMkv);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Update footer free-space line (called once at constructor + after path change)
 */
void TTCutAVCutDlg::updateFreeSpaceLine()
{
  auto formatBytes = [](quint64 b) -> QString {
    if (b >= (1ULL<<30)) return QString::number(b / double(1ULL<<30), 'f', 0) + "G";
    if (b >= (1ULL<<20)) return QString::number(b / double(1ULL<<20), 'f', 0) + "M";
    return QString::number(b / double(1ULL<<10), 'f', 0) + "K";
  };

  auto colorFor = [](double pctUsed) -> QString {
    if (pctUsed >= 90.0) return "red";
    if (pctUsed >= 70.0) return "orange";
    return "green";
  };

  auto driveInfo = [&](const QString& path) -> QString {
    struct statvfs sv;
    if (statvfs(path.toLocal8Bit().constData(), &sv) != 0) return QString();
    quint64 total = quint64(sv.f_blocks) * sv.f_frsize;
    quint64 free  = quint64(sv.f_bavail) * sv.f_frsize;
    if (total == 0) return QString();
    double pctUsed = 100.0 * (1.0 - double(free) / double(total));
    return QString("<span style='color:%1'>&#x25CF;</span> %2: %3 frei (%4%)")
             .arg(colorFor(pctUsed))
             .arg(QDir(path).dirName().isEmpty() ? path : QDir(path).dirName())
             .arg(formatBytes(free))
             .arg(int(pctUsed));
  };

  QStringList parts;
  const QString root = "/";
  const QString cutDir = TTSettings::instance()->cutDirPath();
  parts << driveInfo(root);
  if (!cutDir.isEmpty() && cutDir != root) parts << driveInfo(cutDir);
  // Remove empty entries (statvfs failure)
  parts.removeAll(QString());

  laFreeSpace->setText(parts.join(" &nbsp;&middot;&nbsp; "));
}

/* /////////////////////////////////////////////////////////////////////////////
 * Extension that matches the final on-disk container.
 * 0 = MPG (mplex) → mpg
 * 1 = MKV (libav) → mkv
 */
QString TTCutAVCutDlg::expectedContainerExtension(int container)
{
  return (container == 1) ? QStringLiteral("mkv") : QStringLiteral("mpg");
}

/* /////////////////////////////////////////////////////////////////////////////
 * Extension for the intermediate elementary-stream file written by the cut
 * pipeline. Not shown to the user; separate from the container extension
 * because cut video/audio tasks write raw ES that is muxed afterwards.
 *   container 0 (mplex)        → m2v  (MPEG-2 only)
 *   container 1 (MKV), codec 0 → m2v  (MPEG-2)
 *   container 1 (MKV), codec 1 → h264 (H.264)
 *   container 1 (MKV), codec 2 → h265 (H.265)
 */
QString TTCutAVCutDlg::expectedEsExtension(int container, int codec)
{
  if (container == 1) {
    if (codec == 1) return QStringLiteral("h264");
    if (codec == 2) return QStringLiteral("h265");
  }
  return QStringLiteral("m2v");
}

/* /////////////////////////////////////////////////////////////////////////////
 * Rebuild leOutputFile from the current widget state:
 *   basename (± "_cut" suffix) + "." + container extension.
 * A manual base name typed by the user is preserved; only the "_cut" suffix
 * and the extension are adjusted.
 */
void TTCutAVCutDlg::updateOutputFilename()
{
  QFileInfo fi(leOutputFile->text());
  QString base = fi.completeBaseName();

  bool hasSuffix  = base.endsWith(QStringLiteral("_cut"));
  bool wantSuffix = cbAddSuffix->isChecked();
  if      ( wantSuffix && !hasSuffix) base += QStringLiteral("_cut");
  else if (!wantSuffix &&  hasSuffix) base.chop(4);

  // Read container from the live UI value, not from the persistent
  // App-Default — the App-Default is only updated on OK (setGlobalData).
  int container = cbMuxerProg->currentData().toInt();
  QString ext = expectedContainerExtension(container);
  QString newText = base + "." + ext;

  // Idempotency guard: setText() emits textChanged unconditionally.
  if (leOutputFile->text() != newText) {
    leOutputFile->setText(newText);
  }
}
