/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTFRAMEPOSITIONTEXT_H
#define TTFRAMEPOSITIONTEXT_H

#include <QString>

#include "../avstream/ttavstream.h"
#include "../avstream/ttcommon.h"

// "hh:mm:ss.zzz (index)[type]" - the position label of the current-frame and
// the cut-out widget.
inline QString ttFramePositionText(TTVideoStream* videoStream, int pos)
{
  return videoStream->frameTime(pos).toString("hh:mm:ss.zzz")
       + QString(" (%1)").arg(pos)
       + ttFrameTypeTag(videoStream->frameType(pos));
}

#endif // TTFRAMEPOSITIONTEXT_H
