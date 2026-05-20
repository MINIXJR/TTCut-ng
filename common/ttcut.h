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
// TTCUT
// ----------------------------------------------------------------------------


#include "../avstream/ttcommon.h"

#ifndef TTCUT_H
#define TTCUT_H

class QPixmap;
class QString;
class QWidget;
class QStringList;

class TTCut
{
 public:
   // Static-only utility class — instances are not meaningful.
   TTCut() = delete;
   ~TTCut() = delete;


  // icons
   static QPixmap* imgDownArrow;
   static QPixmap* imgUpArrow;
   static QPixmap* imgDelete;
   static QPixmap* imgFileOpen24;
   static QPixmap* imgFileNew;
   static QPixmap* imgFileOpen;
   static QPixmap* imgFileSave;
   static QPixmap* imgFileSaveAs;
   static QPixmap* imgSaveImage;
   static QPixmap* imgSettings;
   static QPixmap* imgSettings18;
   static QPixmap* imgExit;
   static QPixmap* imgPlay;
   static QPixmap* imgStop;
   static QPixmap* imgSearch;
   static QPixmap* imgPreview;
   static QPixmap* imgCutAV;
   static QPixmap* imgCutAudio;
   static QPixmap* imgGoTo;
   static QPixmap* imgMarker;
   static QPixmap* imgClock;
   static QPixmap* imgApply;
   static QPixmap* imgAddToList;
   static QPixmap* imgFileClose;

   // for TTMpeg2
   static QPixmap* imgIFrame;
   static QPixmap* imgPFrame;
   static QPixmap* imgBFrame;

   // Version
   static QString versionString;

   // Audio-only cut output settings
   // Format: 0 = Original ES per track (stream-copy, multiple files)
   //         1 = Matroska Audio (.mka, single multi-track file, stream-copy)
   //         2 = MP3 (re-encode, one .mp3 per track)
   //         3 = AAC (re-encode, one .m4a per track)
   enum AudioOnlyFormat { AOF_OriginalES = 0, AOF_OriginalMKA = 1, AOF_MP3 = 2, AOF_AAC = 3 };

   // --------------------------------------------------------------
   // Global properties
   // --------------------------------------------------------------
   static QWidget*     mainWindow;

   // --------------------------------------------------------------
   // ISO 639 language support
   // --------------------------------------------------------------
   static const QStringList& languageCodes();    // {"und","deu","eng","fra",...}
   static const QStringList& languageNames();    // {"Undetermined","Deutsch","English",...}
   static QString iso639_1to2(const QString& code2);  // "de" → "deu"
   static QString normalizeLangCode(const QString& code);  // "de"/"ger"/"DEU" → "deu", unknown → ""

   // Extract a 3-letter ISO 639-2 language code from a filename of the form
   // "<base>_<lang>[_<n>].<ext>" (matches Show_deu.ac3 / Show_deu_1.ac3 /
   // Show_eng.srt). Falls back to the current system locale converted via
   // iso639_1to2 if no match. Used by TTAudioItem and TTSubtitleItem.
   static QString langFromFilename(const QString& filePath);

   // Populate a QComboBox with "<code> (<name>)" entries from
   // languageCodes() / languageNames() and select the entry whose userData
   // matches currentLang. Shared by TTAudioTreeView and TTSubtitleTreeView,
   // which previously each duplicated the loop. The caller wires the
   // currentIndexChanged signal — the row-lookup logic differs per view.
   static void populateLanguageCombo(class QComboBox* combo, const QString& currentLang);

   // Populate a QComboBox with the audio-only output formats (Original ES,
   // Matroska Audio, MP3, AAC) and select the entry matching the current
   // TTSettings audioOnlyFormat. Used by TTCutAVCutDlg and TTCutSettingsMuxer.
   static void populateAudioOnlyFormatCombo(class QComboBox* combo);
};


#endif
