/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTAUDIOCUTTER_H
#define TTAUDIOCUTTER_H

#include <functional>

#include <QList>
#include <QPair>
#include <QString>

#include "ttaudiorepair.h"

class TTAudioCutter
{
public:
    // Audio ES cutting - time-based stream-copy (ms-accurate)
    // If normalizeAcmod is true and targetAcmods is provided, frames with wrong acmod
    // at segment boundaries are re-encoded to match the target channel layout.
    // progressCb (optional) is called with 0..100, only on value changes,
    // strictly increasing, final value 100 (requires a known total duration).
    // shouldAbort (optional) is polled inside the per-segment packet loop; a
    // deliberate abort routes through the function's normal cleanup path,
    // returns false, and sets lastError() to "aborted by user" (distinguishable
    // from a real failure without a separate wasAborted() flag).
    // repairTable (optional) replaces the payload of any packet whose frame
    // number (packet time snapped to the 32 ms AC3 grid) is a key in the
    // table, writing the substitute bytes with the same PTS offset/accounting
    // as the stream-copy path and skipping the acmod re-encode check for that
    // frame entirely. Existing callers are unaffected (defaults to no lookup).
    bool cut(const QString& inputFile, const QString& outputFile,
             const QList<QPair<double, double>>& cutList,
             bool normalizeAcmod = false,
             const QList<int>& targetAcmods = QList<int>(),
             const std::function<void(int)>& progressCb = nullptr,
             const std::function<bool()>& shouldAbort = {},
             const TTAudioRepair::FrameTable* repairTable = nullptr);

    // Detect audio burst near a boundary (returns true if burst found).
    // A burst is reported when the PEAK of the boundary chunks exceeds the
    // surrounding level by at least minDeltaDb and clears the absolute
    // audibility floor. minDeltaDb comes from TTSettings::burstMinDeltaDb();
    // callers must not pass <= 0 (that means "detection off" and is handled
    // before the file is opened).
    // Sets burstRmsDb (peak) and contextRmsDb (median) only if a burst is found.
    static bool detectBurst(const QString& audioFile, double boundaryTime,
                            bool isCutOut, int minDeltaDb,
                            double& burstRmsDb, double& contextRmsDb);

    // AC3 acmod analysis - detect channel format changes at cut boundaries
    struct AcmodInfo {
        int mainAcmod;            // Majority acmod of segment (-1 if not AC3)
        int cutInAcmod;           // acmod at CutIn position
        int cutOutAcmod;          // acmod at CutOut position
    };

    static AcmodInfo analyzeAcmod(const QString& audioFile,
                                  double cutInTime, double cutOutTime);

    QString lastError() const { return mLastError; }

private:
    void setError(const QString& error);   // same body as TTFFmpegWrapper::setError
    QString mLastError;
};

#endif
