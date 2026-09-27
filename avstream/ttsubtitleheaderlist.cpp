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

QString TTSubtitleHeaderList::textAt( int ms )
{
  QStringList texts;
  for (int i = qMax(0, searchTimeIndex(ms)); i < size(); ++i) {
    TTSubtitleHeader* header = subtitleHeaderAt(i);
    if (header->startMSec() > ms) break;
    if (header->endMSec() >= ms) texts << header->text();
  }
  return texts.join("\r\n");
}

void TTSubtitleHeaderList::sort()
{
  std::stable_sort(begin(), end(), [](const TTAVHeader* a, const TTAVHeader* b) {
    return static_cast<const TTSubtitleHeader*>(a)->startMSec()
         < static_cast<const TTSubtitleHeader*>(b)->startMSec();
  });
}
