/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTSEARCHTASK_LOGO_H
#define TTSEARCHTASK_LOGO_H

#include "ttsearchtask.h"

class TTLogoDetector;

class TTLogoSearchTask : public TTSearchTask
{
  Q_OBJECT
public:
  TTLogoSearchTask(const QString& videoFilePath,
                   TTAVTypes::AVStreamType streamType,
                   TTVideoIndexList* indexList,
                   TTVideoHeaderList* headerList,
                   int startPos, int direction, int frameCount,
                   const TTLogoDetector* detector,
                   float threshold,
                   const QList<TTFrameInfo>& preBuiltFrameIndex = QList<TTFrameInfo>());

protected:
  void operation() override;

private:
  const TTLogoDetector* mDetector;     // owned by main thread; const-only access
  float mThreshold;
  bool  mInitialLogoPresent = false;
};

#endif // TTSEARCHTASK_LOGO_H
