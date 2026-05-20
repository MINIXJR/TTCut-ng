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
// TTCUTPARAMETER
// ----------------------------------------------------------------------------
#ifndef TTCUTPARAMETER_H
#define TTCUTPARAMETER_H

#include <qdatetime.h>

#include <QVector>

#include "../avstream/ttcommon.h"
#include "../avstream/ttfilebuffer.h"
#include "../avstream/ttvideoheaderlist.h"

/*//////////////////////////////////////////////////////////////////////////////
 * TTCutParameter
 */
class TTCutParameter
{
  public:
    TTCutParameter(TTFileBuffer* fBuffer);
    ~TTCutParameter();

    TTFileBuffer* getTargetStreamBuffer();
    bool getIsDVDCompliantStream();
    void setIsDVDCompliantStream(bool value);
    int  getNumPicturesWritten();
    void setNumPicturesWritten(int value);
    int  getCutInIndex();
    void setCutInIndex(int value);
    int  getCutOutIndex();
    void setCutOutIndex(int value);
    void firstCall();
    void lastCall();

  private:
    void writeSequenceEndHeader();

    TTFileBuffer*        targetStreamBuffer;
    bool                 isDVDCompliantStream;
    int                  cutInIndex;
    int                  cutOutIndex;
    int                  numPicturesWritten;
    float                dvdCompliantMaxBitrate;
    int                  dvdCompliantMaxFrames;
};
#endif //TTCUTPARAMETER_H
