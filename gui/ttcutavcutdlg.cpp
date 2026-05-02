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

#include <sys/statvfs.h>
#include <math.h>
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QIcon>
#include <QStyle>

/* /////////////////////////////////////////////////////////////////////////////
 * Constructor
 */
TTCutAVCutDlg::TTCutAVCutDlg(QWidget* parent, bool audioOnly)
    : QDialog(parent)
{
  setupUi(this);

  // Audio-only mode: hide encoder/muxer tabs (irrelevant for audio extraction)
  // and show the audio-format selector. Default values come from QSettings.
  gbAudioOnly->setVisible(audioOnly);
  if (audioOnly) {
    TTCut::populateAudioOnlyFormatCombo(cbAudioOnlyFormat);
    sbAudioOnlyBitrate->setValue(TTCut::audioOnlyBitrateKbps);
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
  connect(btnDirOpen,   SIGNAL(clicked()),           SLOT(onDirectoryOpen()));
  connect(okButton,     SIGNAL(clicked()),           SLOT( onDlgStart()));
  connect(cancelButton, SIGNAL(clicked()),           SLOT( onDlgCancel()));

  // set the tabs data
  // ------------------------------------------------------------------
  setCommonData();
  encodingPage->setTabData();
  muxingPage->setTabData();

  // React to encoder codec changes (disable MPG row for H.264/H.265,
  // update muxer visibility).
  connect(encodingPage, SIGNAL(codecChanged(int)),
          muxingPage,   SLOT(onEncoderCodecChanged(int)));

  // Live filename updates: suffix toggle, codec change, container change.
  // Wired before the initial onEncoderCodecChanged() sync below so that
  // the very first container change (if any) propagates to the filename.
  connect(cbAddSuffix,  SIGNAL(toggled(bool)),           SLOT(updateOutputFilename()));
  connect(encodingPage, SIGNAL(codecChanged(int)),       SLOT(updateOutputFilename()));
  connect(muxingPage,   SIGNAL(containerChanged(int)),   SLOT(updateOutputFilename()));

  // Initial sync based on current codec.
  muxingPage->onEncoderCodecChanged(TTCut::encoderCodec);
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
  muxingPage->getTabData();

  if (gbAudioOnly->isVisible()) {
    TTCut::audioOnlyFormat       = cbAudioOnlyFormat->currentData().toInt();
    TTCut::audioOnlyBitrateKbps  = sbAudioOnlyBitrate->value();
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
      TTCut::cutDirPath,
      (QFileDialog::DontResolveSymlinks |
       QFileDialog::ShowDirsOnly) );

  if ( !str_dir.isEmpty() )
  {
    TTCut::cutDirPath    = str_dir;
    TTCut::muxOutputPath = str_dir;
    leOutputPath->setText( TTCut::cutDirPath );
    qApp->processEvents();
  }

  getFreeDiskSpace();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Set the tab data from global parameter
 */
void TTCutAVCutDlg::setCommonData()
{
  if ( !QDir(TTCut::cutDirPath).exists() )
    TTCut::cutDirPath = QDir::currentPath();

  // cut output filename and output path
  leOutputFile->setText( TTCut::cutVideoName );
  leOutputPath->setText( TTCut::cutDirPath );

  // add "_cut" suffix option
  cbAddSuffix->setChecked(TTCut::cutAddSuffix);

  // cut options
  // write max bittrate tp first sequence
  cbMaxBitrate->setChecked(TTCut::cutWriteMaxBitrate);

  // write sequence end code
  cbEndCode->setChecked(TTCut::cutWriteSeqEnd);

  // Populate the field with suffix + extension from current state.
  updateOutputFilename();

  getFreeDiskSpace();
 }

/* /////////////////////////////////////////////////////////////////////////////
 * Get tab data and set global parameter
 */
void TTCutAVCutDlg::getCommonData()
{
  QString displayName  = leOutputFile->text();
  TTCut::cutDirPath    = leOutputPath->text();
  TTCut::cutAddSuffix  = cbAddSuffix->isChecked();

  if (!QDir(TTCut::cutDirPath).exists())
    TTCut::cutDirPath = QDir::currentPath();

  // The UI field holds the final container extension (.mkv or .mpg).
  // The downstream cut pipeline uses TTCut::cutVideoName as the path
  // for the *intermediate* elementary-stream file, which needs a
  // codec-specific ES extension (.m2v / .h264 / .h265). Strip the UI
  // container extension here and re-attach the ES one.
  QFileInfo fi(displayName);
  QString base = fi.completeBaseName();
  TTCut::cutVideoName = base + "." + expectedEsExtension(TTCut::outputContainer,
                                                         TTCut::encoderCodec);

  TTCut::cutWriteMaxBitrate = cbMaxBitrate->isChecked();
  TTCut::cutWriteSeqEnd     = cbEndCode->isChecked();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Calculate the available diskspace
 */
void TTCutAVCutDlg::getFreeDiskSpace()
{
	DfInfo rootFsInfo    = getDiskSpaceInfo("/");
	DfInfo cutPathFsInfo = getDiskSpaceInfo(TTCut::cutDirPath);

	laPath1->setText(rootFsInfo.path);
	laSize1->setText(QString("%1G").arg(rootFsInfo.size, 0, 'f', 0));
	laUsed1->setText(QString("%1G").arg(rootFsInfo.used, 0, 'f', 0));
	laFree1->setText(QString("%1G").arg(rootFsInfo.free, 0, 'f', 0));
	laUsedPercent1->setText(QString("%1%").arg(rootFsInfo.percentUsed, 0, 'f', 0));

	laPath2->setText(cutPathFsInfo.path);
	laSize2->setText(QString("%1G").arg(cutPathFsInfo.size, 0, 'f', 0));
	laUsed2->setText(QString("%1G").arg(cutPathFsInfo.used, 0, 'f', 0));
	laFree2->setText(QString("%1G").arg(cutPathFsInfo.free, 0, 'f', 0));
	laUsedPercent2->setText(QString("%1%").arg(cutPathFsInfo.percentUsed, 0, 'f', 0));
}

DfInfo TTCutAVCutDlg::getDiskSpaceInfo(QString path)
{
	struct statvfs fsInfo;
	DfInfo dfInfo;

	dfInfo.path        = path;
	dfInfo.size        = 0.0;
	dfInfo.free        = 0.0;
	dfInfo.used        = 0.0;
	dfInfo.percentUsed = 0.0;

	if (statvfs(path.toLocal8Bit().constData(), &fsInfo) == -1) {
		QString msg = QString("could not stat free disk space for %1!").arg(path);
		log->errorMsg(__FILE__, __LINE__, msg);
		return dfInfo;
	}

	double kBlockSize   = 1024.0 / fsInfo.f_frsize;
	double kSpace       = fsInfo.f_blocks / kBlockSize / 1024.0 / 1024.0;
	double kFreeNonRoot = fsInfo.f_bavail / kBlockSize / 1024.0 / 1024.0;
	double kFreeTotal   = fsInfo.f_bfree  / kBlockSize / 1024.0 / 1024.0;
	double kUsed        = kSpace - kFreeTotal;
	double percentUsed  = round(kUsed / kSpace * 100.0);

	dfInfo.size  = kSpace;
	dfInfo.free  = kFreeNonRoot;
	dfInfo.used  = kUsed;
	dfInfo.percentUsed = percentUsed;

	return dfInfo;
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

  QString ext = expectedContainerExtension(TTCut::outputContainer);
  QString newText = base + "." + ext;

  // Idempotency guard: setText() emits textChanged unconditionally.
  // If anything connects to that signal in the future, this prevents
  // re-entrant loops.
  if (leOutputFile->text() != newText) {
    leOutputFile->setText(newText);
  }
}
