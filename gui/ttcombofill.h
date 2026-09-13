/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTCOMBOFILL_H
#define TTCOMBOFILL_H

#include <QComboBox>
#include <QString>

// Replace a combo box's items with a name table (common/ttencodernames.h);
// item index == table index.
inline void ttFillCombo(QComboBox* cb, const char* const* names, int count)
{
  cb->clear();
  for (int i = 0; i < count; ++i)
    cb->insertItem(i, QString::fromLatin1(names[i]));
}

#endif // TTCOMBOFILL_H
