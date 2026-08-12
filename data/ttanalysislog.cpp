/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttanalysislog.h"

#include <QTime>
#include <QtGlobal>

TTAnalysisLog::TTAnalysisLog(LineSink sink, int maxEvents)
  : mSink(std::move(sink)),
    mMaxEvents(qMax(0, maxEvents))
{
}

void TTAnalysisLog::line(const QString& text)
{
  if (mSink) mSink(text);
}

void TTAnalysisLog::event(const QString& text)
{
  if (mEmitted >= mMaxEvents) {
    mSuppressed++;
    return;
  }
  mEmitted++;
  if (mSink) mSink(text);
}

void TTAnalysisLog::resetCap()
{
  mEmitted    = 0;
  mSuppressed = 0;
}

int TTAnalysisLog::suppressed() const
{
  return mSuppressed;
}

QString ttFormatStreamPosition(int frame, float frameRate)
{
  if (frameRate <= 0.0f)
    return QString("frame %1").arg(frame);

  const int ms = qRound(frame / frameRate * 1000.0f);
  return QString("%1 (frame %2)")
      .arg(QTime(0, 0).addMSecs(ms).toString("hh:mm:ss"))
      .arg(frame);
}
