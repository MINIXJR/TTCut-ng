/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*                                                                            */
/* This program is distributed in the hope that it will be useful, but WITHOUT*/
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.                                          */
/* See the GNU General Public License for more details.                       */
/*                                                                            */
/* You should have received a copy of the GNU General Public License along    */
/* with this program; if not, write to the Free Software Foundation,          */
/* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.              */
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
