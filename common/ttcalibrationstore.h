/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTCALIBRATIONSTORE_H
#define TTCALIBRATIONSTORE_H

#include <QHash>
#include <QString>

//! Cost-factor store for the progress estimator: milliseconds of computation
//! per work unit, keyed by calibration key (e.g. "audio/ac3", "mux/h26xcut").
//! Values are written by TTProgressEstimator at regular stage completion.
class ITTCalibrationStore
{
  public:
    virtual ~ITTCalibrationStore() {}

    //! Returns the stored factor, or a negative value if not calibrated.
    virtual double factor(const QString& key) const = 0;
    virtual void   setFactor(const QString& key, double msPerUnit) = 0;
};

//! In-memory implementation for tests.
class TTMemoryCalibrationStore : public ITTCalibrationStore
{
  public:
    double factor(const QString& key) const override { return mMap.value(key, -1.0); }
    void   setFactor(const QString& key, double v) override { mMap.insert(key, v); }

  private:
    QHash<QString, double> mMap;
};

//! QSettings-backed implementation (group progressCalibration/, same
//! persistence target as TTSettings: QSettings("TTCut-ng", "TTCut-ng")).
class TTSettingsCalibrationStore : public ITTCalibrationStore
{
  public:
    double factor(const QString& key) const override;
    void   setFactor(const QString& key, double msPerUnit) override;
};

#endif // TTCALIBRATIONSTORE_H
