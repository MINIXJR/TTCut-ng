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
  TTMPEGAudioStream( const QFileInfo &f_info, int s_pos=0 );
  virtual ~TTMPEGAudioStream();

  TTAVTypes::AVStreamType streamType() const;

  void searchNextSyncByte();
  void parseAudioHeader( quint8* data, int offset, TTMpegAudioHeader* audio_header );

  virtual void cut(int start, int end, TTCutParameter* cp);

  void    readAudioHeader( TTMpegAudioHeader* audio_header );

  virtual int createHeaderList( );
  virtual int createIndexList(){return 0;};

  QString streamExtension();
  QTime   streamLengthTime();
  int     searchIndex( double s_time );
};

#endif //TTMPEGAUDIOSTREAM_H
