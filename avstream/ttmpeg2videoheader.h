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
// *** TTSEQUENCEHEADER
// *** TTSEQUENCEENDHEADER
// *** TTGOPHEADER
// *** TTPICTURESHEADER
// ----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Overview
// -----------------------------------------------------------------------------
//
//                               +- TTMpegAudioHeader
//             +- TTAudioHeader -|
//             |                 +- TTAC3AudioHeader
// TTAVHeader -|
//             |
//             |                                     +- TTSequenceHeader
//             |                                     |
//             |                                     +- TTSequenceEndHeader
//             +- TTVideoHeader -TTMpeg2VideoHeader -|
//             |                                     +- TTPicturesHeader
//             |                                     |
//             |                                     +- TTGOPHeader
//             |
//             +- TTVideoIndex
//             |
//             +- TTBreakObject
//
// -----------------------------------------------------------------------------


#ifndef TTMPEG2VIDEOHEADER_H
#define TTMPEG2VIDEOHEADER_H

#include "ttcommon.h"
#include "ttfilebuffer.h"
#include "ttavheader.h"


// -----------------------------------------------------------------------------
// Abstract class TTMpeg2VideoHeader
// -----------------------------------------------------------------------------
class TTMpeg2VideoHeader : public TTVideoHeader
{
public:
  TTMpeg2VideoHeader();

  virtual bool readHeader( TTFileBuffer* mpeg2_stream ) = 0;
  virtual bool readHeader( TTFileBuffer* mpeg2_stream, quint64 offset ) = 0;
  virtual void parseBasicData( quint8* data, int offset=0 ) = 0;

enum mpeg2StartCodes
  {
    picture_start_code            = 0x00,
    userdata_start_code           = 0xb2,
    sequence_start_code           = 0xb3,
    sequence_error_code           = 0xb4,
    extension_start_code          = 0xb5,
    sequence_end_code             = 0xb7,
    group_start_code              = 0xb8,
    sequence_extension_id         = 0x01,
    sequence_display_extension_id = 0x02,
    ndef                          = 0xFF
  };
};

// -----------------------------------------------------------------------------
// Sequence header [0x000001B3]
// -----------------------------------------------------------------------------
class TTSequenceHeader : public TTMpeg2VideoHeader
{
 public:
  TTSequenceHeader();

  bool readHeader( TTFileBuffer* mpeg2_stream );
  bool readHeader( TTFileBuffer* mpeg2_stream, quint64 offset );
  void parseBasicData( quint8* data, int offset=0);

  int     horizontalSize();
  int     verticalSize();
  int     aspectRatio();
  QString aspectRatioText();
  float   frameRateValue();
  float   bitRateKbit();
  int     vbvBufferSize();

  // from sequence [B3]
  int      horizontal_size_value;
  int      vertical_size_value;
  int      aspect_ratio_information;
  int      frame_rate_code;
  int      bit_rate_value;
  int      vbv_buffer_size_value;

  // from sequence_extension [B5/01]
  bool     progressive_sequence;

 protected:
  void parseExtensionData( quint8* data, int offset=0 );
};

/*! \brief SequenceEndHeader
 *
 */
class TTSequenceEndHeader : public TTMpeg2VideoHeader
{
 public:
  TTSequenceEndHeader();

  bool readHeader( TTFileBuffer* mpeg2_stream );
  bool readHeader( TTFileBuffer* mpeg2_stream, quint64 offset );
  void parseBasicData( quint8* data, int offset=0);
};

// -----------------------------------------------------------------------------
// Group of pictures header [000001B8]
// -----------------------------------------------------------------------------
class TTGOPHeader : public TTMpeg2VideoHeader
{
public:
   TTGOPHeader();

  bool readHeader( TTFileBuffer* mpeg2_stream );
  bool readHeader( TTFileBuffer* mpeg2_stream, quint64 offset );
  void parseBasicData( quint8* data, int offset=0 );

   // from group_of_pictures_header [B8]
   TTimeCode time_code;
   bool      closed_gop;
   bool      broken_link;
};

// MPEG-2 picture_coding_type values (ISO/IEC 13818-2, Table 6-12).
enum Mpeg2PicCoding {
  MPEG2_PIC_I = 1,   // intra-coded
  MPEG2_PIC_P = 2,   // predictive-coded
  MPEG2_PIC_B = 3    // bidirectionally predictive-coded
};

// -----------------------------------------------------------------------------
// Pictures header [00000100]
// -----------------------------------------------------------------------------
class TTPicturesHeader : public TTMpeg2VideoHeader
{
 public:
  TTPicturesHeader();

  bool    readHeader( TTFileBuffer* mpeg2_stream );
  bool    readHeader( TTFileBuffer* mpeg2_stream, quint64 offset );
  void    parseBasicData( quint8* data, int offset=0 );
  void    parseExtensionData( quint8* data, int offset=0 );

  // from picture_header [00]
  int     temporal_reference;
  int     picture_coding_type;
  int     vbv_delay;
  bool    progressive_frame;
  bool    top_field_first;
  // from picture_coding_extension [B5/08]
  int      picture_structure;  // 1=top, 2=bottom, 3=frame (default: 3)
};
#endif //TTMPEG2VIDEOHEADER_H
