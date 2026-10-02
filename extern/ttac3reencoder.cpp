/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttac3reencoder.h"
#include "../avstream/ttbitstream.h"

// BSI syntax: ATSC A/52, section 5.3.2 and annex D (alternate bit stream
// syntax, bsid 6).
bool ttParseAc3FrameMeta(const uint8_t* data, int size, TTAc3FrameMeta* meta)
{
    if (!data || !meta || size < 8 || data[0] != 0x0B || data[1] != 0x77)
        return false;

    TTBitReader br(data + 5, size - 5);   // behind syncword, crc1, fscod/frmsizecod
    TTAc3FrameMeta m;
    m.bsid = int(br.bits(5));
    if (m.bsid > 8)
        return false;                     // 11..16 is E-AC3; 9, 10 are not AC3 either
    m.bsmod = int(br.bits(3));
    m.acmod = int(br.bits(3));
    if ((m.acmod & 1) && m.acmod != 1) m.cmixlev = int(br.bits(2));
    if (m.acmod & 4)                   m.surmixlev = int(br.bits(2));
    if (m.acmod == 2)                  m.dsurmod = int(br.bits(2));
    m.lfeon = br.flag();
    const int dialnorm = int(br.bits(5));
    m.dialnorm = dialnorm == 0 ? -31 : -dialnorm;   // code 0 is reserved and means -31
    if (br.flag()) br.skip(8);                      // compr
    if (br.flag()) br.skip(8);                      // langcod
    if (br.flag()) {                                // audprodie
        m.mixlevel = int(br.bits(5));
        m.roomtyp  = int(br.bits(2));
    }
    if (m.acmod == 0) {                             // 1+1: the second channel's set
        br.skip(5);
        if (br.flag()) br.skip(8);
        if (br.flag()) br.skip(8);
        if (br.flag()) br.skip(7);
    }
    m.copyright = int(br.bits(1));
    m.origbs    = int(br.bits(1));
    if (m.bsid == 6) {
        if (br.flag()) {                            // xbsi1e
            m.dmixmod       = int(br.bits(2));
            m.ltrtcmixlev   = int(br.bits(3));
            m.ltrtsurmixlev = int(br.bits(3));
            m.lorocmixlev   = int(br.bits(3));
            m.lorosurmixlev = int(br.bits(3));
        }
        if (br.flag()) {                            // xbsi2e
            m.dsurexmod    = int(br.bits(2));
            m.dheadphonmod = int(br.bits(2));
            m.adconvtyp    = int(br.bits(1));
            br.skip(9);                             // xbsi2, encinfo
        }
    }
    if (br.error())
        return false;
    *meta = m;
    return true;
}
