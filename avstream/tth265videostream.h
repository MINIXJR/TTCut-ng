/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTH265VIDEOSTREAM
// H.265/HEVC Video Stream — codec identity. Everything else lives in
// TTH26xVideoStream.
// ----------------------------------------------------------------------------

#ifndef TTH265VIDEOSTREAM_H
#define TTH265VIDEOSTREAM_H

#include "tth26xvideostream.h"

#include <QFileInfo>

class TTH265VideoStream : public TTH26xVideoStream
{
    Q_OBJECT

public:
    explicit TTH265VideoStream(const QFileInfo& fInfo);
    ~TTH265VideoStream() override = default;

    TTAVTypes::AVStreamType streamType() const override;
    const char* codecLabel() const override { return "H.265"; }

protected:
    TTVideoCodecType expectedCodec() const override;
};

#endif // TTH265VIDEOSTREAM_H
