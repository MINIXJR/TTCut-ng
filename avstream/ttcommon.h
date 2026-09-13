/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTCOMMON_H
#define TTCOMMON_H

#include <QString>
#include <QList>
#include <algorithm>
#include <QDateTime>
#include <QStringList>

class TTMessageLogger;

class TTTimeCode;

// These helpers return Qt classes (QString / QTime / TTTimeCode), so they
// cannot have C linkage — only POD-returning functions are eligible. The
// 'extern "C"' wrappers here were never legal; keep the names as plain C++
// free functions.
bool    ttAssigned( const void* pointer );
bool    ttFileExists( QString fName );
bool    ttDeleteFile( QString fName );
// Removes every existing file in the list, logging each one that will not
// go; empty entries are skipped. Used by the abort-cleanup paths.
void    ttRemoveFiles( const QStringList& files, TTMessageLogger* log );
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

// Entries of an ascending index list strictly below `index` - the number of
// MPEG-2 field-picture extras that precede a position. One implementation
// for TTMpeg2VideoStream::extrasBefore (the bitstream parser's list) and
// TTAVData::countExtraFramesBefore (the audio-correction list).
inline int ttCountBelow(const QList<int>& ascending, int index)
{
  return int(std::lower_bound(ascending.begin(), ascending.end(), index) - ascending.begin());
}

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

#endif // TTCOMMON_H
