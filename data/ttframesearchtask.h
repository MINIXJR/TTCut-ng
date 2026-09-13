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
// TTFRAMESEARCHTASK
// ----------------------------------------------------------------------------

#ifndef TTFRAMESEARCHTASK_H
#define TTFRAMESEARCHTASK_H

#include "../common/ttthreadtask.h"
#include <atomic>
#include "../mpeg2decoder/ttmpeg2decoder.h"

class TTVideoStream;
class TTFFmpegWrapper;

//! Runable task for frame comparison and searching
class TTFrameSearchTask : public TTThreadTask
{
  Q_OBJECT

  public:
    TTFrameSearchTask(TTVideoStream* referenceStream, int referenceIndex,
                      TTVideoStream* searchStream,    int searchIndex);

  protected:
    enum class DecoderKind { Mpeg2, FFmpeg };
    static DecoderKind decoderKindFor(TTVideoStream* stream);

    void    initFrameSearch();
    quint64 compareFrames(const TFrameInfo& searchInfo);
    void    cleanUp() override;
    void    operation() override;

  public slots:
    void onUserAbort() override;

  signals:
    void finished(int index);

  private:
    //! Open `stream` with a search wrapper: cancel token set, the stream's
    //! frame index adopted (or built), search mode off. Throws
    //! TTAbortException naming `role` when the file or the index fails.
    TTFFmpegWrapper* openFFmpegWrapperFor(TTVideoStream* stream, const char* role);
    //! Copy the reference frame's planes into mpRefY/U/V.
    void captureRefBuffers(const TFrameInfo& refInfo);
    TTVideoStream*  mpReferenceStream;
    TTVideoStream*  mpSearchStream;
    int             mReferenceIndex;
    int             mSearchIndex;
    // Reference frame YUV planes (tight-packed, copied during initFrameSearch)
    quint8*         mpRefY;
    quint8*         mpRefU;
    quint8*         mpRefV;
    int             mRefWidth;
    int             mRefHeight;
    //! Set on the GUI thread (onUserAbort), read by the worker between frames
    //! and - via TTFFmpegWrapper::setCancelToken - inside a running decode.
    std::atomic<bool> mAbort;
};

#endif
