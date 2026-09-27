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
// H.264/AVC Video Stream — codec identity and the PAFF accessors. Everything else lives
// in TTH26xVideoStream.
// ----------------------------------------------------------------------------

#include "tth264videostream.h"

TTH264VideoStream::TTH264VideoStream(const QFileInfo& fInfo)
    : TTH26xVideoStream(fInfo)
{
    stream_type = TTAVTypes::h264_video;
}

TTAVTypes::AVStreamType TTH264VideoStream::streamType() const
{
    return TTAVTypes::h264_video;
}

TTVideoCodecType TTH264VideoStream::expectedCodec() const
{
    return CODEC_H264;
}
