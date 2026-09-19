/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTASPECTWINDOW - MPEG-2 aspect ratio at the edges of a cut vs. its majority.
//
// One implementation for the three consumers of the aspect hint: the cut
// list's hint column, the preview's jump row and the warning before the cut,
// so they cannot disagree. The MKV muxer takes the display aspect of the
// whole track from the first sequence header of the cut; a single stray
// picture at the start marks a 16:9 programme as 4:3.
// ----------------------------------------------------------------------------
#ifndef TTASPECTWINDOW_H
#define TTASPECTWINDOW_H

class TTVideoStream;

// Aspect values are MPEG-2 aspect_ratio_information codes (2 = 4:3, 3 = 16:9).
// Positions are display positions.
struct TTAspectWindowInfo
{
  int mainAspect   = -1;  // majority aspect of the window (-1: not MPEG-2, empty, or a tie)
  int cutInAspect  = -1;  // aspect of the picture at cutIn
  int cutOutAspect = -1;  // aspect of the picture at cutOut
  int cutInTarget  = -1;  // first position >= cutIn with mainAspect, -1 when cutIn already has it
  int cutOutTarget = -1;  // last position <= cutOut with mainAspect, -1 when cutOut already has it
};

//! Aspect of every picture in [cutIn, cutOut] (clamped to the stream), from
//! the sequence header that governs it in decode order - a leading B-picture
//! of an open GOP belongs to the header before its I-picture. Returns
//! defaults for a non-MPEG-2 stream, missing lists or an empty window; an
//! exact tie between two aspects leaves mainAspect and both targets at -1.
TTAspectWindowInfo ttAnalyzeAspectWindow(TTVideoStream* stream, int cutIn, int cutOut);

#endif // TTASPECTWINDOW_H
