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
// *** TTAUDIOHEADERLIST
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

#ifndef TTAUDIOHEADERLIST_H
#define TTAUDIOHEADERLIST_H

#include "ttheaderlist.h"

class TTAudioHeaderList : public TTHeaderList
{
 public:
  TTAudioHeaderList( int size );

  TTAudioHeader* audioHeaderAt( int index );

  int searchTimeIndex( double s_time );

 protected:
  void sort();
};

#endif //TTAUDIOHEADERLIST_H
