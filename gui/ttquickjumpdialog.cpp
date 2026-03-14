/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#include "ttquickjumpdialog.h"
#include "ttquickjumpmodel.h"
#include "ttquickjumpdelegate.h"
#include "ttquickjumpworker.h"

#include "../avstream/ttavstream.h"
#include "../avstream/ttavtypes.h"
#include "../avstream/ttmpeg2videoheader.h"
#include "../avstream/tth264videostream.h"
#include "../avstream/tth265videostream.h"
#include "../common/ttthreadtaskpool.h"

#include <QListView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QDebug>

#include "../common/ttcut.h"

static const int THUMB_HEIGHT = 83;

static int computeThumbWidth(TTVideoStream* vs)
{
  int type = vs->streamType();
  bool isMpeg2 = (type == TTAVTypes::mpeg2_demuxed_video ||
                   type == TTAVTypes::mpeg2_mplexed_video);

  if (isMpeg2) {
    // MPEG-2 SD uses non-square pixels — need Display AR from sequence header
    TTSequenceHeader* seq = vs->currentSequenceHeader();
    if (seq) {
      int ar = seq->aspectRatio();
      if (ar == 3) return THUMB_HEIGHT * 16 / 9;   // 16:9
      if (ar == 4) return THUMB_HEIGHT * 221 / 100; // 2.21:1
      if (ar == 2) return THUMB_HEIGHT * 4 / 3;     // 4:3
      // Fallback: pixel dimensions
      if (seq->verticalSize() > 0)
        return THUMB_HEIGHT * seq->horizontalSize() / seq->verticalSize();
    }
  }

  // H.264/H.265: pixels are square (SAR=1:1) — 16:9 for HD/UHD
  return THUMB_HEIGHT * 16 / 9;
}

TTQuickJumpDialog::TTQuickJumpDialog(TTVideoStream* videoStream,
                                      int currentPosition, QWidget* parent)
  : QDialog(parent),
    mVideoStream(videoStream),
    mCurrentPosition(currentPosition),
    mSelectedFrameIndex(-1),
    mCurrentWorker(0),
    mTaskPool(new TTThreadTaskPool()),
    mResizeTimer(new QTimer(this))
{
  mResizeTimer->setSingleShot(true);
  mResizeTimer->setInterval(200);
  connect(mResizeTimer, SIGNAL(timeout()), SLOT(onResizeDebounced()));
  setWindowTitle(tr("Zeitsprung — Keyframes"));
  setWindowModality(Qt::WindowModal);

  int thumbWidth = computeThumbWidth(videoStream);
  mDelegate = new TTQuickJumpDelegate(thumbWidth, THUMB_HEIGHT, this);
  mModel = new TTQuickJumpModel(videoStream, this);

  // Apply interval from global settings
  mModel->setIntervalSeconds(TTCut::quickJumpIntervalSec);

  setupUI();
  calculateItemsPerPage();

  // Navigate to page containing current position
  int page = mModel->pageForFrameIndex(mCurrentPosition);
  navigateToPage(page);

  // Highlight the keyframe closest to current position
  mDelegate->setHighlightFrameIndex(mCurrentPosition);

  // Restore window size or use 80% of available screen
  QSettings settings;
  QRect screenGeom = QGuiApplication::primaryScreen()->availableGeometry();
  QSize defaultSize(screenGeom.width() * 80 / 100, screenGeom.height() * 80 / 100);
  QSize savedSize = settings.value("QuickJumpDialog/size", defaultSize).toSize();
  resize(savedSize);
}

TTQuickJumpDialog::~TTQuickJumpDialog()
{
  abortCurrentWorker();

  // Save window size
  QSettings settings;
  settings.setValue("QuickJumpDialog/size", size());

  delete mTaskPool;
}

