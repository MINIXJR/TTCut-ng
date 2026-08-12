/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTANALYSISLOG_H
#define TTANALYSISLOG_H

#include <QString>

#include <functional>

//! Detail-pane logging for the landing-zone analysis workers.
//!
//! Deliberately not a QObject: the sink is a plain callback, so the class can
//! be exercised without a task pool, a progress dialog or an event loop (see
//! tools/diag/test_analysislog). The workers bind the sink to their inherited
//! status channel:
//!
//!   TTAnalysisLog mLog([this](const QString& s) {
//!       onStatusReport(StatusReportArgs::AddProcessLine, s, 0);
//!     }, 20);
//!
//! AddProcessLine is the one status state that carries no progress meaning:
//! it passes TTThreadTask and TTThreadTaskPool without touching their step
//! counters and reaches TTProgressBar as a pure detail line.
class TTAnalysisLog
{
public:
  using LineSink = std::function<void(const QString&)>;

  //! maxEvents caps event() per section; line() is never capped.
  TTAnalysisLog(LineSink sink, int maxEvents);

  //! Uncapped line - headers and summaries.
  void line(const QString& text);

  //! Capped line - findings and discarded candidates. Beyond the cap the text
  //! is dropped and only counted, so one chatty analysis cannot crowd another
  //! one out of the pane.
  void event(const QString& text);

  //! Open the cap for the next section and clear the suppressed counter.
  //! An analysis with several sections (audio: silence, then format) calls
  //! this between them.
  void resetCap();

  //! How many event() calls the cap swallowed since the last resetCap().
  int suppressed() const;

private:
  LineSink mSink;
  int      mMaxEvents;
  int      mEmitted    = 0;
  int      mSuppressed = 0;
};

//! "00:16:34 (frame 49719)" - the time form the landing-zone list uses
//! (data/ttstreampointmodel.cpp). A frame rate <= 0 yields "frame 49719"
//! alone rather than an invented time.
QString ttFormatStreamPosition(int frame, float frameRate);

#endif // TTANALYSISLOG_H
