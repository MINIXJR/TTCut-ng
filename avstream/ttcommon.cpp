/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttcommon.h"
#include "../common/ttmessagelogger.h"

#include <math.h>
#include <QString>
#include <QFileInfo>

// check if pointer is assigned
bool ttAssigned( const void* pointer )
{
  if ( pointer != NULL )
    return true;
  else
    return false;
}

// change file extension from fName to cExt
QString ttChangeFileExt( QString fName, const char* cExt )
{
  // QFileInfo accepts a path string directly — no need to round-trip via QFile.
  QFileInfo fInfo( fName );
  QString   sNewFileName;
  uint len1, len2, len;

  QString sExt = fInfo.suffix();

  len1 = sExt.length();
  len2 = fName.length();

  len  = len2 - len1;

  sNewFileName = fName;
  sNewFileName.truncate(len);

  if ( len1 == 0 )
    sNewFileName += ".";

  sNewFileName += cExt;

  return sNewFileName;
}


// check if file fName exists
bool ttFileExists( QString fName )
{
  QFile file( fName );

  return file.exists();
}

// delete file fName physically
bool ttDeleteFile( QString fName )
{
  QFile file( fName );

  return file.remove();
}

// add file extension cExt to fName
QString ttAddFileExt( QString fName, const char* cExt )
{
  QString sNewFileName;
  sNewFileName  = fName;
  sNewFileName += '.';
  sNewFileName += cExt;

  return sNewFileName;
}

// convert msec to QTime
QTime ttMsecToTime( int msec )
{
  // Trivially defer to the double-arg variant; same arithmetic, no duplicate.
  return ttMsecToTimeD(static_cast<double>(msec));
}

QTime ttMsecToTimeD( double msec )
{
  QTime time;
  int hour, minute, second, msecond;

  //qDebug( "TTMSECTOSECD  : msec: %lf",msec );

  if ( trunc(msec) <= 0 )
  {
      hour    = 0;
      minute  = 0;
      second  = 0;
      msecond = 0;
  }
  else
  {
    hour    = (int)trunc(msec / 3600000.0);
    minute  = (int)trunc((msec - hour * 3600000.0) / 60000.0);
    second  = (int)trunc((msec - hour * 3600000.0 - minute * 60000.0) / 1000.0);
    msecond = (int)trunc(msec - hour * 3600000.0 - minute * 60000.0 - second * 1000.0);
  }

  time.setHMS( hour, minute, second, msecond );

  return time;
}

// convert number of frames to QTime
QTime ttFramesToTime(long lFrames, float fps)
{
  QTime time;
  int hour, minute, second, msecond;

  //qDebug("FramesToTime: fps: %f",fps);

  if ( fps < 1 )
    fps = 25.0;

  if ( lFrames <= 0 )
  {
      hour    = 0;
      minute  = 0;
      second  = 0;
      msecond = 0;
  }
  else
  {
    hour    = (int)trunc(lFrames / (3600 * fps));
    minute  = (int)trunc((lFrames - hour * 3600 * fps) / (60 * fps));
    second  = (int)trunc((lFrames - hour * 3600 * fps - minute * 60 * fps) / fps);
    msecond = (int)trunc((lFrames - hour * 3600 * fps - minute * 60 * fps - second * fps) * 1000.0/fps);
  }

  time.setHMS( hour, minute, second, msecond );

  return time;
}

// convert QTime to number of frames
long ttTimeToFrames(QTime timeCode, float fps)
{
   long Result;

   if (fps < 1 )
    fps = 25.0;

  if ( timeCode.msec() < 1000 )
  {
    Result = (long)trunc(timeCode.hour()   * 3600 * fps +
		   timeCode.minute() * 60   * fps +
		   timeCode.second() * fps);

    Result = Result + (long)trunc(timeCode.msec() / (1000 / fps));
  }
  else
    Result = -1;

  return Result;
}


TTTimeCode ttFrameToTimeCode( int frame_nr, float fps)
{
  int frames_per_second = (int)(fps+0.5);
  TTTimeCode tc;
 
  //if (useNTSCDropFrame && fps==30000m/1001m)
  //{
    // NTSC mit DropFrame-Korrektur
    //FrameNr+=2*((FrameNr/1800)-(FrameNr/18000)); // Jede Minute 2 Frames mehr ausser alle 10 Min.
    //tc.drop_frame_flag=true;
  //}
  //else
  tc.drop_frame_flag = false;
  
  tc.pictures = (short)(frame_nr%frames_per_second);

  QTime dt = ttFramesToTime( frame_nr, fps );

  tc.seconds    =(short)dt.second();
  tc.minutes    =(short)dt.minute();
  tc.hours      =(short)dt.hour();
  tc.marker_bit =true;
  
  return tc;
}


TTTimeCode::TTTimeCode()
{

}

QString ttFrameTypeTag(int frameType)
{
  switch (frameType) {
    case 1: return QStringLiteral(" [I]");
    case 2: return QStringLiteral(" [P]");
    case 3: return QStringLiteral(" [B]");
    default: return QString();
  }
}
