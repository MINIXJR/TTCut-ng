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
// TTVIDEOHEADERLIST
// ----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Overview
// -----------------------------------------------------------------------------
//
//               +- TTAudioHeaderList 
//               | 
//               +- TTAudioIndexList
// TTHeaderList -|
//               +- TTVideoHeaderList
//               |
//               +- TTVideoIndexList
//
// -----------------------------------------------------------------------------

#ifndef TTVIDEOHEADERLIST_H
#define TTVIDEOHEADERLIST_H

#include "ttheaderlist.h"
#include "ttmpeg2videoheader.h"

class TTSequenceHeader;
class TTPicturesHeader;
class TTGOPHeader;

/* /////////////////////////////////////////////////////////////////////////////   
 * TTVideoHeaderList: Pointer list MPEG2 header objects
 */
class TTVideoHeaderList : public TTHeaderList
{
  public:
    TTVideoHeaderList(int size);
    virtual ~TTVideoHeaderList();

    quint8            headerTypeAt(int index);
    TTVideoHeader*    headerAt(int index);
    TTVideoHeader*    getPrevHeader(int startPos, TTMpeg2VideoHeader::mpeg2StartCodes type = TTMpeg2VideoHeader::ndef);
    TTVideoHeader*    getNextHeader(int startPos, TTMpeg2VideoHeader::mpeg2StartCodes type = TTMpeg2VideoHeader::ndef);
    TTVideoHeader*    getPrevHeader(TTVideoHeader* current, TTMpeg2VideoHeader::mpeg2StartCodes type = TTMpeg2VideoHeader::ndef);
    TTVideoHeader*    getNextHeader(TTVideoHeader* current, TTMpeg2VideoHeader::mpeg2StartCodes type = TTMpeg2VideoHeader::ndef);
    TTSequenceHeader* sequenceHeaderAt(int index);
    TTSequenceHeader* firstSequenceHeader();
    TTPicturesHeader* pictureHeaderAt(int index);
    TTGOPHeader*      gopHeaderAt(int index);

    int headerIndex(TTVideoHeader* current);

  protected:
    virtual void sort();
};
#endif //TTVIDEOHEADERLIST_H
