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
// TTHEADERLIST
// ----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Overview
// -----------------------------------------------------------------------------
//
//               +- TTAudioHeaderList 
//               | 
// TTHeaderList -+
//               |
//               +- TTVideoHeaderList
//
// -----------------------------------------------------------------------------

#ifndef TTHEADERLIST_H
#define TTHEADERLIST_H

#include "ttavheader.h"
#include "../common/ttmessagelogger.h"

#include <QVector>

// -----------------------------------------------------------------------------
// TTHeaderList: Pointer list for TTAVHeader objects
// -----------------------------------------------------------------------------
class TTHeaderList : public QVector<TTAVHeader*>
{
 public:
  virtual ~TTHeaderList();

  virtual void add( TTAVHeader* header );
  virtual void deleteAll();

 protected:
  TTHeaderList(int size);
  virtual void sort() = 0;
  virtual void checkIndexRange(int index);

 protected:
  TTMessageLogger* log;
  int              initial_size;
};

#endif //TTHEADERLIST
