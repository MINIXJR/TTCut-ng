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
    explicit TTCutProjectData(const QFileInfo& fInfo);
    ~TTCutProjectData();
    TTCutProjectData(const TTCutProjectData&) = delete;
    TTCutProjectData& operator=(const TTCutProjectData&) = delete;

    void serializeAVDataItem(const TTAVItem* vitem);
    void serializeStreamPoints(const QList<TTStreamPoint>& points);
    void serializeLogoData(const TTLogoProjectData& logoData);
    //! Every <Video> section of the document, in order. Returns how many of
    //! them actually started an open task - a section with too few nodes or
    //! a path resolveProjectPath rejects is skipped, and a project where
    //! that leaves zero videos never gets a task and therefore never a pool
    //! exit (code-audit run 5, finding C4); the caller ends the load itself.
    int  deserializeAVDataItem(TTAVData* avData);
    QList<TTStreamPoint> deserializeStreamPoints();
    TTLogoProjectData deserializeLogoData();
    void deserializeSettings();

    QString filePath();

    void writeXml();
    void readXml();

  private:
    void createDocumentStructure();
    void addTextElement(QDomElement& parent, const QString& tag, const QString& text);
    QDomElement writeVideoSection(const QString& filePath, int order);
    // <Audio> and <Subtitle> carry the same fields (Order, Name, optional
    // Language and Delay); tag picks the element name.
    QDomElement writeTrackSection(QDomElement& parent, const QString& tag, const QString& filePath, int order, const QString& language, int delayMs = 0);
    QDomElement writeRepairSection(QDomElement& parent, qint64 frameFrom, qint64 frameTo, quint8 channelMask, const QString& method);
    QDomElement writeCutSection(QDomElement& parent, int cutIn, int cutOut, int order);
    QDomElement writeMarkerSection(QDomElement& parent, int markerPos, int markerType, int order);
    //! Order and validated path from a section header. <Video>, <Audio> and
    //! <Subtitle> all start with the same two nodes (Order, Name); 'section'
    //! names the caller in the log messages. false when the section carries
    //! too few nodes or resolveProjectPath refuses the path.
    bool        parseSectionHeader(const QDomNodeList& nodes, const char* section,
                                   int& order, QString& name);
    //! One <Video> section; false when it was skipped without starting an open task.
    bool        parseVideoSection(QDomNodeList videoNodesList, TTAVData* avData);
    void        parseAudioSection(QDomNodeList audioNodesList, TTAVData* avData, TTAVItem* avItem);
    static void parseCutSection(QDomNodeList cutNodesList, TTAVItem* avItem);
    static void parseMarkerSection(QDomNodeList markerNodeList, TTAVItem* avItem);
    //! 'position' is the section's index among the <Subtitle> sections of its
    //! <Video>; it stands in for an <Order> of -1, which projects written
    //! before 2026-09-25 carry for every discovered or added subtitle.
    void        parseSubtitleSection(QDomNodeList subtitleNodesList, TTAVData* avData, TTAVItem* avItem,
                                     int position);
    //! <Language>/<Delay> child of an <Audio> or <Subtitle> section; true when
    //! 'node' was one of the two.
    static bool parseTrackLanguageDelay(const QDomNode& node, QString& lang, int& delayMs);
    void        serializeSettings();
    void        parseSettingsSection(QDomElement settingsElement);

  private:
    QFileInfo*    xmlFileInfo;
    QDomDocument* xmlDocument;
    QDomElement*  xmlRoot;
    QDomNodeList* xmlNodeList;
};

#endif //TTCUTPROJECTDATA_H
