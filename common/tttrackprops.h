/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTTRACKPROPS_H
#define TTTRACKPROPS_H

#include <QList>
#include <QString>

//! MKV properties of one audio or subtitle track besides language and delay:
//! the Matroska track name (TrackName) and the track flags. Written by
//! TTMkvMergeProvider as the stream's "title" metadata and AV_DISPOSITION_*
//! bits. Which flags the GUI offers depends on the track type (audio:
//! original, visual impaired; subtitle: original, hearing impaired, forced);
//! the muxer writes whatever is set.
struct TTTrackProps
{
  enum Flag {
    Original        = 0x01,   // FlagOriginal:        original-language track
    VisualImpaired  = 0x02,   // FlagVisualImpaired:  audio description
    HearingImpaired = 0x04,   // FlagHearingImpaired: SDH subtitles
    Forced          = 0x08,   // FlagForced:          always show
  };

  QString name;               // empty = no TrackName
  int     flags = 0;          // OR of Flag

  bool hasFlag(Flag f) const { return (flags & f) != 0; }
  void setFlag(Flag f, bool on) { flags = on ? (flags | f) : (flags & ~f); }
  bool isEmpty() const { return name.isEmpty() && flags == 0; }

  bool operator==(const TTTrackProps& o) const { return name == o.name && flags == o.flags; }
  bool operator!=(const TTTrackProps& o) const { return !(*this == o); }
};

using TTTrackPropsList = QList<TTTrackProps>;

#endif // TTTRACKPROPS_H
