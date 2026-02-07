/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : tttranscode.h                                                   */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 08/07/2005 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTTRANSCODE
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

#ifndef TTTRANSCODE_H
#define TTTRANSCODE_H

#include "../common/ttmessagelogger.h"
#include "../common/istatusreporter.h"
#include "../common/ttcut.h"
#include "ttencodeparameter.h"

#include <QFileInfo>

struct AVCodecContext;

class TTVideoStream;
class TTEncodeParameter;

class TTTranscodeProvider : public IStatusReporter
{
  Q_OBJECT

  public:
    TTTranscodeProvider(TTEncodeParameter& enc_par);
    ~TTTranscodeProvider();

    bool encodePart(TTVideoStream* vStream, int start, int end);

    TTEncodeParameter parameter() { return enc_par; }

  private:
    bool setupEncoder();
    void freeEncoder();
    bool encodeFrames(TTVideoStream* vs, int start, int end);

    TTMessageLogger*  log;
    TTEncodeParameter enc_par;
    AVCodecContext*    mEncoder;
};

#endif //TTTRANSCODE_H
