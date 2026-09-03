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
// TTTRANSCODE
// ----------------------------------------------------------------------------

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
    explicit TTTranscodeProvider(TTEncodeParameter& encPar);
    ~TTTranscodeProvider();

    bool encodePart(TTVideoStream* vStream, int start, int end);


  private:
    bool setupEncoder();
    void freeEncoder();
    bool encodeFrames(TTVideoStream* vs, int start, int end);

    TTMessageLogger*  mLog;
    TTEncodeParameter mEncPar;
    AVCodecContext*    mEncoder;
};

#endif //TTTRANSCODE_H
