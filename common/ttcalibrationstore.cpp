/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttcalibrationstore.h"

#include <QSettings>

double TTSettingsCalibrationStore::factor(const QString& key) const
{
  QSettings settings("TTCut-ng", "TTCut-ng");
  settings.beginGroup("progressCalibration");
  return settings.value(key, -1.0).toDouble();
}

void TTSettingsCalibrationStore::setFactor(const QString& key, double msPerUnit)
{
  QSettings settings("TTCut-ng", "TTCut-ng");
  settings.beginGroup("progressCalibration");
  settings.setValue(key, msPerUnit);
}
