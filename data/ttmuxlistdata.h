/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2008 / ttcut.tritime.org                       */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2005                                                      */
/* FILE     : ttmuxlistdata.h                                                 */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 03/11/2006 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// *** TTMUXLISTDATA
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
