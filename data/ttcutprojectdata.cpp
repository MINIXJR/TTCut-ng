/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttcutprojectdata.cpp                                            */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 11/13/2008 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTCUTPROJECTDATA
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

#include "ttcutprojectdata.h"
#include "ttavdata.h"
#include "ttsubtitlelist.h"
#include "ttstreampoint.h"
#include "../avstream/ttavstream.h"
#include "../avstream/ttsrtsubtitlestream.h"
#include "../common/ttexception.h"
#include "../common/ttcut.h"

#include <QDir>

namespace {
// Validate a file-path read from a .ttcut project. Project files may carry
// either an absolute path (the historic format) or a path relative to the
// project file's directory. We reject anything that looks like a path-traversal
// or contains control characters/null bytes; the resulting absolute path
// (canonical-ish, but without requiring the file to exist yet) is returned.
// Empty return = invalid / refused.
static QString resolveProjectPath(const QString& name, const QFileInfo* projectFile)
{
    if (name.isEmpty()) return QString();
    // Reject NUL and other control bytes that can confuse downstream consumers.
    for (QChar c : name) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7F) return QString();
    }
    // Reject explicit '..' path segments — VDR/normal video paths never need
    // them and they're the hallmark of a path-traversal payload.
    QStringList parts = name.split('/', Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        if (p == "..") return QString();
    }
    QFileInfo fi(name);
    if (fi.isAbsolute()) return fi.absoluteFilePath();
    // Relative: anchor against the project file directory.
    if (!projectFile) return QString();
    return QDir(projectFile->absolutePath()).absoluteFilePath(name);
}
}  // namespace



/* /////////////////////////////////////////////////////////////////////////////
 * Constructor
 */
