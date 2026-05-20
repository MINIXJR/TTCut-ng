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
#include "../mpeg2decoder/ttmpeg2decoder.h"

class TTVideoStream;

//! Runable task for frame comparison and searching
class TTFrameSearchTask : public TTThreadTask
{
  Q_OBJECT

  public:
    TTFrameSearchTask(TTVideoStream* referenceStream, int referenceIndex,
                      TTVideoStream* searchStream,    int searchIndex);

  protected:
    enum class DecoderKind { Mpeg2, FFmpeg };
    DecoderKind decoderKindFor(TTVideoStream* stream) const;

    void    initFrameSearch();
    quint64 compareFrames(const TFrameInfo& searchInfo);
    void    cleanUp();
    void    operation();

  public slots:
    void onUserAbort();

  signals:
    void finished(int index);

  private:
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
    bool            mAbort;
};

#endif