void TTQuickJumpDialog::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(4, 4, 4, 4);
  mainLayout->setSpacing(4);

  // List view
  mListView = new QListView(this);
  mListView->setModel(mModel);
  mListView->setItemDelegate(mDelegate);
  mListView->setViewMode(QListView::IconMode);
  mListView->setResizeMode(QListView::Adjust);
  mListView->setUniformItemSizes(true);
  mListView->setMovement(QListView::Static);
  mListView->setSelectionMode(QAbstractItemView::SingleSelection);
  mListView->setWrapping(true);
  mListView->setSpacing(2);

  connect(mListView, SIGNAL(doubleClicked(QModelIndex)),
          SLOT(onItemDoubleClicked(QModelIndex)));

  mainLayout->addWidget(mListView, 1);

  // Pagination bar
  QHBoxLayout* pageLayout = new QHBoxLayout();
  pageLayout->setContentsMargins(0, 0, 0, 0);

  mBtnBack = new QPushButton(tr("Back"), this);
  mPageLabel = new QLabel(this);
  mPageLabel->setAlignment(Qt::AlignCenter);
  mPageLabel->setMinimumWidth(110);
  mBtnForward = new QPushButton(tr("Forward"), this);

  // Interval spinner
  QLabel* intervalLabel = new QLabel(tr("Interval:"), this);
  mIntervalSpinner = new QSpinBox(this);
  mIntervalSpinner->setRange(0, 600);
  mIntervalSpinner->setSuffix(" s");
  mIntervalSpinner->setSpecialValueText(tr("All"));
  mIntervalSpinner->setValue(mModel->intervalSeconds());
  mIntervalSpinner->setToolTip(tr("Show keyframes every N seconds (0=all)"));

  connect(mBtnBack,    SIGNAL(clicked()), SLOT(onPageBack()));
  connect(mBtnForward, SIGNAL(clicked()), SLOT(onPageForward()));
  connect(mIntervalSpinner, SIGNAL(valueChanged(int)), SLOT(onIntervalChanged(int)));

  pageLayout->addWidget(intervalLabel);
  pageLayout->addWidget(mIntervalSpinner);
  pageLayout->addStretch();
  pageLayout->addWidget(mBtnBack);
  pageLayout->addWidget(mPageLabel);
  pageLayout->addWidget(mBtnForward);
  pageLayout->addStretch();

  mainLayout->addLayout(pageLayout);

  // Hint label
  mHintLabel = new QLabel(tr("Double-click thumbnail to jump to frame"), this);
  mHintLabel->setAlignment(Qt::AlignCenter);
  QFont hintFont = mHintLabel->font();
  hintFont.setPointSize(8);
  mHintLabel->setFont(hintFont);
  mHintLabel->setStyleSheet("color: #6c7086;");

  mainLayout->addWidget(mHintLabel);
}

void TTQuickJumpDialog::calculateItemsPerPage()
{
  QSize cellSize = mDelegate->sizeHint(QStyleOptionViewItem(), QModelIndex());
  int viewWidth = mListView->viewport()->width();
  int viewHeight = mListView->viewport()->height();

  if (viewWidth < cellSize.width() || viewHeight < cellSize.height()) {
    // Fallback to default
    mModel->setItemsPerPage(30);
    return;
  }

  int cols = viewWidth / (cellSize.width() + mListView->spacing());
  int rows = viewHeight / (cellSize.height() + mListView->spacing());

  if (cols < 1) cols = 1;
  if (rows < 1) rows = 1;

  mModel->setItemsPerPage(cols * rows);
}

void TTQuickJumpDialog::navigateToPage(int page)
{
  abortCurrentWorker();
  mModel->setPage(page);
  updatePageLabel();
  startThumbnailWorker();
}

void TTQuickJumpDialog::updatePageLabel()
{
  mPageLabel->setText(tr("Page %1 / %2")
    .arg(mModel->currentPage() + 1)
    .arg(mModel->pageCount()));

  mBtnBack->setEnabled(mModel->currentPage() > 0);
  mBtnForward->setEnabled(mModel->currentPage() < mModel->pageCount() - 1);
}

