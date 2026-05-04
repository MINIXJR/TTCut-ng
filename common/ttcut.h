/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2005                                                      */
/* FILE     : ttcut.h                                                         */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 03/01/2005 */
/* MODIFIED: b. altendorf                                    DATE: 03/19/2005 */
/* MODIFIED: b. altendorf                                    DATE: 03/23/2005 */
/* MODIFIED: b. altendorf                                    DATE: 03/31/2005 */
/* MODIFIED:                                                 DATE:            */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTCUT
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*                                                                            */
/* This program is distributed in the hope that it will be useful, but WITHOUT*/
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.                                          */
/* See the GNU General Public License for more details.                       */
/*                                                                            */
/* You should have received a copy of the GNU General Public License along    */
/* with this program; if not, write to the Free Software Foundation,          */
/* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.              */
/*----------------------------------------------------------------------------*/


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
   TTCut();
   ~TTCut();

 public:
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
   static QStringList languageCodes();    // {"und","deu","eng","fra",...}
   static QStringList languageNames();    // {"Undetermined","Deutsch","English",...}
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
