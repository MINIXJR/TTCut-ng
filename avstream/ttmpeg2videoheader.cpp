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
// TTSEQUENCEHEADER
// TTSEQUENCEENDHEADER
// TTGOPHEADER
// TTPICTURESHEADER
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


#include "ttmpeg2videoheader.h"

const char cName[] = "MPEGVIDEOHEADER";

/* /////////////////////////////////////////////////////////////////////////////
 * TTMpeg2VideoHeader
 * Base class for all MPEG2 video header
 */
TTMpeg2VideoHeader::TTMpeg2VideoHeader()
{
  log = TTMessageLogger::getInstance();
}


/* /////////////////////////////////////////////////////////////////////////////
 * TTSequenceHeader: Sequence header [0x000001B3]
 * Default constructor, extends TTMpeg2VideoHeader
 */
TTSequenceHeader::TTSequenceHeader() : TTMpeg2VideoHeader()
{
  header_start_code     = sequence_start_code;
  progressive_sequence  = false;  // interlaced unless sequence_extension says otherwise
}

/* /////////////////////////////////////////////////////////////////////////////
 * Read sequence header from stream
 */
bool TTSequenceHeader::readHeader( TTFileBuffer* mpeg2_stream )
{
  quint8  header_data[8];

  try
  {
    // read 8 byte from stream, starting at current offset
    mpeg2_stream->readByte( header_data, 8 ) ;

    // fill sequence header
    header_offset = mpeg2_stream->position() - 12;
    parseBasicData( header_data );

    // Search for sequence_extension (extension_start_code 0xB5 with id 0x1 in
    // upper nibble of first extension byte). Limit search to 1024 bytes to
    // avoid scanning entire file on corrupt data. If no extension is found
    // (e.g. MPEG-1 stream or truncated), progressive_sequence keeps its
    // default value of false.
    int count_zeros = 0;
    int searchLimit = 1024;
    quint8 value;
    do
    {
      mpeg2_stream->readByte(value);
      if ( value == 0x00 )
      {
        count_zeros++;
      }
      else if ( value != 1 )
      {
        count_zeros = 0;
      }
      if (--searchLimit <= 0) return true;  // no extension found — keep defaults
    }
    while ( value != 0x01 || count_zeros < 2 );

    // value is 0x01, next byte is the start_code_identifier
    quint8 identifier;
    mpeg2_stream->readByte(identifier);
    if (identifier != extension_start_code) return true;  // not extension — keep defaults

    // Read 6 bytes of extension data (enough for progressive_sequence)
    quint8 ext_data[6];
    mpeg2_stream->readByte( ext_data, 6 );

    // Check the extension_id nibble (upper 4 bits of byte 0)
    if ((ext_data[0] & 0xF0) != 0x10) return true;  // not sequence_extension — keep defaults

    parseExtensionData( ext_data );
  }
  catch (TTFileBufferException)
  {
    return false;
  }
  return true;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Read sequence header at given offset
 */
bool TTSequenceHeader::readHeader( TTFileBuffer* mpeg2_stream, quint64 offset )
{
  mpeg2_stream->seekAbsolute( offset+4 );

  return readHeader( mpeg2_stream );
}

/* /////////////////////////////////////////////////////////////////////////////
 * Parse basic header data
 */
void TTSequenceHeader::parseBasicData( quint8* data, int offset )
{
  horizontal_size_value        = (data[offset+0] << 4) + ((data[offset+1] & 0xF0) >> 4);
  vertical_size_value          = ((data[offset+1] & 0x0F) << 8) + data[offset+2];
  aspect_ratio_information     = (data[offset+3] & 0xF0) >> 4;
  if (aspect_ratio_information == 0 || aspect_ratio_information > 4)
    aspect_ratio_information = 1;  // default: square pixels
  frame_rate_code              = (data[offset+3] & 0x0F);
  if (frame_rate_code == 0 || frame_rate_code > 8)
    frame_rate_code = 3;  // default: 25 fps (PAL)
  bit_rate_value               = (int)(((data[offset+4] << 10) + (data[offset+5] << 2)+((data[offset+6] & 0xC0) >> 6))*400);
  vbv_buffer_size_value        = ((data[offset+6] & 0x1F) << 5)+((data[offset+7] & 0xF8) >> 3);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Parse sequence_extension data. progressive_sequence is bit 3 of byte 1
 * (per ISO/IEC 13818-2 section 6.2.2.3).
 */
void TTSequenceHeader::parseExtensionData( quint8* data, int offset )
{
  progressive_sequence = ((data[offset+1] & 0x08) == 0x08);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Returns the horizontal size value
 */
int TTSequenceHeader::horizontalSize()
{
  return horizontal_size_value;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Returns the vertical size value
 */
int TTSequenceHeader::verticalSize()
{
  return vertical_size_value;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Return the aspect ratio code
 */
int TTSequenceHeader::aspectRatio()
{
	return aspect_ratio_information;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Returns the aspect ratio code as string value
 */
QString TTSequenceHeader::aspectRatioText()
{
  QString szTemp;

  if ( aspect_ratio_information == 1 ) szTemp = "1:1";
  if ( aspect_ratio_information == 2 ) szTemp = "4:3";
  if ( aspect_ratio_information == 3 ) szTemp = "16:9";
  if ( aspect_ratio_information == 4 ) szTemp = "2.21:1";

  return szTemp;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Returns the frame rate as string value
 */
/* /////////////////////////////////////////////////////////////////////////////
 * Returns the frame rate value
 */
float TTSequenceHeader::frameRateValue()
{
  float value = 25.0;

  if ( frame_rate_code == 2 ) value = 24.0;
  if ( frame_rate_code == 3 ) value = 25.0;
  if ( frame_rate_code == 5 ) value = 30.0;

  if ( frame_rate_code < 2 || frame_rate_code > 5 )
    log->errorMsg(cName, __LINE__, "Couldn't determine the correct frame rate: assume 25 fps!");

  return value;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Returns the bit rate value in Kbit
 */
float TTSequenceHeader::bitRateKbit()
{
  return (float)bit_rate_value / 1000.0;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Returns the vbv buffer size value
 */
int TTSequenceHeader::vbvBufferSize()
{
  return vbv_buffer_size_value;
}


/* /////////////////////////////////////////////////////////////////////////////
 * TTSequenceEndHeader
 * Default constructor, extends TTMpeg2VideoHeader
 */
  TTSequenceEndHeader::TTSequenceEndHeader()
: TTMpeg2VideoHeader()
{
  header_start_code = sequence_end_code;
}

bool TTSequenceEndHeader::readHeader( TTFileBuffer* mpeg2_stream )
{
  header_offset = mpeg2_stream->position() - 4;

  return true;
}

bool TTSequenceEndHeader::readHeader( TTFileBuffer* mpeg2_stream, quint64 offset )
{
  mpeg2_stream->seekAbsolute( offset+4 );

  return readHeader( mpeg2_stream );
}

void TTSequenceEndHeader::parseBasicData( __attribute__ ((unused))quint8* data, __attribute__ ((unused))int offset )
{

}

/* /////////////////////////////////////////////////////////////////////////////
 * TTGOPHeader: Group of pictures header [000001B8]
 * Default constructor, extends TTMpeg2VideoHeader
 */
  TTGOPHeader::TTGOPHeader()
:TTMpeg2VideoHeader()
{
  header_start_code = group_start_code;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Read the GOP header from given stream
 */
bool TTGOPHeader::readHeader( TTFileBuffer* mpeg2_stream )
{
  quint8 header_data[4];

  try
  {
    mpeg2_stream->readByte( header_data,4 );

    header_offset = mpeg2_stream->position() - 8;
     parseBasicData( header_data );

    return true;
  }
  catch (TTFileBufferException)
  {
    return false;
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Read the GOP header at given offset
 */
bool TTGOPHeader::readHeader( TTFileBuffer* mpeg2_stream, quint64 offset )
{
  mpeg2_stream->seekAbsolute( offset+4 );

  return readHeader( mpeg2_stream );
}

/* /////////////////////////////////////////////////////////////////////////////
 * Parse the basic GOP header data
 */
void TTGOPHeader::parseBasicData( quint8* data, int offset )
{
  time_code.drop_frame_flag = (data[offset+0] >> 7) == 1;
  time_code.hours           = (int)((data[offset+0] & 0x7C) >> 2);
  time_code.minutes         = (int)(((data[offset+0] & 0x03) << 4) + ((data[offset+1] & 0xF0) >> 4));
  time_code.marker_bit      = ((data[offset+1] & 0x08) >> 3) == 1;
  time_code.seconds         = (int)(((data[offset+1] & 0x07) << 3) + ((data[offset+2] & 0xE0) >> 5));
  time_code.pictures        = (int)(((data[offset+2] & 0x1F) << 1) + ((data[offset+3] & 0x80) >> 7));
  closed_gop                = ((data[offset+3] & 0x40) >> 6) == 1;
  broken_link               = ((data[offset+3] & 0x20) >> 5) == 1;
}

/* /////////////////////////////////////////////////////////////////////////////
 * TTPicturesHeader: Pictures header [00000100]
 * Default constructor, extends TTMpeg2VideoHeader
 */
  TTPicturesHeader::TTPicturesHeader()
:TTMpeg2VideoHeader()
{
  header_start_code = picture_start_code;

  vbv_delay         = 0;
  picture_structure = 3;  // default: frame_picture (overridden by parseExtensionData if present)
}

/* /////////////////////////////////////////////////////////////////////////////
 * Read picture header from stream
 */
bool TTPicturesHeader::readHeader( TTFileBuffer* mpeg2_stream )
{
  quint8 header_data[5];

  try
  {
    mpeg2_stream->readByte( header_data, 4 );

    header_offset = mpeg2_stream->position() - 8;
    parseBasicData( header_data );

    // search for next start code (picture coding extension)
    // Limit search to 1024 bytes to avoid scanning entire file on corrupt data
    int count_zeros = 0;
    int searchLimit = 1024;
    quint8 value;
    do
    {
      mpeg2_stream->readByte(value);
      if ( value == 0x00 )
      {
        count_zeros++;
      }
      else if ( value != 1 )
      {
        count_zeros = 0;
      }
      if (--searchLimit <= 0) return false;
    }
    while ( value != 0x01 || count_zeros < 2 );

    mpeg2_stream->seekForward( 1 );
    mpeg2_stream->readByte( header_data, 5 );
    parseExtensionData( header_data );

    return true;
  }
  catch (TTFileBufferException)
  {
    return false;
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Read picture header at given offset.
 */
bool TTPicturesHeader::readHeader( TTFileBuffer* mpeg2_stream, quint64 offset )
{
  mpeg2_stream->seekAbsolute( offset+4 );
  return readHeader( mpeg2_stream );
}

/* /////////////////////////////////////////////////////////////////////////////
 * Parse basic picture header data.
 */
void TTPicturesHeader::parseBasicData( quint8* data, int offset )
{
  picture_coding_type = (int)((data[offset+1] & 0x38) >> 3);
  temporal_reference  = (int)((data[offset+0] << 2) + ((data[offset+1] & 0xC0) >> 6));
  vbv_delay           = ((data[offset+1] & 0x07) << 13) + (data[offset+2] << 5) + ((data[offset+3] & 0xF8) >> 3);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Parse picture coding extension data.
 */
void TTPicturesHeader::parseExtensionData( quint8* data, int offset )
{
  // picture_structure: bits 1-0 of byte 2 (per ISO/IEC 13818-2 6.2.3.1)
  picture_structure = data[offset+2] & 0x03;
  progressive_frame = ((data[offset+4] & 0x80) == 0x80);
  top_field_first   = ((data[offset+3] & 0x80) == 0x80);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Form an string representing the picture coding type.
 */
