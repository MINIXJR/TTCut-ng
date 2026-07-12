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

#include "ttaudioheaderlist.h"

#include <algorithm>

bool audioHeaderListCompareItems( TTAVHeader* head_1, TTAVHeader* head_2 );

TTAudioHeaderList::TTAudioHeaderList( int size )
  : TTHeaderList( size )
{

}

TTAudioHeader* TTAudioHeaderList::audioHeaderAt( int index )
{
  checkIndexRange(index);
    
  return (TTAudioHeader*)at( index );
}


void TTAudioHeaderList::sort()
{
  std::sort( begin(), end(), audioHeaderListCompareItems );
}

bool audioHeaderListCompareItems( TTAVHeader* head_1, TTAVHeader* head_2 )
{
  // the values for the display order of two items are compared
  int time1 = (int)(((TTAudioHeader*)head_1)->abs_frame_time * 1000);
  int time2 = (int)(((TTAudioHeader*)head_2)->abs_frame_time * 1000);

  return (time1 < time2);
}
