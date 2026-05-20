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

#include "ttac3audioheader.h"

#include <QString>

const char c_name[] = "TTAC3HEADER   : ";

TTAC3AudioHeader::TTAC3AudioHeader()
  : TTAudioHeader()
{
    str_description = "AC-3";
    str_mode        = "unknown";
}


QString& TTAC3AudioHeader::descString()
{
  return str_description;
}

QString& TTAC3AudioHeader::modeString()
{
  //QString num_string;

  //num_string.setNum(AC3AudioCodingMode[acmod]);
  
  //TODO: Question, is AC3 mode correct ???

  //qDebug( "%sAC3 mode: %d",c_name,acmod );

  str_mode = QString("%1-%2%3").arg(AC3Mode[acmod]).arg(AC3AudioCodingMode[acmod]).arg((lfeon == 0) ? ".0" : ".1");

  //str_mode = AC3Mode[acmod];
  //str_mode += "-";
  //str_mode += num_string;
  //str_mode += (lfeon == 0) ? ".0" : ".1";

  return str_mode;
}

int TTAC3AudioHeader::bitRate()
{
  if (frmsizecod >= 38) return 0;
  return 1000*AC3BitRate[frmsizecod];
}

QString& TTAC3AudioHeader::bitRateString()
{
  str_bit_rate = QString("%1 KBit/s").arg(bitRate());

  return str_bit_rate;
}

int TTAC3AudioHeader::sampleRate()
{
  return AC3SampleRate[fscod];
}

QString& TTAC3AudioHeader::sampleRateString()
{
  str_sample_rate = QString("%1").arg(sampleRate());
  
  return str_sample_rate;
}

