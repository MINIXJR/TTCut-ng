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
// TTMUXLISTDATA
// ----------------------------------------------------------------------------

#include "ttmuxlistdata.h"


#include <QFileInfo>
#include <QString>
#include <QStringList>

/*! ////////////////////////////////////////////////////////////////////////////
 * TTMuxListItem class
 */

//! This class represent an item in the list
TTMuxListDataItem::TTMuxListDataItem()
{
}

TTMuxListDataItem::TTMuxListDataItem(const TTMuxListDataItem& item)
{
  copyFrom(item);
}

void TTMuxListDataItem::copyFrom(const TTMuxListDataItem& item)
{
  // A language list never reaches past its file list.
  auto copyTracks = [](const QStringList& srcFiles, const QStringList& srcLangs,
                       QStringList& files, QStringList& langs) {
    files = srcFiles;
    langs = srcLangs.mid(0, srcFiles.count());
  };

  videoFileName = item.videoFileName;
  copyTracks(item.audioFileNames, item.audioLanguageList,
             audioFileNames, audioLanguageList);
  copyTracks(item.subtitleFileNames, item.subtitleLanguageList,
             subtitleFileNames, subtitleLanguageList);
}

const TTMuxListDataItem& TTMuxListDataItem::operator=(const TTMuxListDataItem& item)
{
  if (this != &item) copyFrom(item);
  return *this;
}


TTMuxListDataItem::TTMuxListDataItem(const QString& video, const QStringList& audio)
  : videoFileName(video), audioFileNames(audio)
{
}

TTMuxListDataItem::TTMuxListDataItem(const QString& video, const QStringList& audio,
                                     const QStringList& subtitle)
  : videoFileName(video), audioFileNames(audio), subtitleFileNames(subtitle)
{
}

const QString& TTMuxListDataItem::getVideoName() const
{
  return videoFileName;
}

void TTMuxListDataItem::setVideoName(const QString& videoFilePath)
{
  videoFileName = videoFilePath;
}

const QStringList& TTMuxListDataItem::getAudioNames() const
{
  return audioFileNames;
}

void TTMuxListDataItem::appendAudioFile(const QString& audioFilePath, const QString& language)
{
  audioFileNames.append(audioFilePath);
  audioLanguageList.append(language);
}

const QStringList& TTMuxListDataItem::getSubtitleNames() const
{
  return subtitleFileNames;
}

void TTMuxListDataItem::appendSubtitleFile(const QString& subtitleFilePath, const QString& language)
{
  subtitleFileNames.append(subtitleFilePath);
  subtitleLanguageList.append(language);
}

const QStringList& TTMuxListDataItem::getAudioLanguages() const
{
  return audioLanguageList;
}

const QStringList& TTMuxListDataItem::getSubtitleLanguages() const
{
  return subtitleLanguageList;
}

/*! ////////////////////////////////////////////////////////////////////////////
 * TTMuxList Container
 */

//! Construct the audio list data object
TTMuxListData::TTMuxListData()
{
  log = TTMessageLogger::getInstance();
}

//! Destruct object
TTMuxListData::~TTMuxListData()
{
  data.clear();
}

void TTMuxListData::appendItem(const TTMuxListDataItem& item)
{
  data.append(item);
}

const QString& TTMuxListData::videoFilePathAt(int index) const
{
  return data[index].videoFileName;
}

//! Returns the audio file-paths string list
const QStringList& TTMuxListData::audioFilePathsAt(int index) const
{
  return data[index].audioFileNames;
}

//! Returns the data item at position index
TTMuxListDataItem& TTMuxListData::itemAt(int index)
{
  return data[index];
}

//! Returns the number of entries in list
int  TTMuxListData::count()
{
  return data.count();
}

//! Print the data list for debug purpose
void TTMuxListData::print()
{
  log->infoMsg(__FILE__, __LINE__, "mux-list data:");

  for(int i=0; i < data.count(); i++) {
    log->infoMsg(__FILE__, __LINE__, "--------------------------------");
    log->infoMsg(__FILE__, __LINE__, QString("video-file: %1").arg(data[i].getVideoName()));
    QStringList audioNames = data[i].getAudioNames();
    for (int j=0; j < audioNames.size(); j++) {
      log->infoMsg(__FILE__, __LINE__, QString("audio-file: %1").arg(audioNames.at(j)));
    }
    QStringList subtitleNames = data[i].getSubtitleNames();
    for (int j=0; j < subtitleNames.size(); j++) {
      log->infoMsg(__FILE__, __LINE__, QString("subtitle-file: %1").arg(subtitleNames.at(j)));
    }
    log->infoMsg(__FILE__, __LINE__, "--------------------------------");
  }
}

