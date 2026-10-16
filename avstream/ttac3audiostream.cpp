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
// TTAC3AUDIOSTREAM
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


#include "ttac3audiostream.h"
#include "ttcommon.h"

// /////////////////////////////////////////////////////////////////////////////
// -----------------------------------------------------------------------------
// *** TTAC3AudioStream: AC3 audio stream class
// -----------------------------------------------------------------------------
// /////////////////////////////////////////////////////////////////////////////

//! Constructor with QFileInfo and start position
TTAC3AudioStream::TTAC3AudioStream(const QFileInfo &f_info, int s_pos)
                 :TTAudioStream(f_info, s_pos)
{
  log = TTMessageLogger::getInstance();
}

//! Destructor
TTAC3AudioStream::~TTAC3AudioStream()
{
}

//! Return the stream type
TTAVTypes::AVStreamType TTAC3AudioStream::streamType() const
{
  return TTAVTypes::ac3_audio;
}

//! Search the next sync byte in stream
void TTAC3AudioStream::searchNextSyncByte()
{
  quint8  byte2;

  stream_buffer->readByte(byte2);

  while ( !stream_buffer->atEnd() )
  {
    const quint8 byte1 = byte2;
    stream_buffer->readByte( byte2 );
    const quint16 sync_word = (byte1<<8) + byte2;

    if (sync_word == 0x0B77)
      break;
  }
}

//! Read audio header information from stream
void TTAC3AudioStream::readAudioHeader( TTAC3AudioHeader* audio_header)
{
  // AC3 frame layout up to and including the bytes we just read:
  //   [2 bytes sync word][6 bytes header data]
  // The 2-byte sync word is consumed in searchNextSyncByte() before this
  // function runs; the 6 bytes of header are read below. The header offset
  // points back to the start of the sync word.
  static constexpr int kAC3SyncBytes   = 2;
  static constexpr int kAC3HeaderBytes = 6;

  quint8* daten = new quint8[kAC3HeaderBytes];
  quint16 stuff;

  stream_buffer->readByte(daten, kAC3HeaderBytes);

  audio_header->setHeaderOffset( stream_buffer->position() - (kAC3SyncBytes + kAC3HeaderBytes) );

  audio_header->fscod           = (quint8)((daten[2]&0xc0)>>6);
  audio_header->frmsizecod      = (quint8)(daten[2]&0x3f);

  // Guard against OOB: AC3FrameLength is [4][38], fscod 3 is reserved, frmsizecod >= 38 is invalid
  if (audio_header->fscod >= 3 || audio_header->frmsizecod >= 38) {
    audio_header->syncframe_words = 0;
  } else {
    audio_header->syncframe_words = AC3FrameLength[audio_header->fscod][audio_header->frmsizecod];
  }
  audio_header->frame_length    = audio_header->syncframe_words*2;
  // 1536 samples per AC3 frame: the duration is samples / sample rate, not
  // the byte length / bit rate, which alternates at 44.1 kHz (69/70-word
  // frames) - see TTMPEGAudioStream::parseAudioHeader (audit run 11, H1).
  audio_header->frame_time      = (audio_header->sampleRate() > 0)
                                  ? 1536.0 * 1000.0 / audio_header->sampleRate() : 0.0;
  audio_header->bsid            = (quint8)(daten[3]>>3);
  audio_header->bsmod           = (quint8)(daten[3] & 0x07);
  audio_header->acmod           = (quint8)(daten[4]>>5);

  //log->debugMsg(__FILE__, __LINE__, QString("sbit rate %1 sframe length %2").
  //    arg(audio_header->bitRate()).
  //    arg(audio_header->frame_length));
  
  stuff = (quint16)((daten[4]<<8)+daten[5]);
  stuff <<= 3;

  if ((audio_header->acmod & 0x01)!=0 && audio_header->acmod!=1)
    stuff<<=2;
  if ((audio_header->acmod & 0x04)!=0)
    stuff<<=2;
  if (audio_header->acmod == 0x02)
    stuff<<=2;

  audio_header->lfeon=(stuff&0x8000)!=0;

  delete [] daten;
}

//! Read the header behind the sync word for createHeaderList()
TTAudioHeader* TTAC3AudioStream::readHeader()
{
  TTAC3AudioHeader* audio_header = new TTAC3AudioHeader();
  readAudioHeader( audio_header );
  return audio_header;
}

//! E-AC3 (DD+) uses the same 0x0B77 sync but a different frame size
//! encoding: bsid > 10 is a stream this parser cannot handle.
bool TTAC3AudioStream::endsHeaderList(const TTAudioHeader* header)
{
  const auto* ac3_header = static_cast<const TTAC3AudioHeader*>(header);
  if (ac3_header->bsid <= 10)
    return false;

  log->warningMsg(__FILE__, __LINE__,
      QString("E-AC3 stream detected (bsid=%1) - not supported, skipping").arg(ac3_header->bsid));
  return true;
}
