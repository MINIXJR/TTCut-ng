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
// TTAC3AUDIOHEADER
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

// Aufbau der AC3 AudioHeader:  (kein Anspruch auf Vollstaendigkeit)
// -----------------------------------------------------------------------------
// SyncWort: 0B 77    (00001011 01110111)   (mindestens 7 Byte)
//    16 Bit Prüfsumme
//     2 Bit Samplerate                    (0, 1, 2)
//     6 Bit Bitrate                       (0 - 37)
//     5 Bit Stream Identification         (01000 = 8)
//     3 Bit Mode                          (bsmode)
//     3 Bit ModeErweiterung               (acmode)
// -----------------------------------------------------------------------------

#ifndef TTAC3AUDIOHEADER_H
#define TTAC3AUDIOHEADER_H

#include "ttavheader.h"

class QString;

__attribute__ ((unused))static int AC3SampleRate[4] =
  {
    //samplerate : 0 bis 3
    48000, 44100, 32000, 0
  };

// bitrate * 1/1000 (!)
__attribute__ ((unused))static int AC3BitRate[38] =
  {
    //frmsizecod : 0 bis 37
      32,  32,  40,  40,   48,  48,  56,  56,  64,  64,
      80,  80,  96,  96,  112, 112, 128, 128, 160, 160,
      192, 192, 224, 224, 256, 256, 320, 320, 384, 384,
      448, 448, 512, 512, 576, 576, 640, 640
  };

__attribute__ ((unused))static int AC3FrameLength[4][38] =
{
   //samplerate : 0 bis 3, frmsizecod : 0 bis 37
{     64,   64,   80,   80,   96,   96,  112,  112,  128, 128,
     160,  160,  192,  192,  224,  224,  256,  256,  320, 320,
     384,  384,  448,  448,  512,  512,  640,  640,  768, 768,
     896,  896, 1024, 1024, 1152, 1152, 1280, 1280},

{     69,   70,   87,   88,  104,  105,  121,  122,  139,  140,
     174,  175,  208,  209,  243,  244,  278,  279,  348,  349,
     417,  418,  487,  488,  557,  558,  696,  697,  835,  836,
     975,  976, 1114, 1115, 1253, 1254, 1393, 1394},

{     96,   96,  120,  120,  144,  144,  168,  168,  192,  192,
     240,  240,  288,  288,  336,  336,  384,  384,  480,  480,
     576,  576,  672,  672,  768,  768,  960,  960, 1152, 1152,
    1344, 1344, 1536, 1536, 1728, 1728, 1920, 1920},

{    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
     0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
     0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
     0,    0,    0,    0,    0,    0,    0,    0}
};

// acmod -> Number of channels
__attribute__ ((unused))static int AC3AudioCodingMode[8] =
{
  2,1,2,3,3,4,4,5
};

__attribute__ ((unused))static const char* AC3Mode[8] =
{
  "1+1", "1/0", "2/0", "3/0",
  "2/1", "3/1", "2/2", "3/2"
  };


// -----------------------------------------------------------------------------
// *** TTAC3AudioHeader
// -----------------------------------------------------------------------------
class TTAC3AudioHeader : public TTAudioHeader
{
public:
  TTAC3AudioHeader();

  QString& descString();
  QString& modeString();
  int      bitRate();
  QString& bitRateString();
  int      sampleRate();
  QString& sampleRateString();

  //private:
  int     crc1;
  quint8  fscod;
  quint8  frmsizecod;
  quint16 syncframe_words;
  quint8  bsid;
  quint8  bsmod;
  quint8  acmod;
  bool    lfeon;
};

#endif //TTAC3AUDIOHEADER_H

