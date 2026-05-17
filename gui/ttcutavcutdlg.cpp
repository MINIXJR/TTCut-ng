/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2005                                                      */
/* FILE     : ttcutavcutdlg.cpp                                               */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 04/01/2005 */
/* MODIFIED: b. altendorf                                    DATE: 03/05/2006 */
/* MODIFIED: b. altendorf                                    DATE: 04/24/2007 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTCUTAVCUTDLG
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*                                                                            */
/* This program is distributed in the hope that it will be useful, but WITHOUT*/
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.                                          */
/* See the GNU General Public License for more details.                       */
/*                                                                            */
/* You should have received a copy of the GNU General Public License along    */
/* with this program; if not, write to the Free Software Foundation,          */
/* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.              */
/*----------------------------------------------------------------------------*/


#include "ttcutavcutdlg.h"

#include "../common/ttsettings.h"

#include <sys/statvfs.h>
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QIcon>
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
  connect(btnDirOpen,   &QPushButton::clicked, this, &TTCutAVCutDlg::onDirectoryOpen);
  connect(okButton,     &QPushButton::clicked, this, &TTCutAVCutDlg::onDlgStart);
  connect(cancelButton, &QPushButton::clicked, this, &TTCutAVCutDlg::onDlgCancel);

  // Populate container and mux-target combos
  populateMuxerProg();
  populateMuxTarget();

  // rbCreateMuxScript / rbMuxStreams
  int muxMode = TTSettings::instance()->muxMode();
  rbCreateMuxScript->setChecked(muxMode == 1);
  rbMuxStreams->setChecked(muxMode == 0);
  connect(rbCreateMuxScript, &QRadioButton::clicked, this, [](bool c) {
    if (c) TTSettings::instance()->setMuxMode(1);
  });
  connect(rbMuxStreams, &QRadioButton::clicked, this, [](bool c) {
    if (c) TTSettings::instance()->setMuxMode(0);
  });

  // gbMuxOptions: MKV chapters + delete ES
  cbMkvCreateChapters->setChecked(TTSettings::instance()->mkvCreateChapters());
  sbMkvChapterInterval->setValue(TTSettings::instance()->mkvChapterInterval());
  connect(cbMkvCreateChapters, &QCheckBox::toggled, this, [](bool c) {
    TTSettings::instance()->setMkvCreateChapters(c);
  });
  connect(sbMkvChapterInterval, qOverload<int>(&QSpinBox::valueChanged), this, [](int v) {
    TTSettings::instance()->setMkvChapterInterval(v);
  });

  cbDeleteES->setChecked(TTSettings::instance()->muxDeleteES());
  connect(cbDeleteES, &QCheckBox::toggled, this, [](bool c) {
    TTSettings::instance()->setMuxDeleteES(c);
  });

  // cbMuxTarget persistence
  connect(cbMuxTarget, qOverload<int>(&QComboBox::currentIndexChanged), this, [](int idx) {
    TTSettings::instance()->setMpeg2Target(idx);
  });

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

  if (gbAudioOnly->isVisible()) {
    TTSettings::instance()->setAudioOnlyFormat(cbAudioOnlyFormat->currentData().toInt());
  }
}


/* /////////////////////////////////////////////////////////////////////////////
 * Exit, saving changes; start A/V cut
 */
void TTCutAVCutDlg::onDlgStart()
{
  setGlobalData();

  done( 0 );
}


/* /////////////////////////////////////////////////////////////////////////////
 * Exit, discard changes
 */
void TTCutAVCutDlg::onDlgCancel()
{
  done( 1 );
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
  // container extension here and re-attach the ES one.
  QFileInfo fi(displayName);
  QString base = fi.completeBaseName();
  TTSettings::instance()->setCutVideoName(base + "." + expectedEsExtension(TTSettings::instance()->outputContainer(),
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
  int idx = cbMuxerProg->findData(TTSettings::instance()->outputContainer());
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
  cbMuxTarget->setCurrentIndex(TTSettings::instance()->mpeg2Target());
}

/* /////////////////////////////////////////////////////////////////////////////
 * Slot: cbMuxerProg selection changed
 */
void TTCutAVCutDlg::onMuxerProgChanged(int /*index*/)
{
  int value = cbMuxerProg->currentData().toInt();
  TTSettings::instance()->setOutputContainer(value);
  // Also write per-codec preference so it persists across sessions
  switch (TTSettings::instance()->encoderCodec()) {
    case 0: TTSettings::instance()->setMpeg2Muxer(value); break;
    case 1: TTSettings::instance()->setH264Muxer(value);  break;
    case 2: TTSettings::instance()->setH265Muxer(value);  break;
    default: break;
  }
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

  // Fall back to MKV if current container is MPG but codec doesn't support it
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

  QString ext = expectedContainerExtension(TTSettings::instance()->outputContainer());
  QString newText = base + "." + ext;

  // Idempotency guard: setText() emits textChanged unconditionally.
  if (leOutputFile->text() != newText) {
    leOutputFile->setText(newText);
  }
}
