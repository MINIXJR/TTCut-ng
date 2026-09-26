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

#ifndef TTMPEGAUDIOSTREAM_H
#define TTMPEGAUDIOSTREAM_H

#include "ttavstream.h"
#include "ttmpegaudioheader.h"

/* \brief This class represents an MPEG audio stream
 *
 */
class TTMPEGAudioStream : public TTAudioStream
{
  Q_OBJECT

 public:
  explicit TTMPEGAudioStream( const QFileInfo &f_info, int s_pos=0 );
  ~TTMPEGAudioStream() override;

  TTAVTypes::AVStreamType streamType() const override;

  void searchNextSyncByte();
  //! Fills audio_header from the three header bytes after the sync byte at
  //! data[offset]; frame_length 0 when they describe no frame. Needs no
  //! stream: TTAudioType probes sync candidates with it.
  static void parseAudioHeader( const quint8* data, int offset, TTMpegAudioHeader* audio_header );

  void    readAudioHeader( TTMpegAudioHeader* audio_header );

  int createHeaderList() override;
  int createIndexList() override {return 0;}

};

#endif //TTMPEGAUDIOSTREAM_H
