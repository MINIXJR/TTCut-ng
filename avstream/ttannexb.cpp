/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttannexb.h"

int ttStartCodeLength(const QByteArray& nal)
{
    const uint8_t* p = reinterpret_cast<const uint8_t*>(nal.constData());
    if (nal.size() >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) return 4;
    if (nal.size() >= 3 && p[0] == 0 && p[1] == 0 && p[2] == 1) return 3;
    return 0;
}

int ttNextStartCode(const uint8_t* data, int size, int from, int* scLen)
{
    for (int i = from; i + 2 < size; ++i) {
        if (data[i] != 0 || data[i + 1] != 0) continue;
        int sc = 0;
        if (data[i + 2] == 1) sc = 3;
        else if (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1) sc = 4;
        if (sc == 0 || i + sc >= size) continue;
        *scLen = sc;
        return i;
    }
    return -1;
}

int ttNalEnd(const uint8_t* data, int size, int from)
{
    for (int i = from; i + 2 < size; ++i) {
        if (data[i] == 0 && data[i + 1] == 0 && (data[i + 2] == 0 || data[i + 2] == 1))
            return i;
    }
    return size;
}
