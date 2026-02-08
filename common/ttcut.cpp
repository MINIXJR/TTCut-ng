
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2005                                                      */
/* FILE     : ttcut.cpp                                                       */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 03/01/2005 */
/* MODIFIED: b. altendorf                                    DATE: 03/19/2005 */
/* MODIFIED: b. altendorf                                    DATE: 03/23/2005 */
/* MODIFIED: b. altendorf                                    DATE: 03/31/2005 */
/* MODIFIED: b. altendorf                                    DATE: 04/06/2007 */
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

#include "ttcut.h"

#include <QWidget>
#include <QDir>
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QLocale>

// /////////////////////////////////////////////////////////////////////////////
// -----------------------------------------------------------------------------
// Initialize static TTCut class members
// -----------------------------------------------------------------------------
// /////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// Pixmaps
// -----------------------------------------------------------------------------
QPixmap* TTCut::imgDownArrow  = NULL;
QPixmap* TTCut::imgUpArrow    = NULL;
QPixmap* TTCut::imgDelete     = NULL;
QPixmap* TTCut::imgFileOpen24 = NULL;
QPixmap* TTCut::imgFileNew    = NULL;
QPixmap* TTCut::imgFileOpen   = NULL;
QPixmap* TTCut::imgFileSave   = NULL;
QPixmap* TTCut::imgFileSaveAs = NULL;
QPixmap* TTCut::imgSaveImage  = NULL;
QPixmap* TTCut::imgSettings   = NULL;
QPixmap* TTCut::imgSettings18 = NULL;
QPixmap* TTCut::imgExit       = NULL;
QPixmap* TTCut::imgPlay       = NULL;
QPixmap* TTCut::imgStop       = NULL;
QPixmap* TTCut::imgSearch     = NULL;
QPixmap* TTCut::imgChapter    = NULL;
QPixmap* TTCut::imgPreview    = NULL;
QPixmap* TTCut::imgCutAV      = NULL;
QPixmap* TTCut::imgCutAudio   = NULL;
QPixmap* TTCut::imgGoTo       = NULL;
QPixmap* TTCut::imgMarker     = NULL;
QPixmap* TTCut::imgClock      = NULL;
QPixmap* TTCut::imgApply      = NULL;
QPixmap* TTCut::imgAddToList  = NULL;
QPixmap* TTCut::imgFileClose  = NULL;


QPixmap* TTCut::imgIFrame     = NULL;
QPixmap* TTCut::imgPFrame     = NULL;
QPixmap* TTCut::imgBFrame     = NULL;


// --------------------------------------------------------------
// common settings
// --------------------------------------------------------------

// Options
bool    TTCut::fastSlider      = false;
QString TTCut::tempDirPath     = QDir::tempPath();
QString TTCut::lastDirPath     = QDir::homePath();
QString TTCut::projectFileName = "";

// Preview
int TTCut::cutPreviewSeconds   = 25;
int TTCut::playSkipFrames      = 0;

// Frame search
int TTCut::searchLength   = 45;
int TTCut::searchAccuracy = 1;

// Navigation
int TTCut::stepSliderClick =  40;
int TTCut::stepPgUpDown    =  80;
int TTCut::stepArrowKeys   =   1;
int TTCut::stepPlusAlt     = 100;
int TTCut::stepPlusCtrl    = 200;
int TTCut::stepPlusShift   = 200;
int TTCut::stepQuickJump   =  25;
int TTCut::stepMouseWheel  = 120;

// Index files
bool TTCut::createVideoIDD = true;
bool TTCut::createAudioIDD = true;
bool TTCut::createPrevIDD  = false;
bool TTCut::createD2V      = false;
bool TTCut::readVideoIDD   = true;
bool TTCut::readAudioIDD   = true;
bool TTCut::readPrevIDD    = false;

 // Logfile
bool TTCut::createLogFile     = true;
bool TTCut::logModeConsole    = false;
bool TTCut::logModeExtended   = true;
bool TTCut::logVideoIndexInfo = false;
bool TTCut::logAudioIndexInfo = false;

// Recent files
QStringList TTCut::recentFileList;

// --------------------------------------------------------------
// encoder settings
// --------------------------------------------------------------
// Version
QString TTCut::versionString = "TTCut-ng - 0.52.0";

// Options
bool TTCut::encoderMode = true;
int  TTCut::encoderCodec = 0;      // Default to MPEG-2

// Current working values (will be set from codec-specific values)
int  TTCut::encoderPreset = 4;     // Default to 'fast'
int  TTCut::encoderCrf = 2;        // Default qscale for MPEG-2
int  TTCut::encoderProfile = 0;    // Default to Main Profile for MPEG-2

// MPEG-2 specific defaults
int  TTCut::mpeg2Preset = 4;       // fast
int  TTCut::mpeg2Crf = 2;          // qscale for MPEG-2 (2-31, lower=better)
int  TTCut::mpeg2Profile = 0;      // Main Profile
int  TTCut::mpeg2Muxer = 0;        // mplex (TS/PS)

