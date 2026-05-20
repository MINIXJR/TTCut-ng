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
// TTVIDEOINDEXLIST
// ----------------------------------------------------------------------------

#ifndef TTVIDEOINDEXLIST_H
#define TTVIDEOINDEXLIST_H

#include <QVector>

#include "ttmpeg2videoheader.h"
#include "../common/ttmessagelogger.h"

// -----------------------------------------------------------------------------
// TTVideoIndexList: List of pointers to TTFrameIndex 
// -----------------------------------------------------------------------------
class TTVideoIndexList : public QVector<TTVideoIndex*>
{
 public:
  TTVideoIndexList();
  virtual ~TTVideoIndexList();

  void add(TTVideoIndex* index);
  void deleteAll();

  TTVideoIndex* videoIndexAt(int index);

  void sortDisplayOrder();
  bool isStreamOrder();
  bool isDisplayOrder();

  int moveToNextIndexPos(int start_pos, int frame_type=0);
  int moveToPrevIndexPos(int start_pos, int frame_type=0);
  int moveToIndexPos(int index, int frame_type=0);

  int  displayOrder(int index);
  int  streamOrder(int index);
  int  headerListIndex(int index);
  int  pictureCodingType(int index);
 
 protected:
  virtual void sort();
  void checkIndexRange(int index);

 protected:
  TTMessageLogger* log;
  int current_order;
};
#endif //TTVIDEOINDEXLIST_H


