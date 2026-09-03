/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTAVUTIL_H
#define TTAVUTIL_H

#include <QString>

// Free helpers over libav shared by TTFFmpegWrapper, TTAudioCutter and
// TTFrameIndexer. No state, no Qt objects.

// av_strerror() as a QString.
QString ttAvErrorToString(int errnum);

#endif // TTAVUTIL_H
