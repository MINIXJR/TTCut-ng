/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttcutpreview.cpp                                                */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 03/13/2005 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTCUTPREVIEW
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

#include "../common/ttexception.h"
#include "ttcutpreview.h"
#include "../avstream/ttavstream.h"
#include "../data/ttavdata.h"
#include "../data/ttavlist.h"
#include "../data/ttcutpreviewtask.h"
#include "ttmplayerwidget.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QIcon>
#include <QStyle>

/* /////////////////////////////////////////////////////////////////////////////
 * TTCutPreview constructor
 */
TTCutPreview::TTCutPreview(QWidget* parent, int prevW, int prevH)
  : QDialog(parent)
{
  setupUi(this);

  videoPlayer = new TTMplayerWidget(videoFrame);

  setObjectName("TTCutPreview");

  // set desired video width x height
  previewWidth  = prevW;
  previewHeight = prevH;

  cbCutPreview->setEditable( false );
  cbCutPreview->setMinimumSize( 160, 20 );
  cbCutPreview->setInsertPolicy( QComboBox::InsertAfterCurrent );

  // Use theme icons with Qt standard icon fallback for cross-platform support
  QStyle* style = QApplication::style();
  pbPlay->setIcon(QIcon::fromTheme("media-playback-start", style->standardIcon(QStyle::SP_MediaPlay)));
  pbExit->setIcon(QIcon::fromTheme("window-close", style->standardIcon(QStyle::SP_DialogCloseButton)));
  pbPrevCut->setIcon(QIcon::fromTheme("go-previous", style->standardIcon(QStyle::SP_ArrowBack)));
  pbNextCut->setIcon(QIcon::fromTheme("go-next", style->standardIcon(QStyle::SP_ArrowForward)));

  connect(videoPlayer,  SIGNAL(optimalSizeChanged()),     SLOT(onOptimalSizeChanged()));
  connect(videoPlayer,  SIGNAL(playerPlaying()),          SLOT(onPlayerPlaying()));
  connect(videoPlayer,  SIGNAL(playerFinished()),         SLOT(onPlayerFinished()));
  connect(cbCutPreview, SIGNAL(currentIndexChanged(int)), SLOT(onCutSelectionChanged(int)));
  connect(pbPlay,       SIGNAL(clicked()),                SLOT(onPlayPreview()));
  connect(pbExit,       SIGNAL(clicked()),                SLOT(onExitPreview()));
  connect(pbPrevCut,    SIGNAL(clicked()),                SLOT(onPrevCut()));
  connect(pbNextCut,    SIGNAL(clicked()),                SLOT(onNextCut()));
}

/* /////////////////////////////////////////////////////////////////////////////
 * Destroys the object and frees any allocated resources
 */
TTCutPreview::~TTCutPreview()
{
}

/* /////////////////////////////////////////////////////////////////////////////
 * resizeEvent
 */
void TTCutPreview::resizeEvent(QResizeEvent*)
{
	videoPlayer->resize(videoFrame->width()-2, videoFrame->height()-2);
}

void TTCutPreview::onOptimalSizeChanged()
{
}

/* /////////////////////////////////////////////////////////////////////////////
 * Event handler to receive widgets close events
 */
