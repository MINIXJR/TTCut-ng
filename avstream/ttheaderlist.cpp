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

#include "ttheaderlist.h"

#include "../common/ttexception.h"

#include <QString>

const char c_name[] = "TTHEADERLIST: ";

/* /////////////////////////////////////////////////////////////////////////////
 * Create header list instance
 */
TTHeaderList::TTHeaderList(int size)
{
  log = TTMessageLogger::getInstance();
  initial_size = size;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Destructor: delete all header objects in list
 */
TTHeaderList::~TTHeaderList()
{
}

/* /////////////////////////////////////////////////////////////////////////////
 * add an header to the header list
 */
void TTHeaderList::add(TTAVHeader* header)
{
  if (header == NULL)
    qDebug("try to insert NULL header in list!");

  append(header);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Remove all items from the header list
 * The objects in the list were also deleted!
 */
void TTHeaderList::deleteAll()
{
  for (int i = 0; i < size(); i++)
  {
    TTAVHeader* av_header = at(i);
    if (av_header != NULL) {
      //remove(i);
      delete av_header;
    }
  }
  clear();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Check if given index is in list range
 */
void TTHeaderList::checkIndexRange(int index)
{
  if (index < 0 || index >= size())
  {
    QString msg = QString("Index %1 exceeds array bounds: %2").arg(index).arg(count());
    throw TTIndexOutOfRangeException(msg);
  }
}

