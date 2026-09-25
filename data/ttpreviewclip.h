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
// TTPREVIEWCLIP
// ----------------------------------------------------------------------------

#ifndef TTPREVIEWCLIP_H
#define TTPREVIEWCLIP_H

#include <QPair>
#include <QString>
#include <QVector>

#include <functional>

class TTAVData;
class TTAVItem;
class TTCutList;
class TTESSmartCut;
class TTMkvMergeProvider;
class TTVideoStream;

//! The steps both preview clip producers share.
//!
//! Two of them exist and both are needed: TTCutPreviewTask builds the clips for
//! every cut on a worker thread, TTCutPreview rebuilds a SINGLE clip when the
//! user moves a cut edge in the dialog. What they must not do is drift apart —
//! a rebuilt clip that is muxed differently from the original ones looks right
//! and is not. Everything both of them do identically lives here.
//!
//! Deliberately GUI-free and thread-agnostic: no progress reporting, no abort
//! polling, no logging. The callers differ in all three, and the functions
//! return what a caller needs in order to log it in its own words.
//!
//! The audio cut is NOT among them, but for a different reason than it used to
//! be: both producers now go through TTAVData::cutAudioTracks, the spine every
//! final cut uses, so they agree by construction. What still differs is who
//! drives it - the task on its worker thread with cancellation, the rebuild
//! synchronously - which is why each keeps its own call.

//! Everything the clip production reads off cut entry 0.
struct TTPreviewSource
{
  TTAVItem*      avItem     = nullptr;
  TTVideoStream* vStream    = nullptr;
  QString        sourceFile;          //!< elementary stream path
  QString        suffix;              //!< its extension, lower case
  double         frameRate  = 0.0;    //!< stream rate, .info rate as fallback
  int            avOffsetMs = 0;      //!< A/V sync offset from the .info file
  bool           hasAudio   = false;
  QString        audioFile;           //!< track 0, empty unless hasAudio

  //! True when the frame rate had to be taken from the .info file because the
  //! stream reported none. The callers log that; nothing else depends on it.
  bool frameRateFromInfo = false;

  bool isValid() const { return vStream != nullptr; }
};

//! Resolve the source of a single clip's cut list. An empty list, or one whose
//! entry 0 carries no video stream, yields a default-constructed (invalid)
//! result.
TTPreviewSource ttResolvePreviewSource(TTCutList* clipCutList);

//! Delete every preview* file in the temp directory and return how many were
//! removed. Both producers do this before writing new clips, so a stale file
//! from an earlier run cannot be picked up as a result of this one.
int ttRemovePreviewFiles();

//! Frames of one preview window: half of TTSettings::cutPreviewSeconds() at
//! the stream's frame rate, so a transition clip (cut-out window + cut-in
//! window) is as long as the setting.
long ttPreviewFrames(TTVideoStream* vStream);

//! Preview window that shows the cut-in of the cut [\a cutIn, \a cutOut]:
//! [cutIn, cutIn + frames], the end moved forward past B-frames. It never
//! leaves the cut - a cut shorter than the window is shown whole, not with
//! the material behind it.
QPair<int, int> ttPreviewCutInWindow(TTVideoStream* vStream, int cutIn, int cutOut, long frames);

//! Preview window that shows the cut-out of the cut [\a cutIn, \a cutOut]:
//! [cutOut - frames, cutOut], the start moved back to the preceding IDR frame
//! if that still lies in the cut. Never starts before \a cutIn.
QPair<int, int> ttPreviewCutOutWindow(TTVideoStream* vStream, int cutIn, int cutOut, long frames);

//! Clips a preview list yields: the first cut-in, one per transition, the
//! last cut-out - count()/2 + 1 for two entries per cut. The one place that
//! knows this; the task, the dialog and the rebuild all count through it.
int ttPreviewClipCount(TTCutList* previewCutList);

//! Index of the cut-out entry clip \a clipIndex (> 0) starts with: the
//! preceding cut's second entry. The cut-in of the following cut, if the
//! clip is a transition, is the entry after it.
inline int ttPreviewCutOutEntry(int clipIndex) { return 2 * clipIndex - 1; }

//! Fill \a out with the cut entries clip \a iClip is built from: the first
//! cut-in, a cut-out/cut-in pair for every transition in between, or the last
//! cut-out. \a previewCutList is the shortened preview list (two entries per
//! cut), so the number of clips is count()/2 + 1 and \a iClip must be below it.
//! \a out is appended to, not cleared.
void ttBuildClipCutList(TTCutList* previewCutList, int iClip, TTCutList* out);

//! Apply the preview preset and, for H.264/H.265, inject the frame-granularity
//! display-order map (PAFF-safe). Returns the number of map entries injected,
//! or -1 when the stream is not an H.26x stream and nothing was injected.
//!
//! Call it before or after TTESSmartCut::initialize(), whichever suits: the
//! preset is read when the encoder is set up, and initialize()'s cleanup() does
//! not clear it. initialize() itself stays with the caller — the three call
//! sites differ in how they register the engine for cancellation and in what
//! they report when it fails.
int ttApplyPreviewEncoderSettings(TTESSmartCut& smartCut, const TTPreviewSource& src);

//! Configure the muxer for a preview clip. \a displayOrder is the Smart Cut
//! output order for H.264/H.265 and stays empty for MPEG-2, whose display
//! order the muxer derives from temporal_reference itself.
void ttConfigurePreviewMux(TTMkvMergeProvider& mux, const TTPreviewSource& src,
                           const QVector<int>& displayOrder = QVector<int>());

//! Phase a rebuild has reached, reported to the caller's progress callback.
//!
//! A phase, not a message: the wording belongs to the dialog, and moving the
//! strings down here would move their translation context with them and void
//! the existing entries in trans/ttcut-ng_de_DE.ts.
enum class TTPreviewStage
{
  CutVideo,        //!< MPEG-2 video cut
  SmartCutVideo,   //!< H.264/H.265 Smart Cut
  CutAudio,
  Mux
};

using TTPreviewProgressFn = std::function<void(TTPreviewStage)>;

//! Rebuild ONE preview clip, the way the preview dialog does after the user
//! moved a cut edge. \a fileIndex is the preview_NNN number to overwrite,
//! \a clipCutList the entries that clip is built from (see ttBuildClipCutList).
//! Returns true when the clip was written.
//!
//! These two are the dialog's rebuild path, lifted out of TTCutPreview so it
//! can be driven without mpv and a GL context - which is what lets a harness
//! check that a rebuilt clip still matches (tools/diag/test_preview_clip.cpp).
//! They are NOT what TTCutPreviewTask runs: it keeps its shared Smart Cut
//! engine, its cancellation and its planned audio cut. What the two have in
//! common are the functions above.
bool ttRebuildMpeg2PreviewClip(TTAVData* avData, TTCutList* clipCutList, int fileIndex,
                               const TTPreviewProgressFn& progress = {});

//! Same shape as the MPEG-2 sibling. The video goes through TTESSmartCut
//! directly; the audio goes through TTAVData::cutAudioTracks, the spine every
//! final cut uses, so a rebuilt clip sounds like the one it replaces.
bool ttRebuildSmartCutPreviewClip(TTAVData* avData, TTCutList* clipCutList, int fileIndex,
                                  const TTPreviewProgressFn& progress = {});

#endif // TTPREVIEWCLIP_H
