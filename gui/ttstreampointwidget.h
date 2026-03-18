/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#ifndef TTSTREAMPOINTWIDGET_H
#define TTSTREAMPOINTWIDGET_H

#include <QWidget>

class QListView;
class QTabWidget;
class QPushButton;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QLabel;
class TTStreamPointModel;

class TTStreamPointWidget : public QWidget
{
  Q_OBJECT

public:
  TTStreamPointWidget(TTStreamPointModel* model, QWidget* parent = 0);

  void setAnalysisRunning(bool running);
  void loadSettings();
  void saveSettings();

signals:
  void analyzeRequested();
  void abortRequested();
  void jumpToFrame(int frameIndex);
  void deleteRequested(int row);
  void deleteAllRequested();
  void setCutIn(int frameIndex);
  void setCutOut(int frameIndex);

private slots:
  void onAnalyzeClicked();
  void onDeleteAllClicked();
  void onItemDoubleClicked(const QModelIndex& index);
  void onContextMenu(const QPoint& pos);
  void onDeleteKey();

private:
  void setupLandezonenTab(QWidget* tab);
  void setupSettingsTab(QWidget* tab);

  TTStreamPointModel* mModel;
  QListView*          mListView;
  QTabWidget*         mTabWidget;
  QPushButton*        mBtnAnalyze;
  QPushButton*        mBtnDeleteAll;
  QLabel*             mLblStatus;
  bool                mAnalysisRunning;

  // Settings widgets
  QCheckBox*      mCbSilence;
  QSpinBox*       mSbSilenceThreshold;
  QDoubleSpinBox* mSbSilenceMinDuration;
  QCheckBox*      mCbAudioChange;
  QCheckBox*      mCbAspectChange;
};

#endif // TTSTREAMPOINTWIDGET_H
