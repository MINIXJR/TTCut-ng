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
// *** TTMUXLISTDATA
// ----------------------------------------------------------------------------

#ifndef TTMUXLISTDATA_H
#define TTMUXLISTDATA_H

#include "../common/ttcut.h"
#include "../common/ttmessagelogger.h"

#include <QList>
#include <QStringList>

class QString;
class QFileInfo;

class TTMuxListDataItem
{
  friend class TTMuxListData;

  public:
    TTMuxListDataItem();
    TTMuxListDataItem(const TTMuxListDataItem& item);
    TTMuxListDataItem(QString video, QStringList audio);
    TTMuxListDataItem(QString video, QStringList audio, QStringList subtitle);

    QString     getVideoName();
    void        setVideoName(QString videoFilePath);
    QStringList getAudioNames();
    void        appendAudioFile(QString audioFilePath, const QString& language = QString());
    QStringList getSubtitleNames();
    void        appendSubtitleFile(QString subtitleFilePath, const QString& language = QString());
    QStringList getAudioLanguages();
    QStringList getSubtitleLanguages();

    const TTMuxListDataItem& operator=(const TTMuxListDataItem& item);

  private:
    QString     videoFileName;
    QStringList audioFileNames;
    QStringList audioLanguageList;
    QStringList subtitleFileNames;
    QStringList subtitleLanguageList;
};


class TTMuxListData
{
  public:
    TTMuxListData();
    ~TTMuxListData();

    int  addItem(QString video);
    int  addItem(QString video, QString audio);
    int  addItem(QString video, QStringList audio);
    int  addItem(QString video, QStringList audio, QStringList subtitle);
    void appendAudioName(int index, QString audio);
    void appendSubtitleName(int index, QString subtitle);

    void appendItem(const TTMuxListDataItem& item);

    TTMuxListDataItem& itemAt(int index);
    QString            videoFilePathAt(int index);
    QStringList        audioFilePathsAt(int index);
    QStringList        subtitleFilePathsAt(int index);
    int  							 count();
    void 							 deleteAll();
    void 						   removeAt(int index);
    void 							 print();

  private:
    TTMuxListDataItem createMuxListItem(QString videoFilePath);

  private:
    TTMessageLogger* log;
    QList<TTMuxListDataItem>data;
};

#endif //TTMUXLISTDATA_H
