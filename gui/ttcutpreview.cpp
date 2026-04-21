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
#include "../data/ttcutvideotask.h"
#include "../data/ttcutaudiotask.h"
#include "../extern/ttessmartcut.h"
#include "../extern/ttffmpegwrapper.h"
#include "../extern/ttmkvmergeprovider.h"
#include "../avstream/ttesinfo.h"
#include "../avstream/ttavtypes.h"
#include "../common/ttthreadtaskpool.h"

extern "C" {
#include <libavcodec/codec_id.h>
}
#include "ttmplayerwidget.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QHBoxLayout>
#include <QIcon>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStyle>

#include "../extern/ttffmpegwrapper.h"
#include "../extern/ttessmartcut.h"
#include "../extern/ttmkvmergeprovider.h"
#include "../avstream/ttesinfo.h"

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

  // Burst warning widgets — inserted into existing controls layout
  lblBurstWarning = new QLabel(this);
  lblBurstWarning->setStyleSheet("QLabel { color: #FF8C00; font-weight: bold; }");
  lblBurstWarning->hide();

  pbBurstShift = new QPushButton(tr("Shift -1 Frame"), this);
  pbBurstShift->setIcon(QApplication::style()->standardIcon(QStyle::SP_ArrowLeft));
  pbBurstShift->hide();
  connect(pbBurstShift, SIGNAL(clicked()), this, SLOT(onBurstShift()));

  // Find the controls HBoxLayout (row 1 in the grid) and insert burst widgets
  // after pbNextCut (index 3) and before the spacer/pbExit
  QGridLayout* grid = qobject_cast<QGridLayout*>(layout());
  if (grid) {
    QLayoutItem* controlsItem = grid->itemAtPosition(1, 0);
    QHBoxLayout* controlsLayout = controlsItem ? qobject_cast<QHBoxLayout*>(controlsItem->layout()) : nullptr;
    if (controlsLayout) {
      // Original order: cbCutPreview(0), pbPrevCut(1), pbPlay(2), pbNextCut(3), spacer(4), pbExit(5)
      // Insert after pbNextCut → positions 4 and 5, pushing spacer and pbExit right
      controlsLayout->insertWidget(4, lblBurstWarning);
      controlsLayout->insertWidget(5, pbBurstShift);
    }
  }

  mpCutList = nullptr;
  mpOriginalCutList = nullptr;
  mpAVData = nullptr;
  mBurstSegmentIdx = -1;
  mBurstIsCutOut = false;
  mClipOffset = 0;
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
void TTCutPreview::initPreview(TTCutList* previewCutList, TTCutList* originalCutList, TTAVData* avData, bool skipFirst, bool skipLast)
{
  mpCutList = previewCutList;
  mpOriginalCutList = originalCutList;
  mpAVData = avData;

  int       iPos;
  QString   preview_video_name;
  QFileInfo preview_video_info;
  QString   selectionString;

  int numPreview = previewCutList->count()/2+1;

  // skipFirst/skipLast: skip standalone start/end clips when they are
  // neighbor-only context (not the selected cut). mClipOffset maps
  // combobox index → preview file index.
  mClipOffset = skipFirst ? 1 : 0;

  // Create video and audio preview clips
  for (int i = 0; i < numPreview; i++ ) {
    // first cut-in (skip when first cut is neighbor-only context)
    if (i == 0 && !skipFirst) {
      TTCutItem item = previewCutList->at(i);
      selectionString = QString("Start: %1").arg(item.cutInTime().toString("hh:mm:ss"));
      cbCutPreview->addItem( selectionString );
    }

    // cut i-i
    if (numPreview > 1 && i > 0 && i < numPreview-1) {
      iPos = (i-1)*2+1;

      TTCutItem item1 = previewCutList->at(iPos);
      TTCutItem item2 = previewCutList->at(iPos+1);
      selectionString = QString("Cut %1-%2: %3 - %4")
            .arg(i).arg(i+1)
            .arg(item1.cutInTime().toString("hh:mm:ss"))
            .arg(item2.cutOutTime().toString("hh:mm:ss"));
      cbCutPreview->addItem( selectionString );
    }

    //last cut out (skip when last cut is neighbor-only context)
    if (i == numPreview-1 && !skipLast) {
      iPos = (i-1)*2+1;

      TTCutItem item = previewCutList->at(iPos);
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

  // Map combobox index to file index (offset for transitionsOnly mode)
  int fileIndex = iCut + 1 + mClipOffset;

  // Try .mkv first (H.264/H.265 via mkvmerge), then .mpg (MPEG-2 via mplex)
  preview_video_name = QString("preview_%1.mkv").arg(fileIndex, 3, 10, QChar('0'));
  preview_video_info.setFile( QDir(TTCut::tempDirPath), preview_video_name );
  if (!preview_video_info.exists()) {
    preview_video_name = QString("preview_%1.mpg").arg(fileIndex, 3, 10, QChar('0'));
    preview_video_info.setFile( QDir(TTCut::tempDirPath), preview_video_name );
  }
  current_video_file = preview_video_info.absoluteFilePath();

  // Check for subtitle file
  preview_subtitle_name = QString("preview_%1.srt").arg(fileIndex, 3, 10, QChar('0'));
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

  checkBurstForCurrentCut(iCut);
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
 * Check for audio burst at the currently selected cut transition
 */
void TTCutPreview::checkBurstForCurrentCut(int iCut)
{
  lblBurstWarning->hide();
  pbBurstShift->hide();
  pbBurstShift->setText(tr("Shift -1 Frame"));
  mBurstSegmentIdx = -1;

  if (!mpCutList || mpCutList->count() < 2) return;

  int numPreview = mpCutList->count() / 2 + 1;

  // Only middle items (transitions) have bursts to check
  if (iCut <= 0 || iCut >= numPreview - 1) return;

  // iPos = index of the CutOut entry in the cut list for this transition
  int iPos = (iCut - 1) * 2 + 1;
  if (iPos >= mpCutList->count()) return;

  TTCutItem cutOutItem = mpCutList->at(iPos);
  if (!cutOutItem.avDataItem() || cutOutItem.avDataItem()->audioCount() == 0) return;

  double frameRate = cutOutItem.avDataItem()->videoStream()->frameRate();
  QString audioFile = cutOutItem.avDataItem()->audioStreamAt(0)->filePath();
  int threshold = TTCut::burstThresholdDb;

  // Check CutOut of left segment
  double cutOutTime = (cutOutItem.cutOutIndex() + 1) / frameRate;
  double outBurstDb = 0, outContextDb = 0;
  bool hasCutOutBurst = TTFFmpegWrapper::detectAudioBurst(audioFile, cutOutTime, true, outBurstDb, outContextDb);
  if (hasCutOutBurst && threshold != 0 && outBurstDb < threshold) hasCutOutBurst = false;

  if (hasCutOutBurst) {
    lblBurstWarning->setText(QString::fromUtf8("\xe2\x9a\xa0 Audio-Burst am Ende von Schnitt %1 (%2 dB)")
        .arg(iCut).arg(outBurstDb, 0, 'f', 1));
    lblBurstWarning->show();
    pbBurstShift->show();
    mBurstSegmentIdx = iPos;
    mBurstIsCutOut = true;
    return;  // Show only one burst per transition (CutOut takes priority)
  }

  // Check CutIn of right segment
  if (iPos + 1 < mpCutList->count()) {
    TTCutItem cutInItem = mpCutList->at(iPos + 1);
    double cutInTime = cutInItem.cutInIndex() / frameRate;
    double inBurstDb = 0, inContextDb = 0;
    bool hasCutInBurst = TTFFmpegWrapper::detectAudioBurst(audioFile, cutInTime, false, inBurstDb, inContextDb);
    if (hasCutInBurst && threshold != 0 && inBurstDb < threshold) hasCutInBurst = false;

    if (hasCutInBurst) {
      lblBurstWarning->setText(QString::fromUtf8("\xe2\x9a\xa0 Audio-Burst am Anfang von Schnitt %1 (%2 dB)")
          .arg(iCut + 1).arg(inBurstDb, 0, 'f', 1));
      lblBurstWarning->show();
      pbBurstShift->setText(tr("Shift +1 Frame"));
      pbBurstShift->show();
      mBurstSegmentIdx = iPos + 1;
      mBurstIsCutOut = false;
    }
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Shift the cut point by one frame to avoid the audio burst
 */
void TTCutPreview::onBurstShift()
{
  if (mBurstSegmentIdx < 0 || !mpCutList || !mpOriginalCutList) return;
  if (mBurstSegmentIdx >= mpCutList->count()) return;

  // Map preview index to original cut list index
  int originalIdx = mBurstSegmentIdx / 2;
  if (originalIdx >= mpOriginalCutList->count()) return;

  TTCutItem copyItem = mpOriginalCutList->at(originalIdx);

  int oldIdx, newIdx;
  if (mBurstIsCutOut) {
    oldIdx = copyItem.cutOutIndex();
    newIdx = oldIdx - 1;
  } else {
    oldIdx = copyItem.cutInIndex();
    newIdx = oldIdx + 1;
  }

  // Update the REAL model data via TTAVItem::updateCutEntry
  // The copy's avDataItem() points to the real TTAVItem in the model
  TTAVItem* avItem = copyItem.avDataItem();
  if (mpAVData && avItem) {
    // Find the real cut item in the model by matching position
    for (int i = 0; i < mpAVData->cutCount(); i++) {
      TTCutItem realItem = mpAVData->cutItemAt(i);
      if (realItem.cutInIndex() == copyItem.cutInIndex() &&
          realItem.cutOutIndex() == copyItem.cutOutIndex() &&
          realItem.avDataItem() == avItem) {
        if (mBurstIsCutOut) {
          avItem->updateCutEntry(realItem, realItem.cutInIndex(), newIdx);
        } else {
          avItem->updateCutEntry(realItem, newIdx, realItem.cutOutIndex());
        }
        qDebug() << "Burst shift: Updated REAL model cut" << i
                 << (mBurstIsCutOut ? "CutOut" : "CutIn")
                 << "Frame" << oldIdx << "->" << newIdx;
        break;
      }
    }
  }

  // Update the copy in our original cut list
  TTCutItem updatedCopy(copyItem);
  if (mBurstIsCutOut) {
    updatedCopy.update(copyItem.cutInIndex(), newIdx);
  } else {
    updatedCopy.update(newIdx, copyItem.cutOutIndex());
  }
  mpOriginalCutList->update(copyItem, updatedCopy);

  // Also update preview cut list entry so burst re-check uses new values
  TTCutItem previewItem = mpCutList->at(mBurstSegmentIdx);
  TTCutItem updatedPreview(previewItem);
  if (mBurstIsCutOut) {
    updatedPreview.update(previewItem.cutInIndex(), newIdx);
  } else {
    updatedPreview.update(newIdx, previewItem.cutOutIndex());
  }
  mpCutList->update(previewItem, updatedPreview);

  qDebug() << "Burst shift:" << (mBurstIsCutOut ? "CutOut" : "CutIn")
           << "Frame" << oldIdx << "->" << newIdx
           << "(original cut" << originalIdx << ")";

  // Show feedback
  QString label = mBurstIsCutOut ? "CutOut neu" : "CutIn neu";
  lblBurstWarning->setStyleSheet("QLabel { color: #228B22; font-weight: bold; }");
  lblBurstWarning->setText(QString::fromUtf8("\xe2\x9c\x93 %1 (Frame %2 \xe2\x86\x92 %3)")
      .arg(label).arg(oldIdx).arg(newIdx));
  pbBurstShift->hide();

  // Regenerate the current preview clip
  int iCut = cbCutPreview->currentIndex();
  regeneratePreviewClip(iCut);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Regenerate a single preview clip after burst shift
 */
void TTCutPreview::regeneratePreviewClip(int iCut)
{
  if (!mpCutList || mpCutList->count() < 2) return;

  int numPreview = mpCutList->count() / 2 + 1;
  if (iCut < 0 || iCut >= numPreview) return;

  // Stop player if running
  if (videoPlayer->isPlaying()) {
    videoPlayer->stop();
  }

  // Show progress dialog — repaint() forces synchronous painting before blocking work
  QProgressDialog progress(tr("Regenerating preview..."), QString(), 0, 0, this);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  progress.show();
  progress.repaint();
  QApplication::processEvents();

  // Build temporary cut list for this clip (same logic as TTCutPreviewTask::operation)
  TTCutList tmpCutList;
  int iPos;

  if (iCut == 0) {
    // First cut-in
    TTCutItem item = mpCutList->at(0);
    tmpCutList.append(item.avDataItem(), item.cutInIndex(), item.cutOutIndex());
  } else if (iCut == numPreview - 1) {
    // Last cut-out
    iPos = (iCut - 1) * 2 + 1;
    TTCutItem item = mpCutList->at(iPos);
    tmpCutList.append(item.avDataItem(), item.cutInIndex(), item.cutOutIndex());
  } else {
    // Middle transition: cutOut of seg X + cutIn of seg X+1
    iPos = (iCut - 1) * 2 + 1;
    TTCutItem item1 = mpCutList->at(iPos);
    TTCutItem item2 = mpCutList->at(iPos + 1);
    tmpCutList.append(item1.avDataItem(), item1.cutInIndex(), item1.cutOutIndex());
    tmpCutList.append(item2.avDataItem(), item2.cutInIndex(), item2.cutOutIndex());
  }

  if (tmpCutList.count() == 0) return;

  // Get source info
  TTCutItem firstItem = tmpCutList.at(0);
  TTAVItem* avItem = firstItem.avDataItem();
  TTVideoStream* vStream = avItem->videoStream();
  TTAVTypes::AVStreamType streamType = vStream->streamType();
  bool isMpeg2 = (streamType == TTAVTypes::mpeg2_demuxed_video ||
                  streamType == TTAVTypes::mpeg2_mplexed_video);

  QString outputFile;

  if (isMpeg2) {
    regenerateMpeg2PreviewClip(iCut, &tmpCutList, &progress);
    outputFile = TTCutPreviewTask::createPreviewFileName(iCut + 1, "mpg");
  } else {
    regenerateSmartCutPreviewClip(iCut, &tmpCutList, &progress);
    outputFile = TTCutPreviewTask::createPreviewFileName(iCut + 1, "mkv");
  }

  qDebug() << "Regenerate: Preview clip" << iCut + 1 << "rebuilt:" << outputFile;

  // Reload clip in player
  current_video_file = outputFile;
  videoPlayer->load(current_video_file);
  pbPlay->setText(tr("Play"));
  pbPlay->setIcon(QIcon::fromTheme("media-playback-start",
      QApplication::style()->standardIcon(QStyle::SP_MediaPlay)));

  // Re-check burst for the current cut
  // Reset label style back to warning color for next check
  lblBurstWarning->setStyleSheet("QLabel { color: #FF8C00; font-weight: bold; }");
  checkBurstForCurrentCut(iCut);

  // If no burst detected after shift, show success
  if (!lblBurstWarning->isVisible()) {
    lblBurstWarning->setStyleSheet("QLabel { color: #228B22; font-weight: bold; }");
    lblBurstWarning->setText(QString::fromUtf8("\xe2\x9c\x93 Burst behoben"));
    lblBurstWarning->show();
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Regenerate MPEG-2 preview clip using TTCutVideoTask + mplex
 */
void TTCutPreview::regenerateMpeg2PreviewClip(int iCut, TTCutList* tmpCutList,
                                               QProgressDialog* progress)
{
  TTCutItem firstItem = tmpCutList->at(0);
  TTAVItem* avItem = firstItem.avDataItem();
  TTVideoStream* vStream = avItem->videoStream();

  // Get A/V sync offset from .info file
  int avOffsetMs = 0;
  QString infoFile = TTESInfo::findInfoFile(vStream->filePath());
  if (!infoFile.isEmpty()) {
    TTESInfo esInfo(infoFile);
    if (esInfo.isLoaded() && esInfo.hasTimingInfo() && esInfo.avOffsetMs() != 0)
      avOffsetMs = esInfo.avOffsetMs();
  }

  progress->setLabelText(tr("Cutting MPEG-2 video..."));
  QApplication::processEvents();

  // --- Cut video ---
  QString videoFile = TTCutPreviewTask::createPreviewFileName(iCut + 1, "m2v");
  TTCutVideoTask cutVideoTask(mpAVData);
  cutVideoTask.init(videoFile, tmpCutList);
  mpAVData->threadTaskPool()->start(&cutVideoTask, true);

  // --- Cut audio ---
  bool hasAudio = (avItem->audioCount() > 0);
  QStringList cutAudioFiles;
  if (hasAudio) {
    progress->setLabelText(tr("Cutting audio..."));
    QApplication::processEvents();

    TTAudioStream* aStream = avItem->audioStreamAt(0);
    double fps = vStream->frameRate();
    QList<QPair<double, double>> audioKeepList;
    for (int c = 0; c < tmpCutList->count(); c++) {
      TTCutItem ci = tmpCutList->at(c);
      audioKeepList.append(qMakePair(ci.cutInIndex() / fps, (ci.cutOutIndex() + 1) / fps));
    }

    QString audioExt = QFileInfo(aStream->filePath()).suffix();
    QString cutAudioFile = TTCutPreviewTask::createPreviewFileName(iCut + 1, audioExt);

    QList<int> targetAcmods;
    if (TTCut::normalizeAcmod && audioExt.toLower() == "ac3") {
      for (int s = 0; s < audioKeepList.size(); s++) {
        TTFFmpegWrapper::AcmodInfo aInfo = TTFFmpegWrapper::analyzeAcmod(
            aStream->filePath(), audioKeepList[s].first, audioKeepList[s].second);
        targetAcmods.append(aInfo.mainAcmod);
      }
    }

    TTFFmpegWrapper ffmpegAudio;
    if (ffmpegAudio.cutAudioStream(aStream->filePath(), cutAudioFile,
                                    audioKeepList, TTCut::normalizeAcmod, targetAcmods)) {
      cutAudioFiles.append(cutAudioFile);
    }
  }

  progress->setLabelText(tr("Creating MKV..."));
  QApplication::processEvents();

  // --- Mux to MKV ---
  QString outputFile = TTCutPreviewTask::createPreviewFileName(iCut + 1, "mkv");
  if (hasAudio && !cutAudioFiles.isEmpty()) {
    double fps = vStream->frameRate();
    int frameDurationNs = static_cast<int>(1000000000.0 / fps);
    TTMkvMergeProvider mkvProv;
    mkvProv.setDefaultDuration("0", QString("%1ns").arg(frameDurationNs));
    {
      AVCodecID codecId;
      switch (vStream->streamType()) {
        case TTAVTypes::h265_video: codecId = AV_CODEC_ID_HEVC;       break;
        case TTAVTypes::h264_video: codecId = AV_CODEC_ID_H264;       break;
        default:                    codecId = AV_CODEC_ID_MPEG2VIDEO; break;
      }
      mkvProv.setVideoCodecId(codecId);
    }
    if (avOffsetMs != 0) mkvProv.setAudioSyncOffset(avOffsetMs);
    mkvProv.mux(outputFile, videoFile, cutAudioFiles, QStringList());
  } else {
    QFile::rename(videoFile, outputFile);
  }
  qDebug() << "Regenerate MPEG-2 preview (MKV):" << outputFile;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Regenerate H.264/H.265 preview clip using Smart Cut
 */
void TTCutPreview::regenerateSmartCutPreviewClip(int iCut, TTCutList* tmpCutList,
                                                  QProgressDialog* progress)
{
  TTCutItem firstItem = tmpCutList->at(0);
  TTAVItem* avItem = firstItem.avDataItem();
  TTVideoStream* vStream = avItem->videoStream();
  QString sourceFile = vStream->filePath();
  double frameRate = vStream->frameRate();
  QString suffix = QFileInfo(sourceFile).suffix().toLower();

  // Get A/V offset from .info file (frame rate comes from vStream, already PAFF-corrected)
  int avOffsetMs = 0;
  QString infoFile = TTESInfo::findInfoFile(sourceFile);
  if (!infoFile.isEmpty()) {
    TTESInfo esInfo(infoFile);
    if (esInfo.isLoaded()) {
      if (frameRate <= 0 && esInfo.frameRate() > 0)
        frameRate = esInfo.frameRate();
      if (esInfo.hasTimingInfo() && esInfo.avOffsetMs() != 0)
        avOffsetMs = esInfo.avOffsetMs();
    }
  }

  progress->setLabelText(tr("Video Smart Cut..."));
  QApplication::processEvents();

  // --- Smart Cut video ---
  TTESSmartCut smartCut;
  smartCut.setPresetOverride(TTCut::previewPreset);
  if (!smartCut.initialize(sourceFile, frameRate)) {
    qDebug() << "Regenerate: Smart Cut init failed:" << smartCut.lastError();
    return;
  }

  QList<QPair<int, int>> cutFrames;
  for (int i = 0; i < tmpCutList->count(); i++) {
    TTCutItem item = tmpCutList->at(i);
    cutFrames.append(qMakePair(item.cutInIndex(), item.cutOutIndex()));
  }

  QString tempVideoFile = QString("%1/preview_video_temp.%2")
      .arg(TTCut::tempDirPath).arg(suffix);

  if (!smartCut.smartCutFrames(tempVideoFile, cutFrames)) {
    qDebug() << "Regenerate: Smart Cut failed:" << smartCut.lastError();
    return;
  }

  progress->setLabelText(tr("Cutting audio..."));
  QApplication::processEvents();

  // --- Cut audio ---
  bool hasAudio = (avItem->audioCount() > 0);
  QStringList cutAudioFiles;

  if (hasAudio) {
    QString audioFile = avItem->audioStreamAt(0)->filePath();
    QList<QPair<double, double>> keepList;
    for (int i = 0; i < tmpCutList->count(); i++) {
      TTCutItem item = tmpCutList->at(i);
      double cutInTime = item.cutInIndex() / frameRate;
      double cutOutTime = (item.cutOutIndex() + 1) / frameRate;
      keepList.append(qMakePair(cutInTime, cutOutTime));
    }

    QString cutAudioFile = QString("%1/preview_audio_temp.%2")
        .arg(TTCut::tempDirPath)
        .arg(QFileInfo(audioFile).suffix());

    TTFFmpegWrapper ffmpeg;
    if (ffmpeg.cutAudioStream(audioFile, cutAudioFile, keepList)) {
      cutAudioFiles.append(cutAudioFile);
    }
  }

  progress->setLabelText(tr("Creating MKV..."));
  QApplication::processEvents();

  // --- Mux to MKV ---
  QString outputFile = TTCutPreviewTask::createPreviewFileName(iCut + 1, "mkv");
  int frameDurationNs = (int)(1000000000.0 / frameRate);

  TTMkvMergeProvider mkvProvider;
  mkvProvider.setDefaultDuration("0", QString("%1ns").arg(frameDurationNs));
  mkvProvider.setIsPAFF(vStream->isPAFF(), vStream->paffLog2MaxFrameNum());
  {
    AVCodecID codecId;
    switch (vStream->streamType()) {
      case TTAVTypes::h265_video: codecId = AV_CODEC_ID_HEVC;       break;
      case TTAVTypes::h264_video: codecId = AV_CODEC_ID_H264;       break;
      default:                    codecId = AV_CODEC_ID_MPEG2VIDEO; break;
    }
    mkvProvider.setVideoCodecId(codecId);
  }
  if (avOffsetMs != 0) {
    mkvProvider.setAudioSyncOffset(avOffsetMs);
  }
  mkvProvider.mux(outputFile, tempVideoFile, cutAudioFiles, QStringList());

  // Clean up temp files
  QFile::remove(tempVideoFile);
  for (const QString& f : cutAudioFiles) {
    QFile::remove(f);
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