TTCutProjectData::TTCutProjectData(const QFileInfo& fInfo)
{
  xmlFileInfo = new QFileInfo(fInfo);
  xmlNodeList = NULL;
  xmlDocument = NULL;
  xmlRoot     = NULL;

  createDocumentStructure();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Destructor
 */
TTCutProjectData::~TTCutProjectData()
{
  if (xmlFileInfo != 0)  delete xmlFileInfo;
  if (xmlNodeList != 0)  delete xmlNodeList;
  if (xmlDocument != 0)  delete xmlDocument;
  if (xmlRoot     != 0)  delete xmlRoot;
}

/**
 * Returns the xml filename
 */
QString TTCutProjectData::fileName()
{
  return xmlFileInfo->fileName();
}

/**
 * Returns the xml filepath
 */
QString TTCutProjectData::filePath()
{
  return xmlFileInfo->absoluteFilePath();
}
/* /////////////////////////////////////////////////////////////////////////////
 * Serialize an AVDataItem to xml
 */
void TTCutProjectData::serializeAVDataItem(TTAVItem* vItem)
{
  QDomElement video = writeVideoSection(vItem->videoStream()->filePath(), 0);

  for (int i = 0; i < vItem->audioCount(); i++) {
    TTAudioItem aItem   = vItem->audioListItemAt(i);
    TTAudioStream*      aStream = aItem.getAudioStream();
    writeAudioSection(video, aStream->filePath(), aItem.order(), aItem.getLanguage(), aItem.getDelayMs());
  }

  for (int i = 0; i < vItem->cutCount(); i++) {
    TTCutItem cItem = vItem->cutListItemAt(i);
    writeCutSection(video, cItem.cutInIndex(), cItem.cutOutIndex(), cItem.order());
  }

  for (int i = 0; i < vItem->markerCount(); i++) {
  	TTMarkerItem mItem = vItem->markerAt(i);
  	writeMarkerSection(video, mItem.markerPos(), 1, mItem.order());
  }

  for (int i = 0; i < vItem->subtitleCount(); i++) {
    TTSubtitleItem sItem = vItem->subtitleListItemAt(i);
    TTSubtitleStream* sStream = sItem.getSubtitleStream();
    writeSubtitleSection(video, sStream->filePath(), sItem.order(), sItem.getLanguage());
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Deserialize an AVDataItem from xml
 */
void TTCutProjectData::deserializeAVDataItem(TTAVData* avData)
{
  for (int i = 1; i < xmlNodeList->size(); i++) {
    QDomElement elem = xmlNodeList->at(i).toElement();
    if (elem.isNull() || elem.tagName() != "Video") continue;
    parseVideoSection(elem.childNodes(), avData);
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Create document structure
 */
void TTCutProjectData::createDocumentStructure()
{
  xmlDocument = new QDomDocument("TTCut-Projectfile");
  xmlRoot     = new QDomElement(xmlDocument->createElement("TTCut-Projectfile"));

  xmlDocument->appendChild((*xmlRoot));

  QDomElement version = xmlDocument->createElement("Version");
  xmlRoot->appendChild(version);

  version.appendChild(xmlDocument->createTextNode("1.0"));
}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
void TTCutProjectData::parseVideoSection(QDomNodeList videoNodesList, TTAVData* avData)
{
  if (videoNodesList.size() < 2) {
    qDebug("TTCutProjectData::parseVideoSection -> insufficient nodes");
    return;
  }
  int     order = videoNodesList.at(0).toElement().text().toInt();
  QString rawName = videoNodesList.at(1).toElement().text();
  QString name = resolveProjectPath(rawName, xmlFileInfo);
  if (name.isEmpty()) {
    qWarning("TTCutProjectData::parseVideoSection -> rejected unsafe path: %s",
             qPrintable(rawName));
    return;
  }

  qDebug("TTCutProjectData::parseVideoSection -> doOpenVideoStream...");
  TTAVItem* avItem = avData->doOpenVideoStream(name, order);
  if (!avItem) {
    qDebug("TTCutProjectData::parseVideoSection -> doOpenVideoStream returned null");
    return;
  }

  qDebug("after doOpenVideoStream");
  //create the data item;
  for (int i = 2; i < videoNodesList.size(); i++) {

    if (videoNodesList.at(i).nodeName() == "Audio") {
      parseAudioSection(videoNodesList.at(i).childNodes(), avData, avItem);
    }
    else if (videoNodesList.at(i).nodeName() == "Cut") {
      parseCutSection(videoNodesList.at(i).childNodes(), avItem);
    }
    else if (videoNodesList.at(i).nodeName() == "Marker") {
    	parseMarkerSection(videoNodesList.at(i).childNodes(), avItem);
    }
    else if (videoNodesList.at(i).nodeName() == "Subtitle") {
      parseSubtitleSection(videoNodesList.at(i).childNodes(), avData, avItem);
    }
    else {
      qDebug("unkown node!");
    }
  }

  avData->sortCutItemsByOrder();
  avData->sortMarkerByOrder();
}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
void TTCutProjectData::parseAudioSection(QDomNodeList audioNodesList, TTAVData* avData, TTAVItem* avItem)
{
  if (audioNodesList.size() < 2) {
    qDebug("TTCutProjectData::parseAudioSection -> insufficient nodes");
    return;
  }
  int     order = audioNodesList.at(0).toElement().text().toInt();
  QString rawName = audioNodesList.at(1).toElement().text();
  QString name = resolveProjectPath(rawName, xmlFileInfo);
  if (name.isEmpty()) {
    qWarning("TTCutProjectData::parseAudioSection -> rejected unsafe path: %s",
             qPrintable(rawName));
    return;
  }

  // Read optional Language and Delay elements (added in TTCut-ng 0.52+ and 0.66+)
  QString lang;
  int delayMs = 0;
  for (int n = 2; n < audioNodesList.size(); n++) {
    if (audioNodesList.at(n).nodeName() == "Language") {
      lang = audioNodesList.at(n).toElement().text();
    } else if (audioNodesList.at(n).nodeName() == "Delay") {
      delayMs = audioNodesList.at(n).toElement().text().toInt();
    }
  }

  QFileInfo fInfo(name);
  qDebug("TTCutProjectData::parseAudioSection -> before doOpenAudioStream...");
  avData->doOpenAudioStream(avItem, name, order);
  if (!lang.isEmpty()) {
    avData->setPendingAudioLanguage(avItem, order, lang);
  }
  if (delayMs != 0) {
    avData->setPendingAudioDelay(avItem, order, delayMs);
  }
  qDebug("after doOpenAudioStream...");
}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
void TTCutProjectData::parseCutSection(QDomNodeList cutNodesList, TTAVItem* avItem)
{
  if (cutNodesList.size() < 3) {
    qWarning("parseCutSection: <Cut> element has only %d children, expected 3", cutNodesList.size());
    return;
  }

  int order       = cutNodesList.at(0).toElement().text().toInt();
  int cutIn       = cutNodesList.at(1).toElement().text().toInt();
  int cutOut      = cutNodesList.at(2).toElement().text().toInt();

  avItem->appendCutEntry(cutIn, cutOut, order);
}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
void TTCutProjectData::parseMarkerSection(QDomNodeList markerNodesList, TTAVItem* avItem)
{
  if (markerNodesList.size() < 2) {
    qWarning("parseMarkerSection: <Marker> element has only %d children, expected 2", markerNodesList.size());
    return;
  }

  int order = markerNodesList.at(0).toElement().text().toInt();
  int pos   = markerNodesList.at(1).toElement().text().toInt();
  //int type  = markerNodesList.at(2).toElement().text().toInt();

  avItem->appendMarker(pos, order);

  // Also collect as legacy stream point for Landezonen widget
  mParsedLegacyMarkers.append(TTStreamPoint(pos, StreamPointType::ManualMarker,
    QString("Marker (manuell)")));
}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
QDomElement TTCutProjectData::writeVideoSection(const QString& filePath, int order)
{
  QDomElement video = xmlDocument->createElement("Video");
  xmlRoot->appendChild(video);

  QDomElement xmlOrder = xmlDocument->createElement("Order");
  video.appendChild(xmlOrder);
  xmlOrder.appendChild(xmlDocument->createTextNode(QString("%1").arg(order)));

  QDomElement name = xmlDocument->createElement("Name");
  video.appendChild(name);
  name.appendChild(xmlDocument->createTextNode(filePath));

 return video;
}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
QDomElement TTCutProjectData::writeAudioSection(QDomElement& parent, const QString& filePath, int order, const QString& language, int delayMs)
{
  QDomElement audio = xmlDocument->createElement("Audio");
  parent.appendChild(audio);

  QDomElement xmlOrder = xmlDocument->createElement("Order");
  audio.appendChild(xmlOrder);
  xmlOrder.appendChild(xmlDocument->createTextNode(QString("%1").arg(order)));

  QDomElement name = xmlDocument->createElement("Name");
  audio.appendChild(name);
  name.appendChild(xmlDocument->createTextNode(filePath));

  if (!language.isEmpty()) {
    QDomElement lang = xmlDocument->createElement("Language");
    audio.appendChild(lang);
    lang.appendChild(xmlDocument->createTextNode(language));
  }

  if (delayMs != 0) {
    QDomElement delay = xmlDocument->createElement("Delay");
    audio.appendChild(delay);
    delay.appendChild(xmlDocument->createTextNode(QString::number(delayMs)));
  }

  return audio;
}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
QDomElement TTCutProjectData::writeCutSection(QDomElement& parent, int cutIn, int cutOut, int order)
{
  QDomElement cut = xmlDocument->createElement("Cut");
  parent.appendChild(cut);

  QDomElement xmlOrder = xmlDocument->createElement("Order");
  cut.appendChild(xmlOrder);
  xmlOrder.appendChild(xmlDocument->createTextNode(QString("%1").arg(order)));

  QDomElement xmlCutIn = xmlDocument->createElement("CutIn");
  cut.appendChild(xmlCutIn);
  xmlCutIn.appendChild(xmlDocument->createTextNode(QString("%1").arg(cutIn)));

  QDomElement xmlCutOut = xmlDocument->createElement("CutOut");
  cut.appendChild(xmlCutOut);
  xmlCutOut.appendChild(xmlDocument->createTextNode(QString("%1").arg(cutOut)));

  return cut;
}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
QDomElement TTCutProjectData::writeMarkerSection(QDomElement& parent, int markerPos, int markerType, int order)
{
  QDomElement marker = xmlDocument->createElement("Marker");
  parent.appendChild(marker);

  QDomElement xmlOrder = xmlDocument->createElement("Order");
  marker.appendChild(xmlOrder);
  xmlOrder.appendChild(xmlDocument->createTextNode(QString("%1").arg(order)));

  QDomElement xmlMarkerPos = xmlDocument->createElement("MarkerPos");
  marker.appendChild(xmlMarkerPos);
  xmlMarkerPos.appendChild(xmlDocument->createTextNode(QString("%1").arg(markerPos)));

  QDomElement xmlMarkerType = xmlDocument->createElement("MarkerType");
  marker.appendChild(xmlMarkerType);
  xmlMarkerType.appendChild(xmlDocument->createTextNode(QString("%1").arg(markerType)));

  return marker;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Serialize stream points to XML (top-level, after all Video elements)
 */
void TTCutProjectData::serializeStreamPoints(const QList<TTStreamPoint>& points)
{
  for (int i = 0; i < points.size(); i++) {
    const TTStreamPoint& pt = points.at(i);

    QDomElement elem = xmlDocument->createElement("StreamPoint");
    xmlRoot->appendChild(elem);

    QDomElement frameElem = xmlDocument->createElement("Frame");
    elem.appendChild(frameElem);
    frameElem.appendChild(xmlDocument->createTextNode(QString::number(pt.frameIndex())));

    QDomElement typeElem = xmlDocument->createElement("Type");
    elem.appendChild(typeElem);
    typeElem.appendChild(xmlDocument->createTextNode(TTStreamPoint::typeToString(pt.type())));

    QDomElement descElem = xmlDocument->createElement("Description");
    elem.appendChild(descElem);
    descElem.appendChild(xmlDocument->createTextNode(pt.description()));

    QDomElement confElem = xmlDocument->createElement("Confidence");
    elem.appendChild(confElem);
    confElem.appendChild(xmlDocument->createTextNode(QString::number(pt.confidence(), 'f', 2)));

    QDomElement durElem = xmlDocument->createElement("Duration");
    elem.appendChild(durElem);
    durElem.appendChild(xmlDocument->createTextNode(QString::number(pt.duration(), 'f', 2)));
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Deserialize stream points from XML
 */
QList<TTStreamPoint> TTCutProjectData::deserializeStreamPoints()
{
  QList<TTStreamPoint> points;

  if (!xmlRoot) return points;

  QDomNodeList nodes = xmlRoot->childNodes();
  for (int i = 0; i < nodes.size(); i++) {
    QDomElement elem = nodes.at(i).toElement();
    if (elem.isNull()) continue;

    if (elem.tagName() == "StreamPoint") {
      QDomNodeList children = elem.childNodes();
      int frame = 0;
      QString type, desc;
      float confidence = 0.0f, duration = 0.0f;

      for (int j = 0; j < children.size(); j++) {
        QDomElement child = children.at(j).toElement();
        if (child.isNull()) continue;

        if (child.tagName() == "Frame")
          frame = child.text().toInt();
        else if (child.tagName() == "Type")
          type = child.text();
        else if (child.tagName() == "Description")
          desc = child.text();
        else if (child.tagName() == "Confidence")
          confidence = child.text().toFloat();
        else if (child.tagName() == "Duration")
          duration = child.text().toFloat();
      }

      points.append(TTStreamPoint(frame, TTStreamPoint::stringToType(type),
                                   desc, confidence, duration));
    }
    // Legacy Marker elements at top level (convert to ManualMarker)
    else if (elem.tagName() == "Marker") {
      QDomNodeList children = elem.childNodes();
      int pos = 0;
      for (int j = 0; j < children.size(); j++) {
        QDomElement child = children.at(j).toElement();
        if (!child.isNull() && child.tagName() == "MarkerPos")
          pos = child.text().toInt();
      }
      points.append(TTStreamPoint(pos, StreamPointType::ManualMarker,
                                   QString("Marker (manuell)")));
    }
  }

  // Include markers parsed from within <Video> sections ONLY if no StreamPoint
  // elements exist (legacy project without Landezonen). Otherwise the markers
  // would be duplicated on every project load since they are already saved
  // as StreamPoints.
  bool hasStreamPoints = false;
  for (int i = 0; i < nodes.size(); i++) {
    if (nodes.at(i).toElement().tagName() == "StreamPoint") {
      hasStreamPoints = true;
      break;
    }
  }
  if (!hasStreamPoints) {
    points.append(mParsedLegacyMarkers);
  }

  return points;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Serialize logo detection data to XML (top-level)
 */
void TTCutProjectData::serializeLogoData(const TTLogoProjectData& logoData)
{
  if (!logoData.valid) return;

  QDomElement elem = xmlDocument->createElement("LogoProfile");
  xmlRoot->appendChild(elem);

  if (logoData.isMarkad) {
    QDomElement typeElem = xmlDocument->createElement("Source");
    elem.appendChild(typeElem);
    typeElem.appendChild(xmlDocument->createTextNode("markad"));

    QDomElement pathElem = xmlDocument->createElement("Path");
    elem.appendChild(pathElem);
    pathElem.appendChild(xmlDocument->createTextNode(logoData.markadPath));
  } else {
    QDomElement typeElem = xmlDocument->createElement("Source");
    elem.appendChild(typeElem);
    typeElem.appendChild(xmlDocument->createTextNode("manual"));

    QDomElement roiElem = xmlDocument->createElement("ROI");
    elem.appendChild(roiElem);
    roiElem.setAttribute("x", logoData.roi.x());
    roiElem.setAttribute("y", logoData.roi.y());
    roiElem.setAttribute("w", logoData.roi.width());
    roiElem.setAttribute("h", logoData.roi.height());
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Deserialize logo detection data from XML
 */
TTLogoProjectData TTCutProjectData::deserializeLogoData()
{
  TTLogoProjectData data;
  if (!xmlRoot) return data;

  QDomNodeList nodes = xmlRoot->childNodes();
  for (int i = 0; i < nodes.size(); i++) {
    QDomElement elem = nodes.at(i).toElement();
    if (elem.isNull() || elem.tagName() != "LogoProfile") continue;

    QDomNodeList children = elem.childNodes();
    QString source;

    for (int j = 0; j < children.size(); j++) {
      QDomElement child = children.at(j).toElement();
      if (child.isNull()) continue;

      if (child.tagName() == "Source")
        source = child.text();
      else if (child.tagName() == "Path")
        data.markadPath = child.text();
      else if (child.tagName() == "ROI") {
        data.roi = QRect(child.attribute("x").toInt(),
                         child.attribute("y").toInt(),
                         child.attribute("w").toInt(),
                         child.attribute("h").toInt());
      }
    }

    data.isMarkad = (source == "markad");
    data.valid = true;
    break;  // only one LogoProfile element
  }

  return data;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Write subtitle section to XML
 */
QDomElement TTCutProjectData::writeSubtitleSection(QDomElement& parent, const QString& filePath, int order, const QString& language)
{
  QDomElement subtitle = xmlDocument->createElement("Subtitle");
  parent.appendChild(subtitle);

  QDomElement xmlOrder = xmlDocument->createElement("Order");
  subtitle.appendChild(xmlOrder);
  xmlOrder.appendChild(xmlDocument->createTextNode(QString("%1").arg(order)));

  QDomElement name = xmlDocument->createElement("Name");
  subtitle.appendChild(name);
  name.appendChild(xmlDocument->createTextNode(filePath));

  if (!language.isEmpty()) {
    QDomElement lang = xmlDocument->createElement("Language");
    subtitle.appendChild(lang);
    lang.appendChild(xmlDocument->createTextNode(language));
  }

  return subtitle;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Parse subtitle section from XML
 */
void TTCutProjectData::parseSubtitleSection(QDomNodeList subtitleNodesList, TTAVData* avData, TTAVItem* avItem)
{
  if (subtitleNodesList.size() < 2) {
    qDebug("TTCutProjectData::parseSubtitleSection -> insufficient nodes");
    return;
  }
  int     order = subtitleNodesList.at(0).toElement().text().toInt();
  QString rawName = subtitleNodesList.at(1).toElement().text();
  QString name = resolveProjectPath(rawName, xmlFileInfo);
  if (name.isEmpty()) {
    qWarning("TTCutProjectData::parseSubtitleSection -> rejected unsafe path: %s",
             qPrintable(rawName));
    return;
  }

  // Read optional Language element (added in TTCut-ng 0.52+)
  QString lang;
  if (subtitleNodesList.size() > 2 && subtitleNodesList.at(2).nodeName() == "Language") {
    lang = subtitleNodesList.at(2).toElement().text();
  }

  qDebug("TTCutProjectData::parseSubtitleSection -> before doOpenSubtitleStream...");
  avData->doOpenSubtitleStream(avItem, name, order);
  if (!lang.isEmpty()) {
    avData->setPendingSubtitleLanguage(avItem, order, lang);
  }
  qDebug("after doOpenSubtitleStream...");
}

/* /////////////////////////////////////////////////////////////////////////////
 * Serialize global settings to XML (top-level <Settings> element)
 */
void TTCutProjectData::serializeSettings()
{
  QDomElement root = xmlDocument->documentElement();
  QDomElement settings = xmlDocument->createElement("Settings");
  root.appendChild(settings);

  auto addElement = [&](const QString& name, const QString& value) {
    QDomElement el = xmlDocument->createElement(name);
    settings.appendChild(el);
    el.appendChild(xmlDocument->createTextNode(value));
  };

  // Output
  addElement("CutDirPath",    TTCut::cutDirPath);
  addElement("CutVideoName",  TTCut::cutVideoName);
  addElement("CutAddSuffix",  TTCut::cutAddSuffix ? "true" : "false");

  // Muxing
  addElement("OutputContainer",    QString::number(TTCut::outputContainer));
  addElement("MkvCreateChapters",  TTCut::mkvCreateChapters ? "true" : "false");
  addElement("MkvChapterInterval", QString::number(TTCut::mkvChapterInterval));
  addElement("MuxDeleteES",        TTCut::muxDeleteES ? "true" : "false");

  // Encoder (active codec values)
  addElement("EncoderPreset",  QString::number(TTCut::encoderPreset));
  addElement("EncoderCrf",     QString::number(TTCut::encoderCrf));
  addElement("EncoderProfile", QString::number(TTCut::encoderProfile));
}

/* /////////////////////////////////////////////////////////////////////////////
 * Deserialize global settings from XML
 */
void TTCutProjectData::deserializeSettings()
{
  QDomElement root = xmlDocument->documentElement();
  QDomNodeList settingsList = root.elementsByTagName("Settings");
  if (settingsList.isEmpty()) return;
  parseSettingsSection(settingsList.at(0).toElement());
}

/* /////////////////////////////////////////////////////////////////////////////
 * Parse <Settings> element children into TTCut global state
 */
void TTCutProjectData::parseSettingsSection(QDomElement settingsElement)
{
  QDomNodeList children = settingsElement.childNodes();
  for (int i = 0; i < children.size(); i++) {
    QDomElement el = children.at(i).toElement();
    if (el.isNull()) continue;
    QString name  = el.tagName();
    QString value = el.text();

    // Output
    if      (name == "CutDirPath") {
      // Validate against path traversal / NUL injection. We don't anchor a
      // CutDirPath to the project file's directory — users put cut output
      // wherever they want — but we still require a sane absolute path.
      QString validated = resolveProjectPath(value, xmlFileInfo);
      if (!validated.isEmpty()) TTCut::cutDirPath = validated;
      else qWarning("parseSettingsSection: rejected unsafe CutDirPath '%s'",
                    qPrintable(value));
    }
    else if (name == "CutVideoName") {
      // Filename only — must not contain '/' or control chars.
      bool ok = !value.contains('/') && !value.contains('\\');
      for (QChar c : value) if (c.unicode() < 0x20 || c.unicode() == 0x7F) ok = false;
      if (ok) TTCut::cutVideoName = value;
      else qWarning("parseSettingsSection: rejected unsafe CutVideoName '%s'",
                    qPrintable(value));
    }
    else if (name == "CutAddSuffix")       TTCut::cutAddSuffix = (value == "true");
    // Muxing
    else if (name == "OutputContainer")    TTCut::outputContainer = value.toInt();
    else if (name == "MkvCreateChapters")  TTCut::mkvCreateChapters = (value == "true");
    else if (name == "MkvChapterInterval") TTCut::mkvChapterInterval = value.toInt();
    else if (name == "MuxDeleteES")        TTCut::muxDeleteES = (value == "true");
    // Encoder
    else if (name == "EncoderPreset")      TTCut::encoderPreset = value.toInt();
    else if (name == "EncoderCrf")         TTCut::encoderCrf = value.toInt();
    else if (name == "EncoderProfile")     TTCut::encoderProfile = value.toInt();
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Clear the xml structure
 */
void TTCutProjectData::clear()
{

}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
void TTCutProjectData::printXml()
{
  qDebug("xml: %s", qPrintable(xmlDocument->toString()));
}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
void TTCutProjectData::writeXml()
{
  serializeSettings();

  QFile xmlFile(xmlFileInfo->absoluteFilePath());

  if (!xmlFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    throw TTIOException(__FILE__, __LINE__,
      QString("Cannot open project file for writing: %1 (%2)")
        .arg(xmlFileInfo->absoluteFilePath(), xmlFile.errorString()));
  }
  xmlFile.write(xmlDocument->toByteArray());

  xmlFile.flush();
  xmlFile.close();
}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
void TTCutProjectData::readXml()
{
  QFile xmlFile(xmlFileInfo->absoluteFilePath());

  if (!xmlFile.open(QIODevice::ReadOnly)) {
    throw TTDataFormatException(QString("Error opening project file %1!").arg(xmlFileInfo->filePath()));
  }

  if (!xmlDocument->setContent(&xmlFile)) {
    throw TTDataFormatException(QString("Error parsing xml project file %1!").arg(xmlFileInfo->filePath()));
  }

  xmlFile.close();

  if (xmlRoot != NULL)
    delete xmlRoot;

  if (xmlNodeList != NULL)
    delete xmlNodeList;

  xmlRoot     = new QDomElement(xmlDocument->documentElement());
  xmlNodeList = new QDomNodeList(xmlRoot->childNodes());

  //check file version
  if (!xmlNodeList->at(0).isElement()) {
    qDebug("wrong project file format!");
    return;
  }

  QDomElement version = xmlNodeList->at(0).toElement();

  int ver = qRound(version.text().toFloat());

  if (ver != 1) {
    qDebug("wrong project file version: %d", ver);
    return;
  }
}


