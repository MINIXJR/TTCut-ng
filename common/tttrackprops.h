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
// TTTRACKPROPS
// Per-track name and Matroska track flags of an audio or subtitle track.
// ----------------------------------------------------------------------------

#ifndef TTTRACKPROPS_H
#define TTTRACKPROPS_H

#include <QString>
#include <QStringList>

#include <optional>

//! One Matroska track flag. Which flags a track type offers is up to the
//! track list and the muxer: visually impaired is audio-only, forced is
//! subtitle-only.
enum class TTTrackFlag { Original, VisualImpaired, HearingImpaired, Forced };

//! Where a prefilled value came from - shown as tooltip in the track lists.
enum class TTTrackPrefill { None, LanguageMis, LanguageQks, RecordingHearingImpaired };

//! Name and flags as read from a project file. An unset member means the
//! element was absent; a set but empty flags string means "no flag" and
//! still replaces the prefill.
struct TTSavedTrackProps
{
  std::optional<QString> name;
  std::optional<QString> flags;
};

struct TTTrackProps
{
  QString name;
  bool original        = false;
  bool visualImpaired  = false;
  bool hearingImpaired = false;
  bool forced          = false;
  TTTrackPrefill visualImpairedPrefill  = TTTrackPrefill::None;
  TTTrackPrefill hearingImpairedPrefill = TTTrackPrefill::None;
  TTTrackPrefill languagePrefill        = TTTrackPrefill::None; // LanguageMis/Qks: language set to deu

  bool flag(TTTrackFlag f) const
  {
    switch (f) {
      case TTTrackFlag::Original:        return original;
      case TTTrackFlag::VisualImpaired:  return visualImpaired;
      case TTTrackFlag::HearingImpaired: return hearingImpaired;
      case TTTrackFlag::Forced:          return forced;
    }
    return false;
  }

  void setFlag(TTTrackFlag f, bool on)
  {
    switch (f) {
      case TTTrackFlag::Original:        original        = on; break;
      case TTTrackFlag::VisualImpaired:  visualImpaired  = on; break;
      case TTTrackFlag::HearingImpaired: hearingImpaired = on; break;
      case TTTrackFlag::Forced:          forced          = on; break;
    }
  }

  //! Comma-separated set flags in a fixed order; "" if none (project file).
  QString flagsToken() const
  {
    QStringList t;
    if (original)        t << QStringLiteral("original");
    if (visualImpaired)  t << QStringLiteral("visual-impaired");
    if (hearingImpaired) t << QStringLiteral("hearing-impaired");
    if (forced)          t << QStringLiteral("forced");
    return t.join(QLatin1Char(','));
  }

  //! Replaces all four flags; unknown tokens are ignored, prefill reasons
  //! stay (they describe where the open-time value came from).
  void setFlagsFromToken(const QString& token)
  {
    const QStringList t = token.split(QLatin1Char(','), Qt::SkipEmptyParts);
    auto has = [&t](const char* s) {
      for (const QString& x : t)
        if (x.trimmed() == QLatin1String(s)) return true;
      return false;
    };
    original        = has("original");
    visualImpaired  = has("visual-impaired");
    hearingImpaired = has("hearing-impaired");
    forced          = has("forced");
  }

  void applySaved(const TTSavedTrackProps& s)
  {
    if (s.name)  name = *s.name;
    if (s.flags) setFlagsFromToken(*s.flags);
  }

  //! Broadcasters mark the German audio-description track "mis" and the
  //! ARD "Klare Sprache" track "qks" (a local-use code, ISO 639-2 qaa-qtz).
  //! Sets the matching flag and records both prefill reasons; changing the
  //! language to deu is the caller's job (the language lives on the list
  //! item), signalled by languagePrefill != None.
  void prefillFromAudioLanguage(const QString& lang)
  {
    if (lang == QLatin1String("mis")) {
      visualImpaired        = true;
      visualImpairedPrefill = TTTrackPrefill::LanguageMis;
      languagePrefill       = TTTrackPrefill::LanguageMis;
    } else if (lang == QLatin1String("qks")) {
      hearingImpaired        = true;
      hearingImpairedPrefill = TTTrackPrefill::LanguageQks;
      languagePrefill        = TTTrackPrefill::LanguageQks;
    }
  }

  void prefillHearingImpairedFromRecording()
  {
    hearingImpaired        = true;
    hearingImpairedPrefill = TTTrackPrefill::RecordingHearingImpaired;
  }
};

#endif // TTTRACKPROPS_H
