/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTSTREAMPOINT_AUDIOWORKER_H
#define TTSTREAMPOINT_AUDIOWORKER_H

#include "../common/ttthreadtask.h"
#include "ttanalysislog.h"
#include "ttstreampoint.h"

#include <QList>
#include <QString>

struct AVFrame;
class TTAudioHeaderList;

class TTStreamPointAudioWorker : public TTThreadTask
{
  Q_OBJECT

public:
  TTStreamPointAudioWorker(const QString& audioFilePath, float videoFrameRate,
                           bool detectSilence, int silenceThresholdDb,
                           float silenceMinDuration,
                           bool detectAudioChange,
                           TTAudioHeaderList* audioHeaderList);

signals:
  void pointsDetected(const QList<TTStreamPoint>& points);

protected:
  void operation() override;
  void cleanUp() override;

public slots:
  void onUserAbort() override;

private:
  QList<TTStreamPoint> detectSilencePoints();

  //! Report why silence detection could not run, remember that it did not,
  //! and return an empty result. Without this the caller would go on to
  //! print "0 regions found", which is indistinguishable from a track that
  //! genuinely holds no silence - the very ambiguity this pane exists to
  //! remove.
  QList<TTStreamPoint> silenceUnavailable(const QString& reason);
  QList<TTStreamPoint> detectAudioChanges();
  void collectSilenceResult(AVFrame* filtFrame, QList<TTStreamPoint>& results);

  QString              mAudioFilePath;
  float                mVideoFrameRate;
  bool                 mDetectSilence;
  int                  mSilenceThresholdDb;
  float                mSilenceMinDuration;
  bool                 mDetectAudioChange;
  TTAudioHeaderList*   mAudioHeaderList;
  bool                 mSilenceEngineFailed = false;
  TTAnalysisLog        mLog;
};

#endif // TTSTREAMPOINT_AUDIOWORKER_H
