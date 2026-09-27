/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTH264VIDEOSTREAM
// H.264/AVC Video Stream — codec identity and the PAFF accessors. Everything
// else lives in TTH26xVideoStream.
// ----------------------------------------------------------------------------

#ifndef TTH264VIDEOSTREAM_H
#define TTH264VIDEOSTREAM_H

#include "tth26xvideostream.h"

#include <QFileInfo>

class TTH264VideoStream : public TTH26xVideoStream
{
    Q_OBJECT

public:
    explicit TTH264VideoStream(const QFileInfo& fInfo);
    ~TTH264VideoStream() override = default;

    // Stream identity
    TTAVTypes::AVStreamType streamType() const override;
    bool isPAFF() const override { return mFrameIndexBundle.isPAFF; }
    int  paffLog2MaxFrameNum() const override { return mFrameIndexBundle.log2MaxFrameNum; }
    const char* codecLabel() const override { return "H.264"; }

protected:
    TTVideoCodecType expectedCodec() const override;
};

#endif // TTH264VIDEOSTREAM_H
