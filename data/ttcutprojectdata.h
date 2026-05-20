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
// TTCUTPROJECTDATA
// ----------------------------------------------------------------------------

#ifndef TTCUTPROJECTDATA_H
#define TTCUTPROJECTDATA_H

class TTAVItem;
class TTAVData;
class TTVideoStream;
class TTStreamPointModel;

#include "ttstreampoint.h"

#include <QFileInfo>
#include <QDomElement>
#include <QString>
#include <QStringList>
#include <QList>
#include <QRect>

/* /////////////////////////////////////////////////////////////////////////////
 * Logo profile persistence data
 */
struct TTLogoProjectData
{
  bool    valid = false;
  bool    isMarkad = false;
  QString markadPath;
  QRect   roi;
};

/* /////////////////////////////////////////////////////////////////////////////
 * TTCutProjectData
 */
class TTCutProjectData
{
  public:
    TTCutProjectData(const QFileInfo& fInfo);
    ~TTCutProjectData();

    void serializeAVDataItem(TTAVItem* vitem);
    void serializeStreamPoints(const QList<TTStreamPoint>& points);
    void serializeLogoData(const TTLogoProjectData& logoData);
    void deserializeAVDataItem(TTAVData* avData);
    QList<TTStreamPoint> deserializeStreamPoints();
    TTLogoProjectData deserializeLogoData();
    void deserializeSettings();

    QString fileName();
    QString filePath();

    void writeXml();
    void readXml();
    void printXml();

  private:
    void createDocumentStructure();
    QDomElement writeVideoSection(const QString& filePath, int order);
    QDomElement writeAudioSection(QDomElement& parent, const QString& filePath, int order, const QString& language, int delayMs = 0);
    QDomElement writeCutSection(QDomElement& parent, int cutIn, int cutOut, int order);
    QDomElement writeMarkerSection(QDomElement& parent, int markerPos, int markerType, int order);
    QDomElement writeSubtitleSection(QDomElement& parent, const QString& filePath, int order, const QString& language);
    void        parseVideoSection(QDomNodeList videoNodesList, TTAVData* avData);
    void        parseAudioSection(QDomNodeList audioNodesList, TTAVData* avData, TTAVItem* avItem);
    void        parseCutSection(QDomNodeList cutNodesList, TTAVItem* avItem);
    void        parseMarkerSection(QDomNodeList markerNodeList, TTAVItem* avItem);
    void        parseSubtitleSection(QDomNodeList subtitleNodesList, TTAVData* avData, TTAVItem* avItem);
    void        serializeSettings();
    void        parseSettingsSection(QDomElement settingsElement);

  private:
    QFileInfo*    xmlFileInfo;
    QDomDocument* xmlDocument;
    QDomElement*  xmlRoot;
    QDomNodeList* xmlNodeList;
};

#endif //TTCUTPROJECTDATA_H