void TTCutPreview::closeEvent(QCloseEvent* event)
{
  cleanUp();
  event->accept();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Initialize preview parameter
 */
void TTCutPreview::initPreview(TTCutList* cutList)
{
  int       iPos;
  QString   preview_video_name;
  QFileInfo preview_video_info;
  QString   selectionString;

  int numPreview = cutList->count()/2+1;

  // Create video and audio preview clips
  for (int i = 0; i < numPreview; i++ ) {
    // first cut-in
    if (i == 0) {
      TTCutItem item = cutList->at(i);
      selectionString = QString("Start: %1").arg(item.cutInTime().toString("hh:mm:ss"));
      cbCutPreview->addItem( selectionString );
    }

    // cut i-i
    if (numPreview > 1 && i > 0 && i < numPreview-1) {
      iPos = (i-1)*2+1;

      TTCutItem item1 = cutList->at(iPos);
      TTCutItem item2 = cutList->at(iPos+1);
      selectionString = QString("Cut %1-%2: %3 - %4")
            .arg(i).arg(i+1)
            .arg(item1.cutInTime().toString("hh:mm:ss"))
            .arg(item2.cutOutTime().toString("hh:mm:ss"));
      cbCutPreview->addItem( selectionString );
    }

    //last cut out
    if (i == numPreview-1) {
      iPos = (i-1)*2+1;

      TTCutItem item = cutList->at(iPos);
      selectionString = QString("End: %1").arg(item.cutOutTime().toString("hh:mm:ss"));
      cbCutPreview->addItem( selectionString );
    }
  }

  // set the current cut preview to the first cut clip
  // Check for H.264/H.265 (.mkv) or MPEG-2 (.mpg) preview files
  // Try .mkv first (mkvmerge output for H.264/H.265)
  preview_video_name = "preview_001.mkv";
  preview_video_info.setFile(QDir(TTCut::tempDirPath), preview_video_name);
  if (!preview_video_info.exists()) {
    // Fallback to .mpg for MPEG-2
    preview_video_name = "preview_001.mpg";
    preview_video_info.setFile(QDir(TTCut::tempDirPath), preview_video_name);
  }

  current_video_file = preview_video_info.absoluteFilePath();
  onCutSelectionChanged(0);
}

/* /////////////////////////////////////////////////////////////////////////////
 * ComboBox selectionChanged event handler: load the selected movie
 */
void TTCutPreview::onCutSelectionChanged( int iCut )
{
  QString   preview_video_name;
  QString   preview_subtitle_name;
  QFileInfo preview_video_info;
  QFileInfo preview_subtitle_info;

  // Try .mkv first (H.264/H.265 via mkvmerge), then .mpg (MPEG-2 via mplex)
  preview_video_name = QString("preview_%1.mkv").arg(iCut+1, 3, 10, QChar('0'));
  preview_video_info.setFile( QDir(TTCut::tempDirPath), preview_video_name );
  if (!preview_video_info.exists()) {
    preview_video_name = QString("preview_%1.mpg").arg(iCut+1, 3, 10, QChar('0'));
    preview_video_info.setFile( QDir(TTCut::tempDirPath), preview_video_name );
  }
  current_video_file = preview_video_info.absoluteFilePath();

  // Check for subtitle file
  preview_subtitle_name = QString("preview_%1.srt").arg(iCut+1, 3, 10, QChar('0'));
  preview_subtitle_info.setFile( QDir(TTCut::tempDirPath), preview_subtitle_name );
  if (preview_subtitle_info.exists()) {
    videoPlayer->setSubtitleFile(preview_subtitle_info.absoluteFilePath());
  } else {
    videoPlayer->clearSubtitleFile();
  }

  qDebug("load preview %s", qPrintable(current_video_file));
  videoPlayer->load(current_video_file);
  pbPlay->setText(tr("Play"));
  pbPlay->setIcon(QIcon::fromTheme("media-playback-start", QApplication::style()->standardIcon(QStyle::SP_MediaPlay)));

  // Update prev/next button states
  pbPrevCut->setEnabled(iCut > 0);
  pbNextCut->setEnabled(iCut < cbCutPreview->count() - 1);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Play/Pause the selected preview clip
 */
void TTCutPreview::onPlayPreview()
{
  if (videoPlayer->isPlaying()) {
    videoPlayer->stop();
    pbPlay->setText(tr("Play"));
    pbPlay->setIcon(QIcon::fromTheme("media-playback-start", QApplication::style()->standardIcon(QStyle::SP_MediaPlay)));
    return;
  }

  pbPlay->setText(tr("Stop"));
  pbPlay->setIcon(QIcon::fromTheme("media-playback-stop", QApplication::style()->standardIcon(QStyle::SP_MediaStop)));
  videoPlayer->play();
}

void TTCutPreview::onPlayerPlaying()
{
  onPlayPreview();
}

void TTCutPreview::onPlayerFinished()
{
  videoPlayer->load(current_video_file);
  pbPlay->setText(tr("Play"));
  pbPlay->setIcon(QIcon::fromTheme("media-playback-start", QApplication::style()->standardIcon(QStyle::SP_MediaPlay)));
}

/* /////////////////////////////////////////////////////////////////////////////
 * Exit the preview window
 */
void TTCutPreview::onExitPreview()
{
  close();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Go to previous cut in the list and play
 */
void TTCutPreview::onPrevCut()
{
  int currentIndex = cbCutPreview->currentIndex();
  if (currentIndex > 0) {
    cbCutPreview->setCurrentIndex(currentIndex - 1);
    // Auto-play after selection change
    if (!videoPlayer->isPlaying()) {
      videoPlayer->play();
      pbPlay->setText(tr("Stop"));
      pbPlay->setIcon(QIcon::fromTheme("media-playback-stop", QApplication::style()->standardIcon(QStyle::SP_MediaStop)));
    }
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Go to next cut in the list and play
 */
void TTCutPreview::onNextCut()
{
  int currentIndex = cbCutPreview->currentIndex();
  if (currentIndex < cbCutPreview->count() - 1) {
    cbCutPreview->setCurrentIndex(currentIndex + 1);
    // Auto-play after selection change
    if (!videoPlayer->isPlaying()) {
      videoPlayer->play();
      pbPlay->setText(tr("Stop"));
      pbPlay->setIcon(QIcon::fromTheme("media-playback-stop", QApplication::style()->standardIcon(QStyle::SP_MediaStop)));
    }
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Housekeeping: Remove the temporary created preview clips
 */
void TTCutPreview::cleanUp()
{
  videoPlayer->cleanUp();

  // DEBUG: Temporarily skip cleanup so user can test preview files with VLC
  // Preview files are in TTCut::tempDirPath (e.g., preview_001.mkv, preview_001.mpg)
  qDebug() << "DEBUG: Skipping preview file cleanup. Files in:" << TTCut::tempDirPath;
  return;

  // Clean up all preview* files in temp directory
  QDir tempDir(TTCut::tempDirPath);
  QStringList filters;
  filters << "preview*";
  QFileInfoList previewFiles = tempDir.entryInfoList(filters, QDir::Files);
  for (const QFileInfo& fi : previewFiles) {
    QFile::remove(fi.absoluteFilePath());
  }
}
