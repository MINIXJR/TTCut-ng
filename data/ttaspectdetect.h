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

//! Why a sample was classified the way it was. Only the NoStatement reasons
//! carry diagnostic value; they are what the detail pane reports so a run
//! without a finding can be told apart from a run where the detection never
//! engaged.
enum class TTAspectReason {
  None,           //!< Pillarbox - nothing was rejected
  NoBars,         //!< NoPillarbox - picture fills the frame width
  BarsTooWide,    //!< NoStatement - bar wider than 3/16 of the width
  CentreTooDark,  //!< NoStatement - mean luminance between the bars <= 20
  Unusable        //!< NoStatement - null image, wrong format or smaller than 20x20
};

//! Classify one grayscale frame. luminanceThreshold is the per-pixel darkness
//! limit for a column to count as black (TTSettings::spPillarboxThreshold()).
//! When why is non-null it receives the reason behind the verdict.
TTAspectSample classifyAspectSample(const QImage& gray, int luminanceThreshold,
                                    TTAspectReason* why = nullptr);

//! A confirmed aspect-format transition.
struct TTAspectTransition {
  int  firstFrame;    //!< first frame of the run that established the new state
  bool toPillarbox;   //!< true: 16:9 -> 4:3pb, false: 4:3pb -> 16:9
};

//! A candidate run that never reached the hysteresis window.
struct TTAspectCandidate {
  int  firstFrame;    //!< first frame of the run
  bool toPillarbox;   //!< the state the run was heading for
  int  heldFrames;    //!< distance between its first and its last sample
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

  //! Hand out the candidate run that the last feed() broke, if any. Only runs
  //! that challenged the CONFIRMED state are recorded - flip-flopping inside
  //! the confirmed state is noise, not a near miss. Returns false when there
  //! is nothing pending; clears the slot on success.
  bool takeDiscardedCandidate(TTAspectCandidate& out);

private:
  int  mHysteresisFrames;
  bool mHaveState      = false;   //!< baseline established?
  bool mConfirmed      = false;   //!< last confirmed pillarbox state
  bool mCandidate      = false;   //!< current candidate state
  int  mCandidateFirst = 0;       //!< frame where the candidate run started
  int  mCandidateLast  = 0;       //!< last sample that continued the candidate
  bool mHaveDiscarded  = false;   //!< a broken candidate is waiting to be read
  TTAspectCandidate mDiscarded{}; //!< the broken candidate itself
};

#endif // TTASPECTDETECT_H
