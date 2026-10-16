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
  explicit TTAC3AudioStream(const QFileInfo &f_info, int s_pos=0);
  ~TTAC3AudioStream() override;

  TTAVTypes::AVStreamType streamType() const override;

  //! False when the end of the file cut the header off.
  bool readAudioHeader(TTAC3AudioHeader* audio_header);

  int  createIndexList() override {return 0;}

 protected:
  // createHeaderList() steps: sync 0x0B77, 8 header bytes, E-AC3 ends the list
  void           searchNextSyncByte() override;
  int            headerBytes() const override { return 8; }
  TTAudioHeader* readHeader() override;
  bool           endsHeaderList(const TTAudioHeader* header) override;
};

#endif //TTAC3AUDIOSTREAM_H