void TTQuickJumpDialog::startThumbnailWorker()
{
  // Collect frame indices for current page
  int offset = mModel->currentPage() * mModel->itemsPerPage();
  int count = mModel->rowCount();
  const QList<int>& allKeyframes = mModel->keyframeIndices();

  QList<int> pageFrames;
  for (int i = 0; i < count; ++i) {
    int idx = offset + i;
    if (idx < allKeyframes.size()) {
      int frameIndex = allKeyframes.at(idx);
      // Skip frames that already have thumbnails cached
      pageFrames.append(frameIndex);
    }
  }

  if (pageFrames.isEmpty()) return;

  int streamType = mVideoStream->streamType();

  // Reuse existing frame index for H.264/H.265 (avoids 6s rebuild)
  QList<TTFrameInfo> prebuiltIndex;
  if (streamType == TTAVTypes::h264_video) {
    TTH264VideoStream* h264 = static_cast<TTH264VideoStream*>(mVideoStream);
    prebuiltIndex = h264->ffmpegFrameIndex();
  } else if (streamType == TTAVTypes::h265_video) {
    TTH265VideoStream* h265 = static_cast<TTH265VideoStream*>(mVideoStream);
    prebuiltIndex = h265->ffmpegFrameIndex();
  }

  mCurrentWorker = new TTQuickJumpWorker(
    mVideoStream->filePath(), streamType, pageFrames,
    mDelegate->thumbnailSize(),
    mVideoStream->indexList(), mVideoStream->headerList(),
    prebuiltIndex
  );

  connect(mCurrentWorker, SIGNAL(thumbnailReady(int, QImage)),
          mModel, SLOT(onThumbnailReady(int, QImage)));

  mTaskPool->init(1);
  mTaskPool->start(mCurrentWorker);
}

void TTQuickJumpDialog::abortCurrentWorker()
{
  if (mCurrentWorker) {
    // Disconnect signals BEFORE aborting to prevent stale thumbnails
    disconnect(mCurrentWorker, 0, mModel, 0);
    if (mCurrentWorker->isRunning()) {
      mCurrentWorker->onUserAbort();
    }
    mCurrentWorker = 0;
  }
}

int TTQuickJumpDialog::selectedFrameIndex() const
{
  return mSelectedFrameIndex;
}

void TTQuickJumpDialog::onItemDoubleClicked(const QModelIndex& index)
{
  if (!index.isValid()) return;

  mSelectedFrameIndex = index.data(TTQuickJumpModel::FrameIndexRole).toInt();
  accept();
}

void TTQuickJumpDialog::onPageBack()
{
  if (mModel->currentPage() > 0) {
    navigateToPage(mModel->currentPage() - 1);
  }
}

void TTQuickJumpDialog::onPageForward()
{
  if (mModel->currentPage() < mModel->pageCount() - 1) {
    navigateToPage(mModel->currentPage() + 1);
  }
}

void TTQuickJumpDialog::keyPressEvent(QKeyEvent* event)
{
  switch (event->key()) {
    case Qt::Key_Left:
      onPageBack();
      break;
    case Qt::Key_Right:
      onPageForward();
      break;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
      QModelIndex current = mListView->currentIndex();
      if (current.isValid()) {
        onItemDoubleClicked(current);
      }
      break;
    }
    default:
      QDialog::keyPressEvent(event);
  }
}

void TTQuickJumpDialog::resizeEvent(QResizeEvent* event)
{
  QDialog::resizeEvent(event);
  // Debounce -- resize fires many events in quick succession
  mResizeTimer->start();
}

void TTQuickJumpDialog::onResizeDebounced()
{
  calculateItemsPerPage();

  // Re-navigate to recalculate page with new items per page
  int page = mModel->pageForFrameIndex(mCurrentPosition);
  navigateToPage(page);
}

void TTQuickJumpDialog::onIntervalChanged(int value)
{
  abortCurrentWorker();
  mModel->setIntervalSeconds(value);
  calculateItemsPerPage();

  int page = mModel->pageForFrameIndex(mCurrentPosition);
  navigateToPage(page);
}
