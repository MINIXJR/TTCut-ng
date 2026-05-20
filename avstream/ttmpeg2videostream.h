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
// TTMPEG2VIDEOSTREAM
// ----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Overview
// -----------------------------------------------------------------------------
//
//                               +- TTMpegAudioStream
//             +- TTAudioStream -|
//             |                 +- TTAC3AudioStream
// TTAVStream -|
//             |
//             +- TTVideoStream -TTMpeg2VideoStream
//
// -----------------------------------------------------------------------------

#ifndef TTMPEG2VIDEOSTREAM_H
#define TTMPEG2VIDEOSTREAM_H

#include "ttavstream.h"
#include "ttmpeg2videoheader.h"
#include "ttvideoindexlist.h"

#include "../common/ttmessagelogger.h"
#include "../extern/tttranscode.h"

#include <QList>
#include <QString>
#include <QFileInfo>
#include <QStack>

class TTAudioList;
class TTCutList;

// -----------------------------------------------------------------------------
// TTMpeg2VideoStream
// -----------------------------------------------------------------------------
class TTMpeg2VideoStream : public TTVideoStream
{
  Q_OBJECT

  public:
    TTMpeg2VideoStream( const QFileInfo &f_info );
    virtual ~TTMpeg2VideoStream();

    void makeSharedCopy( TTMpeg2VideoStream* v_stream );

    virtual int createHeaderList();
    virtual int createIndexList();

    virtual TTAVTypes::AVStreamType streamType() const;

    // Field-picture extra indices: second field of each top/bottom pair.
    // Empty for progressive sequences and frame-picture-only streams.
    const QList<int>& extraIndices() const { return mExtraIndices; }

    virtual bool isCutInPoint( int pos );
    virtual bool isCutOutPoint( int pos );

    virtual void cut(int start, int end, TTCutParameter* cp);

    TTVideoHeader* getCutStartObject(int cutInPos, int cutOutPos, TTCutParameter* cutParams);
    TTVideoHeader* getCutEndObject(int cutOutPos, TTCutParameter* cutParams);
    TTVideoHeader* checkIFrameSequence(int iFramePos, TTCutParameter* cutParams);
    void transferCutObjects(TTVideoHeader* startObject, TTVideoHeader* endObject, TTCutParameter* cutParams);
    void writeSequenceEndHeader();

    void rewriteGOP(quint8* buffer, quint64 absPos, TTGOPHeader* gop, bool closeGOP, TTCutParameter* cr);
    void removeOrphanedBFrames(QStack<TTBreakObject*>* breakObjects, TTVideoHeader* currentObject);
    void rewriteTempRefData(quint8* buffer, TTPicturesHeader* currentPicture, quint64 bufferStartOffset, int tempRefDelta);
    void encodePart( int start, int end, TTCutParameter* cr);

    //Test!
    bool    createHeaderListFromMpeg2();

  protected:
    bool    openStream();
    bool    closeStream();
    quint64 getByteCount(TTVideoHeader* startObject, TTVideoHeader* endObject);
    // log is inherited from TTAVStream — do not redeclare here.

  private:
    QList<int> mExtraIndices;
};

#endif //TTMPEG2VIDEOSTREAM_H
