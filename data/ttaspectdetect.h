/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTASPECTDETECT_H
#define TTASPECTDETECT_H

#include <QImage>

//! Classification of one sampled frame for aspect-format detection.
enum class TTAspectSample {
  NoPillarbox,   //!< picture fills the frame width
  Pillarbox,     //!< black bars left and right, picture in between
  NoStatement    //!< black frame or failed decode - carries no aspect information
};

//! Classify one grayscale frame. luminanceThreshold is the per-pixel darkness
//! limit for a column to count as black (TTSettings::spPillarboxThreshold()).
TTAspectSample classifyAspectSample(const QImage& gray, int luminanceThreshold);

//! A confirmed aspect-format transition.
struct TTAspectTransition {
  int  firstFrame;    //!< first frame of the run that established the new state
  bool toPillarbox;   //!< true: 16:9 -> 4:3pb, false: 4:3pb -> 16:9
};

//! State machine over samples in display order. A candidate state must hold for
//! hysteresisFrames before a transition is reported; the initial state is never
//! reported; NoStatement samples are ignored entirely.
class TTAspectHysteresis
{
public:
  explicit TTAspectHysteresis(int hysteresisFrames);

  //! Feed one sample taken at display position pos. Returns true and fills out
  //! when this sample confirms a transition.
  bool feed(int pos, TTAspectSample sample, TTAspectTransition& out);

private:
  int  mHysteresisFrames;
  bool mHaveState      = false;   //!< baseline established?
  bool mConfirmed      = false;   //!< last confirmed pillarbox state
  bool mCandidate      = false;   //!< current candidate state
  int  mCandidateFirst = 0;       //!< frame where the candidate run started
};

#endif // TTASPECTDETECT_H
