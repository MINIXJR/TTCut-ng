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
    TTMuxListDataItem(const QString& video, const QStringList& audio);
    TTMuxListDataItem(const QString& video, const QStringList& audio, const QStringList& subtitle);

    const QString&     getVideoName() const;
    void               setVideoName(const QString& videoFilePath);
    const QStringList& getAudioNames() const;
    void               appendAudioFile(const QString& audioFilePath, const QString& language = QString());
    const QStringList& getSubtitleNames() const;
    void               appendSubtitleFile(const QString& subtitleFilePath, const QString& language = QString());
    const QStringList& getAudioLanguages() const;
    const QStringList& getSubtitleLanguages() const;

    const TTMuxListDataItem& operator=(const TTMuxListDataItem& item);

  private:
    // Field-wise copy shared by the copy constructor and operator=; the
    // language lists are copied only as far as the file lists reach.
    void copyFrom(const TTMuxListDataItem& item);
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

    void appendItem(const TTMuxListDataItem& item);

    TTMuxListDataItem& itemAt(int index);
    const QString&     videoFilePathAt(int index) const;
    const QStringList& audioFilePathsAt(int index) const;
    int  							 count();
    void 							 print();

  private:

  private:
    TTMessageLogger* log;
    QList<TTMuxListDataItem>data;
};

#endif //TTMUXLISTDATA_H
