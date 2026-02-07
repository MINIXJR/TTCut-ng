/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttencodeparameter.cpp                                           */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 11/07/2008 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTENCODEPARAMETER
// ----------------------------------------------------------------------------

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

#include "ttencodeparameter.h"
#include "common/ttmessagelogger.h"

/* /////////////////////////////////////////////////////////////////////////////
 * Print out the encode parameter for debug and logging purpose
 */
void TTEncodeParameter::print(char* prefix)
{
  TTMessageLogger* log = TTMessageLogger::getInstance();

  log->debugMsg( prefix, "----------------------------------------------------" );
  log->debugMsg( prefix, "encoder parameter:" );
  log->debugMsg( prefix, "----------------------------------------------------" );
  log->debugMsg( prefix, QString("avi-file        : %1").arg(aviFileInfo().absoluteFilePath()) );
  log->debugMsg( prefix, QString("mpeg-file       : %1").arg(mpeg2FileInfo().absoluteFilePath()) );
  log->debugMsg( prefix, QString("widthxheight    : %1x%2").arg(videoWidth()).arg(videoHeight()) );
  log->debugMsg( prefix, QString("aspect-code     : %1").arg(videoAspectCode()) );
  log->debugMsg( prefix, QString("bitrate         : %1").arg(videoBitrate()) );
  log->debugMsg( prefix, QString("max bitrate     : %1").arg(videoMaxBitrate()) );
  log->debugMsg( prefix, QString("framerate       : %1").arg(videoFPS()) );
  log->debugMsg( prefix, QString("interlaced      : %1").arg(videoInterlaced()) );
  log->debugMsg( prefix, QString("top field first : %1").arg(videoTopFieldFirst()) );
  log->debugMsg( prefix, "----------------------------------------------------" );
}

