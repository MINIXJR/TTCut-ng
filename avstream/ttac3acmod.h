/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTAC3ACMOD - majority acmod of a cut window, from the AC3 header list.
//
// One implementation for both consumers: the cut pipeline (per-segment target
// acmod for TTAudioCutter's normalisation) and the cut list's hint column
// (format-change icon). They used to carry their own copies - a sync-word
// file scan with a fixed 32 ms frame and an in-memory scan with a different
// sampling rule - which could disagree on the majority of one segment.
// ----------------------------------------------------------------------------
#ifndef TTAC3ACMOD_H
#define TTAC3ACMOD_H

class TTAudioStream;

struct TTAcmodInfo
{
  int mainAcmod   = -1;   // majority acmod of the window (-1: not AC3 / empty)
  int cutInAcmod  = -1;   // acmod of the first frame of the window
  int cutOutAcmod = -1;   // acmod of the last frame of the window
};

//! Number of frames sampled at each end of the window.
constexpr int kAcmodSampleFrames = 100;

//! Majority acmod of the AC3 frames in [cutInSec, cutOutSec) of `stream`.
//!
//! The window is the frame range [floor(cutIn / frameDur), floor(cutOut /
//! frameDur) - 1] - the cut pipeline's segment, a frame straddling cutOut is
//! not part of it - clamped to the header list; frameDur is the list's own
//! AC3 frame duration (32 ms at 48 kHz). The majority is taken over the first
//! and the last kAcmodSampleFrames frames of the window, each frame counted
//! once (a window shorter than 2 x kAcmodSampleFrames is sampled whole).
//! Returns defaults for a non-AC3 stream, a missing header list or an empty
//! window.
TTAcmodInfo ttAnalyzeAcmodWindow(TTAudioStream* stream, double cutInSec, double cutOutSec);

#endif // TTAC3ACMOD_H
