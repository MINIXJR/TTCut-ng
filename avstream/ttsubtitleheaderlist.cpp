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
//               |
//               +- TTSubtitleHeaderList
//
// -----------------------------------------------------------------------------

#include "ttsubtitleheaderlist.h"

#include <algorithm>

bool subtitleHeaderListCompareItems( TTAVHeader* head_1, TTAVHeader* head_2 );

TTSubtitleHeaderList::TTSubtitleHeaderList( int size )
  : TTHeaderList( size )
{

}

TTSubtitleHeader* TTSubtitleHeaderList::subtitleHeaderAt( int index )
{
  checkIndexRange(index);
    
  return (TTSubtitleHeader*)at( index );
}

int TTSubtitleHeaderList::searchTimeIndex( int search_time )
{
  if (size() == 0) return -1;

  int abs_time = 0;
  TTSubtitleHeader* subtitle_header;
  int index = 0;

  do
  {
    subtitle_header = (TTSubtitleHeader*)at(index);
    abs_time = (int)(subtitle_header->endMSec());
    index++;
  }
  while ( abs_time < search_time && index < size());

  // return index of next subtitle, if search_time is after end of found subtitle
  return index-1;
}

void TTSubtitleHeaderList::sort()
{
  std::sort( begin(), end(), subtitleHeaderListCompareItems );
}

bool subtitleHeaderListCompareItems( TTAVHeader* head_1, TTAVHeader* head_2 )
{
  // the values for the display order of two items are compared
  int time1 = (int)((TTSubtitleHeader*)head_1)->startMSec();
  int time2 = (int)((TTSubtitleHeader*)head_2)->startMSec();

  return (time1 < time2);
}
