/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTANNEXB_H
#define TTANNEXB_H

#include <QByteArray>
#include <cstdint>

// Annex-B byte-stream framing (H.264 B.1 / H.265 B.2): start codes and NAL
// boundaries, shared by the smart cut's H.264 surgery and the HEVC seam.

// Length of the start code a buffer begins with: 4 (00 00 00 01),
// 3 (00 00 01) or 0 when it does not begin with one.
int ttStartCodeLength(const QByteArray& nal);

// Index of the next start code at or after `from` whose first NAL header
// byte is inside the data; *scLen receives 3 or 4. -1 when there is none.
int ttNextStartCode(const uint8_t* data, int size, int from, int* scLen);

// End of the NAL that runs at `from`: the index of the next 00 00 00 or
// 00 00 01 (three zero-prefixed bytes cannot occur inside an escaped NAL, so
// a zero byte ahead of a start code already belongs to the gap); `size`
// when there is none.
int ttNalEnd(const uint8_t* data, int size, int from);

#endif // TTANNEXB_H
