/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#ifndef TTQUICKJUMPDIALOG_H
#define TTQUICKJUMPDIALOG_H

#include <QDialog>

class QListView;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;
class TTVideoStream;
class TTQuickJumpModel;
class TTQuickJumpDelegate;
class TTQuickJumpWorker;
class TTThreadTaskPool;

class TTQuickJumpDialog : public QDialog
{
  Q_OBJECT

public:
  TTQuickJumpDialog(TTVideoStream* videoStream, int currentPosition,
                    QWidget* parent = 0);
  ~TTQuickJumpDialog();

  int selectedFrameIndex() const;

protected:
  void keyPressEvent(QKeyEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private slots:
  void onItemDoubleClicked(const QModelIndex& index);
  void onPageBack();
  void onPageForward();
  void onResizeDebounced();
  void onIntervalChanged(int value);

private:
  void setupUI();
  void navigateToPage(int page);
  void updatePageLabel();
  void startThumbnailWorker();
  void abortCurrentWorker();
  void calculateItemsPerPage();

private:
  TTVideoStream*        mVideoStream;
  int                   mCurrentPosition;
  int                   mSelectedFrameIndex;

  QListView*            mListView;
  TTQuickJumpModel*     mModel;
  TTQuickJumpDelegate*  mDelegate;

  QPushButton*          mBtnBack;
  QPushButton*          mBtnForward;
  QLabel*               mPageLabel;
  QLabel*               mHintLabel;

  TTQuickJumpWorker*    mCurrentWorker;
  TTThreadTaskPool*     mTaskPool;
  QTimer*               mResizeTimer;
  QSpinBox*             mIntervalSpinner;
};

#endif // TTQUICKJUMPDIALOG_H
