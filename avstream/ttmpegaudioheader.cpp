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
// TMPEGAUDIOHEADER
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

#include "ttmpegaudioheader.h"

#include <QString>

TTMpegAudioHeader::TTMpegAudioHeader()
: TTAudioHeader()
{
  if (str_description.isEmpty()) str_description = "unknown";
  if (str_mode.isEmpty())        str_mode        = "unknown";
  if (str_bit_rate.isEmpty())    str_bit_rate    = "unknown";
  if (str_sample_rate.isEmpty()) str_sample_rate = "unknown";
}

QString& TTMpegAudioHeader::descString()
{
  switch ( version )
  {
  case 0:
    str_description = "Mpeg 2.5";
    break;
  case 2:
    str_description = "Mpeg 2";
    break;
  case 3:
    str_description = "Mpeg 1";
    break;
  default:
    str_description = "reserved";
    break;
  }

  switch ( layer )
  {
  case 1:
    str_description.append( "-Layer 3" );
    break;
  case 2:
    str_description.append( "-Layer 2" );
    break;
  case 3:
    str_description.append( "-Layer 1" );
    break;
  default:
    str_description.append( "-undef." );
    break;
  }
  if (protection_bit==true) // Achtung: false=true!
    str_description.append( ",noCRC" );
  else
    str_description.append( ",CRC" );

  return str_description;
}

QString& TTMpegAudioHeader::modeString()
{
  switch ( mode )
  {
  case 0:
    str_mode = "stereo";
    break;
  case 1:
    str_mode = "joint stereo";
    break;
  case 2:
    str_mode = "dual channel";
    break;
  case 3:
    str_mode = "single channel";
    break;
  }

  return str_mode;
}

int TTMpegAudioHeader::bitRate()
{
  //qDebug( "version, layer, bitrate: %d/%d/%d:%d",version,layer,bitrate_index,mpeg_bit_raten[version][layer][bitrate_index] );
  return 1000*mpeg_bit_raten[version][layer][bitrate_index];
}
 
QString& TTMpegAudioHeader::bitRateString()
{
  str_bit_rate = QString("%1 KBit/s").arg(bitRate());
  //str_bit_rate->sprintf( "%d KBit/s", bitRate() );

  return str_bit_rate;
}

 int TTMpegAudioHeader::sampleRate()
{
  //qDebug( "version, sample_index: %d/%d:%d",version,sampling_frequency,mpeg_sample_raten[version][sampling_frequency] );
  int result = mpeg_sample_raten[version][sampling_frequency];

  if (result == 0)
  {
    qDebug( "version, sample_index: %d/%d:%d",version,sampling_frequency,mpeg_sample_raten[version][sampling_frequency] );
    result = 1;
  }

  return result;
}

 QString& TTMpegAudioHeader::sampleRateString()
 {
   str_sample_rate = QString("%1").arg(sampleRate());
   //str_sample_rate->sprintf( "%d",sampleRate() );

  return str_sample_rate;
 }


