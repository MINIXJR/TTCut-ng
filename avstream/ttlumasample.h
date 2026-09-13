/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTLUMASAMPLE_H
#define TTLUMASAMPLE_H

// The sampling rule the black-frame test and the luma histogram share, for
// H.26x (TTFFmpegWrapper, on the decoder's Y plane) and MPEG-2 (TTSearchTask,
// on a grayscale QImage) alike: skip a 10 % border on every side (station
// logos, letterbox edges), take every second row and column. The thresholds
// applied to the samples stay with each domain.
struct TTCentreBand
{
  int x0, y0, x1, y1;
  static constexpr int step = 2;

  static TTCentreBand of(int width, int height)
  {
    const int bx = width / 10, by = height / 10;
    return { bx, by, width - bx, height - by };
  }
};

#endif // TTLUMASAMPLE_H
