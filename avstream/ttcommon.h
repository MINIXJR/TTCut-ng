/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include <QString>
#include <QDateTime>

class TTTimeCode;

// These helpers return Qt classes (QString / QTime / TTTimeCode), so they
// cannot have C linkage — only POD-returning functions are eligible. The
// 'extern "C"' wrappers here were never legal; keep the names as plain C++
// free functions.
bool    ttAssigned( const void* pointer );
bool    ttFileExists( QString fName );
bool    ttDeleteFile( QString fName );
QString ttAddFileExt( QString fName, const char* cExt );
QString ttChangeFileExt( QString fName, const char* cExt );
QTime   ttMsecToTime( int msec );
QTime   ttMsecToTimeD( double msec );
QTime   ttFramesToTime(long lFrames, float fps);
long    ttTimeToFrames(QTime timeCode, float fps);
TTTimeCode ttFrameToTimeCode( int FrameNr, float fps);

// Frame-type label such as " [I]", " [P]", " [B]" for the position display
// in TTCurrentFrame / TTCutOutFrame / TTCutFrameNavigation. Returns an empty
// string for unknown types (instead of dropping the whole tag silently).
QString ttFrameTypeTag(int frameType);

#ifndef TTTIMECODE_H
#define TTTIMECODE_H

class TTTimeCode
{
 public:
  TTTimeCode();
  QString toString();

  bool drop_frame_flag;
  short hours;
  short minutes;
  short seconds;
  short pictures;
  bool marker_bit;

};
#endif
