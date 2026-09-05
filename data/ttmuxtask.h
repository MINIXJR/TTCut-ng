/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTMUXTASK
// ----------------------------------------------------------------------------

#ifndef TTMUXTASK_H
#define TTMUXTASK_H

#include "ttabortabletask.h"
#include "../extern/ttmkvmergeprovider.h"

#include <QString>
#include <QStringList>

#include <atomic>

class TTAVData;

//! Value bundle for one MKV mux run. Everything is copied on the GUI thread
//! before the task starts, so the worker never reads the cut list or the mux
//! list. Same arrangement as TTH26xCutParams.
struct TTMuxTaskParams
{
  QString     mkvOutput;             // target .mkv
  QString     videoFile;             // cut video ES
  QStringList audioFiles;            // cut audio ES, in track order
  QStringList subtitleFiles;         // cut subtitle files, in track order
  QStringList audioLanguages;        // ISO 639-2/B tags, per audio file
  QStringList subtitleLanguages;     // ISO 639-2/B tags, per subtitle file
  QString     chapterFile;           // empty = no chapters
  QString     defaultDurationNs;     // frame duration of track 0, e.g. "40000000ns"
  bool        isPAFF = false;
  int         paffLog2MaxFrameNum = 4;
  int         videoCodecId = 0;      // AVCodecID value (libavcodec/codec_id.h)
  int         audioSyncOffsetMs = 0; // 0 = do not apply an offset
  qint64      totalDurationMs = 0;   // 0 = do not set one (chapter end calc)
  //! Everything the cut has produced so far and that only exists to feed this
  //! mux (video/audio/subtitle ES). A cancel deletes these together with the
  //! partial .mkv and the chapter file: after a cancel none of them is useful.
  //! A real mux FAILURE leaves them alone, exactly like before.
  QStringList cleanupOnAbort;
};

//! Pool task wrapping one TTMkvMergeProvider::mux() call.
//!
//! The MPEG-2 cut used to mux synchronously inside TTAVData::onCutFinished(),
//! on the GUI thread, where a Cancel click could not be delivered at all. It
//! now runs here, as the cut operation's SECOND pool run (the video task being
//! the first) - see onCutFinished()/onMpeg2MuxFinished() for the status
//! bracket handling that spans both runs.
class TTMuxTask : public TTAbortableTask
{
  Q_OBJECT

  public:
    TTMuxTask(TTAVData* avData);
    void init(const TTMuxTaskParams& params);

    // Results, valid after the pool's exit signal (worker done):
    //! Empty on success; otherwise TTMkvMergeProvider::lastError().
    QString lastError() const { return mError; }
    QString mkvOutput() const { return mParams.mkvOutput; }
    //! The inputs, for the GUI-side follow-up work (ES deletion, chapter file
    //! removal) that stays in onMpeg2MuxFinished().
    const TTMuxTaskParams& params() const { return mParams; }

  protected:
    void cleanUp() override;
    void operation() override;

  public slots:
    void onUserAbort() override;

  private:
    TTMuxTaskParams mParams;
    QString         mError;
    //! mCreatedFiles (base) is filled by init() already, not by operation():
    //! a cancel can arrive before the pool ever schedules run(), and the
    //! files exist from the moment the task is created.
    //! Set by operation() once the mux has run to a conclusion (success or a
    //! real failure). Worker thread only; read by cleanUp() on the same
    //! thread. See cleanUp() for what it guards against.
    bool            mOperationDone = false;

    //! The provider is a member, not a local, so onUserAbort() (GUI thread)
    //! can reach it without racing the worker that would otherwise create and
    //! destroy it. It only receives an atomic store from that side. Same
    //! arrangement as TTH26xCutTask's two engines.
    TTMkvMergeProvider mMkvProvider;
};

#endif
