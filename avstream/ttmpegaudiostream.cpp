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
// TTMPEGAUDIOSTREAM
// ----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Overview
// -----------------------------------------------------------------------------
//
//                               +- TTMpegAudioStream
//             +- TTAudioStream -|
//             |                 +- TTAC3AudioStream
// TTAVStream -|
//             |
//             +- TTVideoStream -TTMpeg2VideoStream
//
// -----------------------------------------------------------------------------


#include "ttmpegaudiostream.h"
#include "ttcommon.h"

#include <math.h>

// /////////////////////////////////////////////////////////////////////////////
// -----------------------------------------------------------------------------
// TTMPEGAudioStream
// -----------------------------------------------------------------------------
// /////////////////////////////////////////////////////////////////////////////

// constructor with file info and start position
// -----------------------------------------------------------------------------
TTMPEGAudioStream::TTMPEGAudioStream( const QFileInfo &f_info, int s_pos )
  : TTAudioStream( f_info, s_pos)
{
  log = TTMessageLogger::getInstance();
}

TTMPEGAudioStream::~TTMPEGAudioStream()
{
}

TTAVTypes::AVStreamType TTMPEGAudioStream::streamType() const
{
  return TTAVTypes::mpeg_audio;
}

// search next sync byte in stream
// -----------------------------------------------------------------------------
void TTMPEGAudioStream::searchNextSyncByte()
{
  quint8  byte2;

  stream_buffer->readByte( byte2 );

  while (!stream_buffer->atEnd() )
  {
    const quint8 byte1 = byte2;
    stream_buffer->readByte( byte2 );

    const quint16 sync_word = (byte1<<8) + byte2;

    if ((sync_word & 0xffe0) == 0xffe0)
    {
      stream_buffer->seekBackward(1);
      break;
    }
  }
}

// parse mpeg audio header data
// -----------------------------------------------------------------------------
void TTMPEGAudioStream::parseAudioHeader( const quint8* data, int offset, TTMpegAudioHeader* audio_header )
{
  audio_header->version            = (data[offset] & 0x18) >> 3;
  audio_header->layer              = (data[offset] & 0x06) >> 1;
  audio_header->protection_bit     = (data[offset] & 0x01) == 1;

  audio_header->bitrate_index      = (data[offset+1] & 0xf0) >> 4;
  audio_header->sampling_frequency = (data[offset+1] & 0x0c) >> 2;
  audio_header->padding_bit        = (data[offset+1] & 0x02) == 2;
  audio_header->private_bit        = (data[offset+1] & 0x01) == 1;

  audio_header->mode               = (data[offset+2] & 0xc0) >> 6;
  audio_header->mode_extension     = (data[offset+2] & 0x30) >> 4;
  audio_header->copyright          = (data[offset+2] & 0x08) == 8;
  audio_header->original_home      = (data[offset+2] & 0x04) == 4;
  audio_header->emphasis           = (data[offset+2] & 0x03);

  // Samples per frame (ISO 11172-3 / 13818-3): Layer I 384, Layer II 1152,
  // Layer III 1152 in MPEG-1 but 576 at the low sampling rates of MPEG-2/2.5.
  // Layer I counts 4-byte slots, the others bytes. The frame's duration is
  // samples / sample rate - NOT its byte length / bit rate: at 44.1 kHz the
  // byte length alternates with the padding bit, so the first header's
  // frame_time, which the cut grid is built on, was 26.083 or 26.125 ms
  // instead of 26.122 ms and every kept segment lost ~20 ms (audit run 11,
  // audio-es-input.md H1). The old byte formula also halved Layer I/II at
  // the low rates (H5).
  audio_header->frame_length = 0;
  audio_header->frame_time   = 0.0;

  int samples = 0;
  switch (audio_header->layer) {
    case 3: samples = 384;  break;                                   // Layer I
    case 2: samples = 1152; break;                                   // Layer II
    case 1: samples = (audio_header->version == 3) ? 1152 : 576; break; // Layer III
    default: break;                                                  // reserved
  }
  // Reserved version (1) or layer, or a corrupt header: no frame. Silent -
  // TTAudioType probes every sync-like byte pair of its first 64 KiB here.
  if (audio_header->version == 1 || samples == 0 ||
      audio_header->sampleRate() <= 0 || audio_header->bitRate() <= 0)
    return;

  const int slotBytes = (audio_header->layer == 3) ? 4 : 1;
  const int slotCount = samples / 8 / slotBytes * audio_header->bitRate() / audio_header->sampleRate();
  audio_header->frame_length = (slotCount + (audio_header->padding_bit ? 1 : 0)) * slotBytes;
  audio_header->frame_time   = samples * 1000.0 / audio_header->sampleRate();
}


// read one audio header from stream
// -----------------------------------------------------------------------------
bool TTMPEGAudioStream::readAudioHeader( TTMpegAudioHeader* audio_header )
{
  quint8 data[3];

  // read 3 byte from stream; readByte swallows the end of the file and
  // returns how many it got - the rest of data would be uninitialised
  if (stream_buffer->readByte( data, 3 ) < 3)
    return false;

  // audio header offset
  audio_header->setHeaderOffset( stream_buffer->position() - 4 );

  // parse current audio header and fill header struct
  parseAudioHeader( data, 0, audio_header );
  return true;
}

// read the header behind the sync word for createHeaderList()
// -----------------------------------------------------------------------------
TTAudioHeader* TTMPEGAudioStream::readHeader()
{
  TTMpegAudioHeader* audio_header = new TTMpegAudioHeader();
  if (!readAudioHeader( audio_header )) {
    delete audio_header;
    return nullptr;
  }
  return audio_header;
}
