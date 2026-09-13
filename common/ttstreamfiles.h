/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTSTREAMFILES_H
#define TTSTREAMFILES_H

#include <QFile>
#include <QString>
#include <QStringList>

#include "ttmessagelogger.h"

// Remove the elementary streams a mux has consumed ("delete ES after mux"),
// one debug line per file. Shared by the MKV path (TTAVData) and the mplex
// path (TTMplexProvider), which used to carry their own copies.
inline void ttRemoveElementaryStreams(const QString& videoFilePath,
                                      const QStringList& audioFilePaths,
                                      const QStringList& subtitleFilePaths = QStringList())
{
  TTMessageLogger* log = TTMessageLogger::getInstance();
  auto removeOne = [log](const char* kind, const QString& path) {
    const bool ok = QFile::remove(path);
    log->debugMsg(__FILE__, __LINE__, QString("Removing %1 stream %2 (%3)")
        .arg(QLatin1String(kind)).arg(path).arg(ok ? "ok" : "failed"));
  };
  removeOne("video", videoFilePath);
  for (const QString& path : audioFilePaths)    removeOne("audio", path);
  for (const QString& path : subtitleFilePaths) removeOne("subtitle", path);
}

#endif // TTSTREAMFILES_H
