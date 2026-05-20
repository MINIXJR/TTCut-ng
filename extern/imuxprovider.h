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
// IMUXPROVIDER
// ----------------------------------------------------------------------------

#ifndef IMUXPROVIDER_H
#define IMUXPROVIDER_H

class TTMuxListData;

class IMuxProvider
{
  public:
    virtual ~IMuxProvider() {}

    virtual void writeMuxScript() = 0;
    virtual void mplexPart(int index) = 0;
};

#endif //IMuxProvider
