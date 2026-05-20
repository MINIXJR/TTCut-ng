/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally (c) 2019 Minei3oat / github.com/Minei3oat                       */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// *** TTSUBTITLEHEADERLIST
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
//               |
//               +- TTSubtitleHeaderList
//
// -----------------------------------------------------------------------------

#ifndef TTSUBTITLEHEADERLIST_H
#define TTSUBTITLEHEADERLIST_H

#include "ttheaderlist.h"

class TTSubtitleHeaderList : public TTHeaderList
{
 public:
  TTSubtitleHeaderList( int size );

  TTSubtitleHeader* subtitleHeaderAt( int index );

  int searchTimeIndex( int search_time );

 protected:
  void sort();
};

#endif //TTSUBTITLEHEADERLIST_H
