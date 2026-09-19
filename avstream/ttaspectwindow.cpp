/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttaspectwindow.h"

#include "ttavstream.h"
#include "ttavtypes.h"
#include "ttmpeg2videoheader.h"

#include <QMap>
#include <QVector>

TTAspectWindowInfo ttAnalyzeAspectWindow(TTVideoStream* stream, int cutIn, int cutOut)
{
  TTAspectWindowInfo info;
  if (!stream || stream->streamType() != TTAVTypes::mpeg2_demuxed_video) return info;
  if (!stream->headerList() || !stream->indexList()) return info;

  cutIn  = qMax(cutIn, 0);
  cutOut = qMin(cutOut, stream->frameCount() - 1);
  if (cutIn > cutOut) return info;

  // One header lookup per picture: 19-23 ms for all 126012 pictures of an
  // 84-minute recording (measured 2026-09-19), cheap enough for the hint
  // column's re-evaluation on every cut edit. getSequenceHeader() only reads
  // the lists; it does not move the stream's current index.
  QVector<int> aspects;
  aspects.reserve(cutOut - cutIn + 1);
  QMap<int, int> counts;
  for (int pos = cutIn; pos <= cutOut; ++pos) {
    TTSequenceHeader* seq = stream->getSequenceHeader(pos);
    const int aspect = seq ? seq->aspectRatio() : -1;
    aspects.append(aspect);
    if (aspect >= 0) counts[aspect]++;
  }
  info.cutInAspect  = aspects.first();
  info.cutOutAspect = aspects.last();

  int best = -1, bestCount = 0, secondCount = 0;
  for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
    if (it.value() > bestCount) {
      secondCount = bestCount;
      bestCount   = it.value();
      best        = it.key();
    } else if (it.value() > secondCount) {
      secondCount = it.value();
    }
  }
  // An exact tie has no majority. "The later part wins" would pick the
  // trailer at a cut-out, "the earlier part" the trailer at a cut-in.
  if (best < 0 || bestCount == secondCount) return info;
  info.mainAspect = best;

  if (info.cutInAspect != best) {
    for (int i = 0; i < aspects.size(); ++i)
      if (aspects[i] == best) { info.cutInTarget = cutIn + i; break; }
  }
  if (info.cutOutAspect != best) {
    for (int i = aspects.size() - 1; i >= 0; --i)
      if (aspects[i] == best) { info.cutOutTarget = cutIn + i; break; }
  }
  return info;
}
