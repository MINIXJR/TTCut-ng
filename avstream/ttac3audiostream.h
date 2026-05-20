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

#ifndef TTAC3AUDIOSTREAM_H
#define TTAC3AUDIOSTREAM_H

#include "../common/ttmessagelogger.h"
#include "ttavstream.h"
#include "ttac3audioheader.h"

/* \brief This class represents an AC3 audio stream object
 *
 */
class TTAC3AudioStream : public TTAudioStream
{
  Q_OBJECT

 public:
  TTAC3AudioStream(const QFileInfo &f_info, int s_pos=0);
  virtual ~TTAC3AudioStream();

  virtual TTAVTypes::AVStreamType streamType() const;

  void searchNextSyncByte();
  void readAudioHeader(TTAC3AudioHeader* audio_header);

  virtual void cut(int start, int end, TTCutParameter* cp);

  virtual int  createHeaderList();
  virtual int  createIndexList(){return 0;};
  QString      streamExtension();
  QTime        streamLengthTime();
};

#endif //TTAC3AUDIOSTREAM_H
