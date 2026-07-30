/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttaspectdetect.h"

#include <QtGlobal>

namespace {

// A column counts as black when at least 90 % of the sampled rows are below
// the threshold. Every 2nd row is sampled, as in the original MPEG-2 scan.
bool isColumnBlack(const uchar* y, int stride, int col, int y0, int y1, int threshold)
{
  int total = 0;
  int black = 0;
  for (int row = y0; row < y1; row += 2) {
    total++;
    if (y[row * stride + col] < threshold) black++;
  }
  if (total == 0) return false;
  return (float)black / total >= 0.90f;
}

// Mean luminance of the rectangle between the bars, every 2nd pixel.
float centreMeanLuma(const uchar* y, int stride, int x0, int x1, int y0, int y1)
{
  long sum = 0;
  int  n   = 0;
  for (int row = y0; row < y1; row += 2) {
    const uchar* line = y + row * stride;
    for (int col = x0; col < x1; col += 2) { sum += line[col]; n++; }
  }
  return (n > 0) ? (float)sum / n : 0.0f;
}

} // namespace

TTAspectSample classifyAspectSample(const QImage& gray, int luminanceThreshold)
{
  if (gray.isNull() || gray.format() != QImage::Format_Grayscale8)
    return TTAspectSample::NoStatement;

  const int w = gray.width();
  const int h = gray.height();
  if (w < 20 || h < 20) return TTAspectSample::NoStatement;

  const uchar* y      = gray.constBits();
  const int    stride = gray.bytesPerLine();

  // Scan band: middle 40 % of the height. Minimum bar: 10 % of the width.
  const int y0     = (int)(h * 0.30f);
  const int y1     = (int)(h * 0.70f);
  const int minBar = w / 10;

  int leftBar = 0;
  for (int col = 0; col < w / 2; ++col) {
    if (isColumnBlack(y, stride, col, y0, y1, luminanceThreshold)) leftBar++;
    else break;
  }

  int rightBar = 0;
  for (int col = w - 1; col >= w / 2; --col) {
    if (isColumnBlack(y, stride, col, y0, y1, luminanceThreshold)) rightBar++;
    else break;
  }

  if (leftBar < minBar || rightBar < minBar)
    return TTAspectSample::NoPillarbox;

  // A bar that is far wider than any aspect conversion could produce is dark
  // picture content, not a bar. Pillarboxing 4:3 into 16:9 leaves
  // w * (1 - (4/3)/(16/9)) / 2 = w/8 on each side; measured bars sit close to
  // that (161 px and 174-177 px at 1280 wide, on two different recordings).
  // Dark scenes do not: a 45 s night sequence in "1994x05_-_Flemming_..."
  // produced 167/384, 391/364, 544/185, 318/211 and 137/276 and was reported
  // as a 4:3 segment.
  //
  // The limit is 1.5x the nominal bar. It has to leave room for the real
  // effect that widens a genuine bar - dark picture content directly against
  // it: the 4:3 section of "03x01_-_Drunter_und_drüber" holds its left bar at
  // 161 px while the right one grows to 212 px for over 10 s. Measured
  // largest genuine bar 212 px, smallest offending one 276 px; the limit sits
  // between them at 240 px.
  //
  // Such a frame carries no aspect information rather than proving the
  // absence of a pillarbox - hence NoStatement, which the hysteresis skips
  // instead of using it to break a run. Otherwise the bleed above would tear
  // a genuine 4:3 segment into three.
  const int maxBar = w * 3 / 16;
  if (leftBar > maxBar || rightBar > maxBar)
    return TTAspectSample::NoStatement;

  // Mirror of TTFFmpegWrapper::isFrameBlack, which ignores the outer 10 % and
  // calls a frame black at a mean luminance <= 20: a fully black frame would
  // otherwise read as pillarbox, because its bars meet in the middle. The
  // threshold is an image-domain value (swscale output, black == 0), not the
  // raw-Y video-range value (black ~ 16).
  const int cx0 = leftBar;
  const int cx1 = w - rightBar;
  if (cx1 - cx0 < minBar) return TTAspectSample::NoStatement;
  if (centreMeanLuma(y, stride, cx0, cx1, y0, y1) <= 20.0f)
    return TTAspectSample::NoStatement;

  return TTAspectSample::Pillarbox;
}

TTAspectHysteresis::TTAspectHysteresis(int hysteresisFrames)
  : mHysteresisFrames(qMax(1, hysteresisFrames))
{
}

bool TTAspectHysteresis::feed(int pos, TTAspectSample sample, TTAspectTransition& out)
{
  if (sample == TTAspectSample::NoStatement) return false;

  const bool isPillarbox = (sample == TTAspectSample::Pillarbox);

  // The first usable sample only establishes the baseline.
  if (!mHaveState) {
    mHaveState      = true;
    mConfirmed      = isPillarbox;
    mCandidate      = isPillarbox;
    mCandidateFirst = pos;
    return false;
  }

  // A different state starts a new candidate run.
  if (isPillarbox != mCandidate) {
    mCandidate      = isPillarbox;
    mCandidateFirst = pos;
    return false;
  }

  if (mCandidate != mConfirmed && (pos - mCandidateFirst) >= mHysteresisFrames) {
    mConfirmed      = mCandidate;
    out.firstFrame  = mCandidateFirst;
    out.toPillarbox = mCandidate;
    return true;
  }

  return false;
}
