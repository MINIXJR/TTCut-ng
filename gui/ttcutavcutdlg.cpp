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
TTCutAVCutDlg::TTCutAVCutDlg(QWidget* parent)
    : QDialog(parent)
{
  setupUi(this);

  // not implemented yet
  tabWidget->removeTab(3);

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
  //chaptersPage->setTabData();
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
  //chaptersPage->getTabData();
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

  getFreeDiskSpace();
 }

/* /////////////////////////////////////////////////////////////////////////////
 * Get tab data and set global parameter
 */
void TTCutAVCutDlg::getCommonData()
{
  // cut output filename and output path
  TTCut::cutVideoName  = leOutputFile->text();
  TTCut::cutDirPath    = leOutputPath->text();
  TTCut::cutAddSuffix  = cbAddSuffix->isChecked();

  if ( !QDir(TTCut::cutDirPath).exists() )
    TTCut::cutDirPath    = QDir::currentPath();

  // Check for video file extension based on codec and muxer selection
  QFileInfo cutFile(TTCut::cutVideoName);
  QString ext = cutFile.suffix().toLower();

  // Determine appropriate extension based on output container and codec
  // TTCut::outputContainer: 0=mplex, 1=mkvmerge, 2=ffmpeg
  // TTCut::encoderCodec: 0=MPEG-2, 1=H.264, 2=H.265
  QString expectedExt;

  if (TTCut::outputContainer == 1) {
    // MKV output - extension is .mkv, video will be .h264/.h265/.m2v
    if (TTCut::encoderCodec == 1) {
      expectedExt = "h264";
    } else if (TTCut::encoderCodec == 2) {
      expectedExt = "h265";
    } else {
      expectedExt = "m2v";
    }
  } else if (TTCut::outputContainer == 2) {
    // MP4/TS output via ffmpeg - extension is based on codec
    if (TTCut::encoderCodec == 1 || TTCut::encoderCodec == 2) {
      expectedExt = "ts";  // TS container for H.264/H.265
    } else {
      expectedExt = "m2v";
    }
  } else {
    // mplex (MPEG-2 only) - always .m2v
    expectedExt = "m2v";
  }

  // Add extension if missing or different
  if (ext.isEmpty() || (ext != expectedExt && ext != "m2v" && ext != "h264" && ext != "h265" && ext != "ts")) {
    TTCut::cutVideoName += "." + expectedExt;
  }

  // cut options
  // write max bittrate tp first sequence
  TTCut::cutWriteMaxBitrate = cbMaxBitrate->isChecked();

  // write sequence end code
  TTCut::cutWriteSeqEnd = cbEndCode->isChecked();
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

	if (statvfs(toAscii(path), &fsInfo) == -1) {
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



