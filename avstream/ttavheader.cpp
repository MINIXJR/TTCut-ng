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
// TTAVHEADER (abstract)
// TTAUDIOHEADER
// TTVIDEOHEADER
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

#include "ttavheader.h"

#include <qstring.h>

// /////////////////////////////////////////////////////////////////////////////
// -----------------------------------------------------------------------------
// *** TTAVHeader
// -----------------------------------------------------------------------------
// /////////////////////////////////////////////////////////////////////////////

// default constructor
// -----------------------------------------------------------------------------
TTAVHeader::TTAVHeader()
{
  str_description   = "unknown";
  str_mode          = "unknown";
  str_bit_rate      = "unknown";
  str_sample_rate   = "unknown";
  header_start_code = 0xFF;
  header_offset     = 0;
}

// destructor
// -----------------------------------------------------------------------------
TTAVHeader::~TTAVHeader()
{
  //qDebug("TTAVHeader destructor...");
}

// return header description string
// -----------------------------------------------------------------------------
const QString& TTAVHeader::descString()
{
  return str_description;
}

// return header mode string
// -----------------------------------------------------------------------------
const QString& TTAVHeader::modeString()
{
  return str_mode;
}

// return bit rate string
// -----------------------------------------------------------------------------
const QString& TTAVHeader::bitRateString()
{
  return str_bit_rate;
}

// return sample rate string
// -----------------------------------------------------------------------------
const QString& TTAVHeader::sampleRateString()
{
  return str_sample_rate;
}

// return header type (start code)
// -----------------------------------------------------------------------------
quint8 TTAVHeader::headerType()
{
  return header_start_code;
}

// return header offset in bytes
// -----------------------------------------------------------------------------
quint64 TTAVHeader::headerOffset() const
{
  return header_offset;
}

bool TTAVHeader::operator==(const TTAVHeader& test) const
{
  return (header_offset == test.header_offset);
}


// /////////////////////////////////////////////////////////////////////////////
// -----------------------------------------------------------------------------
// *** TTAudioHeader
// -----------------------------------------------------------------------------
// /////////////////////////////////////////////////////////////////////////////

// default constructor
// -----------------------------------------------------------------------------
TTAudioHeader::TTAudioHeader()
{
  position       = 0;
  frame_time     = 0.0;
  abs_frame_time = 0.0;
  frame_length   = 0;
}

// -----------------------------------------------------------------------------
// methods common for all audio header objects
// -----------------------------------------------------------------------------

// return bit rate
// -----------------------------------------------------------------------------
int TTAudioHeader::bitRate()
{
  return 0;
}

// return sample rate
// -----------------------------------------------------------------------------
int TTAudioHeader::sampleRate()
{
  return 0;
}


double TTAudioHeader::absFrameEndTime()
{
  return 0.0;
}


int TTAudioHeader::compareTo()
{
  return 0;
}

int TTAudioHeader::frameLength()
{
  return frame_length;
}


// /////////////////////////////////////////////////////////////////////////////
// -----------------------------------------------------------------------------
// *** TTVideoHeader
// -----------------------------------------------------------------------------
// /////////////////////////////////////////////////////////////////////////////

// default constructor
// -----------------------------------------------------------------------------
TTVideoHeader::TTVideoHeader()
{
}

// /////////////////////////////////////////////////////////////////////////////
// -----------------------------------------------------------------------------
// *** TTVideoIndex
// -----------------------------------------------------------------------------
// /////////////////////////////////////////////////////////////////////////////
void TTVideoIndex::setDisplayOrder(int value)
{
  display_order = value;
}

int TTVideoIndex::getDisplayOrder()
{
  return display_order;
}

void TTVideoIndex::setHeaderListIndex(int value)
{
  header_list_index = value;
}

int TTVideoIndex::getHeaderListIndex()
{
  return header_list_index;
}

void TTVideoIndex::setPictureCodingType(int value)
{
  picture_coding_type = value;
}

int TTVideoIndex::getPictureCodingType()
{
  return picture_coding_type;
}

// /////////////////////////////////////////////////////////////////////////////
// -----------------------------------------------------------------------------
// *** TTBreakObject
// -----------------------------------------------------------------------------
// /////////////////////////////////////////////////////////////////////////////
TTBreakObject::TTBreakObject()
{
  stop_object          = (TTVideoHeader*)NULL;
  restart_object       = (TTVideoHeader*)NULL;
  stop_object_index    = -1;
  restart_object_index = -1;
}

TTBreakObject::~TTBreakObject()
{
}

void TTBreakObject::setStopObject( TTVideoHeader* stop, int index )
{
  stop_object = stop;
  stop_object_index = index;
}


void TTBreakObject::setRestartObject( TTVideoHeader* restart, int index )
{
  restart_object = restart;
  restart_object_index = index;
}


TTVideoHeader* TTBreakObject::stopObject()
{
  return stop_object;
}


TTVideoHeader* TTBreakObject::restartObject()
{
  return restart_object;
}



// /////////////////////////////////////////////////////////////////////////////
// -----------------------------------------------------------------------------
// *** TTSubtitleHeader
// -----------------------------------------------------------------------------
// /////////////////////////////////////////////////////////////////////////////
TTSubtitleHeader::TTSubtitleHeader()
{
  mText = "";
  mStartMSec = 0;
  mEndMSec = 0;
}

QString TTSubtitleHeader::text()
{
  return mText;
}

void TTSubtitleHeader::setText(QString text)
{
  mText = text;
}

QTime TTSubtitleHeader::startTime()
{
  return QTime::fromMSecsSinceStartOfDay(mStartMSec);
}

int TTSubtitleHeader::startMSec()
{
  return mStartMSec;
}

void TTSubtitleHeader::setStartTime(QTime start)
{
  mStartMSec = start.msecsSinceStartOfDay();
}

void TTSubtitleHeader::setStartTime(int mSec)
{
  mStartMSec = mSec;
}

QTime TTSubtitleHeader::endTime()
{
  return QTime::fromMSecsSinceStartOfDay(mEndMSec);
}

int TTSubtitleHeader::endMSec()
{
  return mEndMSec;
}

void TTSubtitleHeader::setEndTime(QTime end)
{
  mEndMSec = end.msecsSinceStartOfDay();
}

void TTSubtitleHeader::setEndTime(int mSec)
{
  mEndMSec = mSec;
}

