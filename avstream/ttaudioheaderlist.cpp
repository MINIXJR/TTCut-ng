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

bool audioHeaderListCompareItems( const TTAVHeader* head_1, const TTAVHeader* head_2 );

TTAudioHeaderList::TTAudioHeaderList( int size )
  : TTHeaderList( size )
{

}

TTAudioHeader* TTAudioHeaderList::audioHeaderAt( int index )
{
  checkIndexRange(index);
    
  return static_cast<TTAudioHeader*>(at( index ));
}


void TTAudioHeaderList::sort()
{
  std::sort( begin(), end(), audioHeaderListCompareItems );
}

bool audioHeaderListCompareItems( const TTAVHeader* head_1, const TTAVHeader* head_2 )
{
  // by start time; compared as double - the former int of ms x 1000
  // overflowed after 35 minutes
  return static_cast<const TTAudioHeader*>(head_1)->abs_frame_time
       < static_cast<const TTAudioHeader*>(head_2)->abs_frame_time;
}
