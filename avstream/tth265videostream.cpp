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
// H.265/HEVC Video Stream — codec identity. Everything else lives
// in TTH26xVideoStream.
// ----------------------------------------------------------------------------

#include "tth265videostream.h"

TTH265VideoStream::TTH265VideoStream(const QFileInfo& fInfo)
    : TTH26xVideoStream(fInfo)
{
    stream_type = TTAVTypes::h265_video;
    mLog->infoMsg(__FILE__, __LINE__,
        QString("Creating H.265/HEVC video stream for: %1").arg(fInfo.filePath()));
}

TTAVTypes::AVStreamType TTH265VideoStream::streamType() const
{
    return TTAVTypes::h265_video;
}

TTVideoCodecType TTH265VideoStream::expectedCodec() const
{
    return CODEC_H265;
}