// H.264 specific defaults
int  TTCut::h264Preset = 4;        // fast
int  TTCut::h264Crf = 18;          // CRF 18 (high quality for cut points)
int  TTCut::h264Profile = 2;       // high profile
int  TTCut::h264Muxer = 1;         // mkvmerge (MKV)

// H.265 specific defaults
int  TTCut::h265Preset = 4;        // fast
int  TTCut::h265Crf = 20;          // CRF 20 (high quality for cut points)
int  TTCut::h265Profile = 0;       // main profile
int  TTCut::h265Muxer = 1;         // mkvmerge (MKV)

// --------------------------------------------------------------
// muxer settings
// --------------------------------------------------------------
// Options
int     TTCut::muxMode       = 0;
int     TTCut::mpeg2Target   = 7;
QString TTCut::muxProg       = "mplex";
QString TTCut::muxProgPath   = "/usr/local/bin/";
QString TTCut::muxProgCmd    = "-f 8";
QString TTCut::muxOutputPath = QDir::homePath();
bool    TTCut::muxDeleteES   = false;
bool    TTCut::muxPause      = true;
int     TTCut::outputContainer = 1;  // Default to MKV for modern codecs

// MKV chapter settings
bool    TTCut::mkvCreateChapters  = true;   // Create chapters by default
int     TTCut::mkvChapterInterval = 5;      // Every 5 minutes

// --------------------------------------------------------------
// chapter settings
// --------------------------------------------------------------
// Options
bool TTCut::spumuxChapter = false;

// -----------------------------------------------------------------------------
// Status
// -----------------------------------------------------------------------------
bool TTCut::isVideoOpen       = false;
int  TTCut::numAudioTracks    = 0;
bool TTCut::isProjektModified = false;
bool TTCut::isPlaying         = false;
bool TTCut::isWorking         = false;

// --------------------------------------------------------------
// Cut settings
// --------------------------------------------------------------
// cut option
QString  TTCut::muxFileName        = "muxscript.sh";
QString  TTCut::cutDirPath         = QDir::currentPath();
QString  TTCut::cutVideoName       = "_cut.m2v";
bool     TTCut::cutAddSuffix       = true;
bool     TTCut::cutWriteMaxBitrate = false;
bool     TTCut::cutWriteSeqEnd     = false;
bool     TTCut::correctCutTimeCode = false;
bool     TTCut::correctCutBitRate  = false;
bool     TTCut::createCutIDD       = false;
bool     TTCut::readCutIDD         = false;

// --------------------------------------------------------------
// Global properties
// --------------------------------------------------------------
float    TTCut::frameRate          = 25.0;
QWidget* TTCut::mainWindow         = NULL;

TTCut::TTCut()
{

}

TTCut::~TTCut()
{

}


const char* toAscii(const QString& string)
{
	//qDebug(qPrintable(QString("converting string %1").arg(string)));

	char* result = string.toLatin1().data();

	//qDebug(qPrintable(QString("result is %1").arg(result)));

	return result;
}

// ISO 639-2/B language codes typically used in DVB broadcasts
QStringList TTCut::languageCodes()
{
  return QStringList()
    << "und" << "deu" << "eng" << "fra" << "ita" << "spa" << "por"
    << "dut" << "pol" << "cze" << "hun" << "dan" << "swe" << "fin"
    << "nor" << "rus" << "tur" << "gre" << "hrv" << "slo" << "rum"
    << "bul" << "srp" << "slv" << "jpn" << "chi" << "kor" << "ara";
}

QStringList TTCut::languageNames()
{
  return QStringList()
    << "Undetermined" << "Deutsch" << "English" << "Français" << "Italiano"
    << "Español" << "Português" << "Nederlands" << "Polski" << "Čeština"
    << "Magyar" << "Dansk" << "Svenska" << "Suomi" << "Norsk" << "Русский"
    << "Türkçe" << "Ελληνικά" << "Hrvatski" << "Slovenčina" << "Română"
    << "Български" << "Srpski" << "Slovenščina" << "日本語" << "中文"
    << "한국어" << "العربية";
}

QString TTCut::iso639_1to2(const QString& code2)
{
  static QMap<QString, QString> map;
  if (map.isEmpty()) {
    map["un"] = "und"; map["de"] = "deu"; map["en"] = "eng"; map["fr"] = "fra";
    map["it"] = "ita"; map["es"] = "spa"; map["pt"] = "por"; map["nl"] = "dut";
    map["pl"] = "pol"; map["cs"] = "cze"; map["hu"] = "hun"; map["da"] = "dan";
    map["sv"] = "swe"; map["fi"] = "fin"; map["no"] = "nor"; map["ru"] = "rus";
    map["tr"] = "tur"; map["el"] = "gre"; map["hr"] = "hrv"; map["sk"] = "slo";
    map["ro"] = "rum"; map["bg"] = "bul"; map["sr"] = "srp"; map["sl"] = "slv";
    map["ja"] = "jpn"; map["zh"] = "chi"; map["ko"] = "kor"; map["ar"] = "ara";
  }
  return map.value(code2, "und");
}
