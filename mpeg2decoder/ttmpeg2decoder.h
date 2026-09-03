/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTMPEG2DECODER
// ----------------------------------------------------------------------------

#ifndef TTMPEG2DECODER_H
#define TTMPEG2DECODER_H

#include "../common/ttcut.h"

// standard C header files
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// libmpeg2-dec header files
extern "C"
{
#include <mpeg2dec/mpeg2.h>
#include <mpeg2dec/mpeg2convert.h>
}

// Qt header files
#include <qstring.h>

#include "../avstream/ttavheader.h"
#include "../avstream/ttvideoheaderlist.h"
#include "../avstream/ttvideoindexlist.h"
#include "../avstream/ttmpeg2videoheader.h"
#include "../avstream/ttframeinfo.h"

/* /////////////////////////////////////////////////////////////////////////////
 * Sequence properties struct
 */
typedef struct
{
  int          width;
  int          height;
  unsigned int byte_rate;
  unsigned int vbv_buffer_size;
  unsigned int frame_period;
} TSequenceInfo;

/* /////////////////////////////////////////////////////////////////////////////
 * Initial buffer size
 */
const int initialStreamBufferSize  = 65536;
const int initialDecoderBufferSize = 5129;


/* /////////////////////////////////////////////////////////////////////////////
 * TTMpeg2Decoder class declaration
 */
class TTMpeg2Decoder
{
 public:
  TTMpeg2Decoder(QString cFName,
                 TTVideoIndexList* viIndex, TTVideoHeaderList* viHeader,
                 TPixelFormat pixelFormat=formatRGB32);
  ~TTMpeg2Decoder();

  void        openMPEG2File(QString cFName);
  int         moveToFrameIndex(int iFramePos);
  TFrameInfo* decodeFirstMPEG2Frame(TPixelFormat pixelFormat=formatRGB32);
  TFrameInfo* decodeMPEG2Frame(TPixelFormat pixelFormat=formatRGB32);
  TFrameInfo* getFrameInfo();

  int desiredFrameType;
  int desiredFramePos;

 protected:
  void initDecoder(TPixelFormat pixelFormat=formatRGB32);
  int  decodeNextFrame();
  int  skipFrames(int count);
  int  seek(quint64 seqHeaderOffset);

private:
  QFile*              mpeg2Stream;
  mpeg2dec_t*         mpeg2Decoder;
  mpeg2_state_t       state;
  quint8*             streamBuffer;
  quint8*             decoderBuffer;
  int                 streamBufferSize;
  int                 decoderBufferSize;
  const mpeg2_info_t* mpeg2Info;
  quint8*             sliceData;
  TTVideoHeaderList*  videoHeaderList;
  TTVideoIndexList*   videoIndexList;
  TPixelFormat        convType;
  TFrameInfo*         t_frame_info;
};

/* /////////////////////////////////////////////////////////////////////////////
 * Decoder exception class
 */
class TTMpeg2DecoderException
{
  public:
    enum ExceptionType
    {
      ArgumentNull,
      DecoderInit,
      StreamOpen
    };

    explicit TTMpeg2DecoderException(ExceptionType type);

    QString message() const;

  protected:
    ExceptionType ex_type;
};
#endif //TTMPEG2DECODER_H

