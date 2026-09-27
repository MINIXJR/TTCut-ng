/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally (c) 2019 Minei3oat / github.com/Minei3oat                       */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTSRTSUBTITLESTREAM
// ----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Overview
// -----------------------------------------------------------------------------
//
//                               +- TTMpegAudioStream
//             +- TTAudioStream -|
//             |                 +- TTAC3AudioStream
// TTAVStream -|
//             |
//             +- TTVideoStream - TTMpeg2VideoStream
//             |
//             +- TTSubtitleStream - TTSrtSubtitleStream
//
// -----------------------------------------------------------------------------

#ifndef TTSRTSUBTITLESTREAM_H
#define TTSRTSUBTITLESTREAM_H

#include "ttavstream.h"
#include "../common/ttmessagelogger.h"

class TTSrtSubtitleStream : public TTSubtitleStream
{
  Q_OBJECT

  public:
    explicit TTSrtSubtitleStream(const QFileInfo &f_info);
    ~TTSrtSubtitleStream() override;

    TTAVTypes::AVStreamType streamType() const override;

    void cut(int start, int end, TTCutParameter* cp) override;

    int  createHeaderList() override;
    int  createIndexList() override {return 0;}

    QTime streamLengthTime() override;
};

#endif // TTSRTSUBTITLESTREAM_H
