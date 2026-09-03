/* SPDX-License-Identifier: GPL-3.0-or-later */
/* TTCut-ng - avstream/ttframeinfo.h                                          */
/* Decoded-frame description shared by the MPEG-2 decoder and the libav     */
/* wrapper: pixel format codes and the plane layout of one YUV frame.        */

#ifndef TTFRAMEINFO_H
#define TTFRAMEINFO_H

#include <QtGlobal>

// Pixel format codes of the MPEG-2 decoder output
enum TPixelFormat
{
  formatRGB24 = 1,  // RGB interleaved; default
  formatRGB32 = 2,
  formatRGB8  = 3,
  formatYV12  = 4,
  formatYUV24 = 5   // YUV planes
};

// One decoded frame: plane pointers and geometry. The planes belong to the
// decoder that filled the struct and stay valid until its next decode call.
typedef struct
{
  quint8* Y;
  quint8* U;
  quint8* V;
  int      width;
  int      height;
  int      size;
  int      type;
  int      chroma_width;
  int      chroma_height;
  int      chroma_size;
} TFrameInfo;

#endif // TTFRAMEINFO_H
