/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttac3acmod.h"

#include "ttac3audioheader.h"
#include "ttaudioheaderlist.h"
#include "ttavstream.h"
#include "ttavtypes.h"

#include <QtGlobal>
#include <cmath>

TTAcmodInfo ttAnalyzeAcmodWindow(TTAudioStream* stream, double cutInSec, double cutOutSec)
{
  TTAcmodInfo info;
  if (!stream || stream->streamType() != TTAVTypes::ac3_audio) return info;
  TTAudioHeaderList* list = stream->headerList();
  if (!list || list->count() == 0) return info;

  auto headerAt = [list](int i) {
    return dynamic_cast<TTAC3AudioHeader*>(list->audioHeaderAt(i));
  };

  // Frame duration from the list itself (ms per AC3 frame, 32 at 48 kHz).
  const TTAC3AudioHeader* first = headerAt(0);
  if (!first) return info;
  double frameDurMs = first->frame_time;
  if (frameDurMs <= 0) frameDurMs = 32.0;

  // [floor(in), floor(out)) in frames - the cut pipeline's window: the frame
  // that starts before cutOut but ends after it is NOT part of the segment.
  const int last     = list->count() - 1;
  const int startIdx = qBound(0, static_cast<int>(std::floor(cutInSec  * 1000.0 / frameDurMs)),     last);
  const int endIdx   = qBound(0, static_cast<int>(std::floor(cutOutSec * 1000.0 / frameDurMs)) - 1, last);
  if (endIdx < startIdx) return info;

  int acmodCount[8] = {0};
  int sampled = 0;
  auto sample = [&](int from, int to) {   // inclusive
    for (int i = from; i <= to; ++i) {
      const TTAC3AudioHeader* h = headerAt(i);
      if (!h || h->acmod > 7) continue;
      acmodCount[h->acmod]++;
      ++sampled;
    }
  };
  // First and last kAcmodSampleFrames frames, each frame once: when the two
  // ranges would overlap, the window is sampled whole.
  const int headEnd   = qMin(startIdx + kAcmodSampleFrames - 1, endIdx);
  const int tailStart = qMax(endIdx - kAcmodSampleFrames + 1, startIdx);
  if (tailStart <= headEnd) {
    sample(startIdx, endIdx);
  } else {
    sample(startIdx, headEnd);
    sample(tailStart, endIdx);
  }
  if (sampled == 0) return info;

  int mainAcmod = 0, maxCount = 0;
  for (int i = 0; i < 8; ++i) {
    if (acmodCount[i] > maxCount) { maxCount = acmodCount[i]; mainAcmod = i; }
  }
  const TTAC3AudioHeader* hIn  = headerAt(startIdx);
  const TTAC3AudioHeader* hOut = headerAt(endIdx);
  info.mainAcmod   = mainAcmod;
  info.cutInAcmod  = hIn  ? hIn->acmod  : -1;
  info.cutOutAcmod = hOut ? hOut->acmod : -1;
  return info;
}
