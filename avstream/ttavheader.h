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
// TTVIDEOINDEX
// TTSUBTITLEHEADER
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
//             |
//             +- TTSubtitleHeader
//
// -----------------------------------------------------------------------------

#ifndef TTAVHEADER_H
#define TTAVHEADER_H

#include "ttcommon.h"
#include "../common/ttmessagelogger.h"
#include "ttfilebuffer.h"

#include <QString>
#include <QTime>

// -----------------------------------------------------------------------------
// *** TTAVHeader: Base class for all header objects
// -----------------------------------------------------------------------------
class TTAVHeader
{
public:
  TTAVHeader();
  virtual ~TTAVHeader();

  virtual const QString& descString();
  virtual const QString& modeString();
  virtual const QString& bitRateString();
  virtual const QString& sampleRateString();

  virtual quint8   headerType();
  virtual void     setHeaderType( quint8 h_type ) {header_start_code = h_type;}
  virtual quint64  headerOffset() const;
  virtual void     setHeaderOffset( quint64 h_offset ){header_offset = h_offset;}

  virtual bool operator==(const TTAVHeader& test) const;

protected:
  quint64          header_offset;
  quint8           header_start_code;
  QString          str_description;
  QString          str_mode;
  QString          str_bit_rate;
  QString          str_sample_rate;
  TTMessageLogger* log;
};


// -----------------------------------------------------------------------------
// *** TTAudioHeader: Base class for all audio header objects
// -----------------------------------------------------------------------------
class TTAudioHeader : public TTAVHeader
{
public:
  TTAudioHeader();

  virtual int     bitRate();
  virtual int     sampleRate();
  virtual double  absFrameEndTime();
  virtual int     compareTo();
  virtual int     frameLength();

  //protected:
  long   position;  // header offset ???
  float  frame_time;
  float  abs_frame_time;
  int    frame_length;
  int    bit_rate;
  int    sample_rate;
};

// -----------------------------------------------------------------------------
// *** TTVideoHeader: Base class for all video header objects
// -----------------------------------------------------------------------------
class TTVideoHeader : public TTAVHeader
{
public:
  TTVideoHeader();

  virtual bool readHeader(TTFileBuffer* mpeg2_stream) = 0;
  virtual bool readHeader(TTFileBuffer* mpeg2_stream, quint64 offset) = 0;
  virtual void parseBasicData(quint8* data, int offset=0) = 0;


 protected:
  typedef struct
  {
    bool    drop_frame_flag;
    int     hours;
    int     minutes;
    bool    marker_bit;
    int     seconds;
    int     pictures;
  } TTimeCode;
};

// -----------------------------------------------------------------------------
// TTVideoIndex: Object (data) for the video index list
// -----------------------------------------------------------------------------
class TTVideoIndex
{
 public:
   TTVideoIndex(){};

   void setDisplayOrder(int value);
   int  getDisplayOrder();
   void setHeaderListIndex(int value);
   int  getHeaderListIndex();
   void setPictureCodingType(int value);
   int  getPictureCodingType();

 protected:
   int display_order;
   int header_list_index;
   int picture_coding_type;
};


// -----------------------------------------------------------------------------
// TTBreakObject
// -----------------------------------------------------------------------------
class TTBreakObject
{
 public:
  TTBreakObject();
  ~TTBreakObject();

  void setStopObject( TTVideoHeader* stop, int index=-1 );
  void setRestartObject( TTVideoHeader* restart, int index=-1 );
  TTVideoHeader* stopObject();
  TTVideoHeader* restartObject();

 private:
  TTVideoHeader* stop_object;
  TTVideoHeader* restart_object;
  int stop_object_index;
  int restart_object_index;
};

// -----------------------------------------------------------------------------
// *** TTSubtitleHeader: Base class for all subtitle header objects
// -----------------------------------------------------------------------------
class TTSubtitleHeader : public TTAVHeader
{
 public:
  TTSubtitleHeader();

  QString text();
  void    setText(QString text);
  QTime   startTime();
  int     startMSec();
  void    setStartTime(QTime start);
  void    setStartTime(int mSec);
  QTime   endTime();
  int     endMSec();
  void    setEndTime(QTime end);
  void    setEndTime(int mSec);

 protected:
  QString mText;
  int     mStartMSec;
  int     mEndMSec;
};
#endif //TTAVHEADER_H


