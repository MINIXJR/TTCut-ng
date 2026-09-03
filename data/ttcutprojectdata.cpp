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

#include "ttcutprojectdata.h"
#include "ttavdata.h"
#include "ttaudiorepairitem.h"
#include "ttsubtitlelist.h"
#include "ttstreampoint.h"
#include "../avstream/ttavstream.h"
#include "../common/ttexception.h"
#include "../common/ttcut.h"
#include "../common/ttsettings.h"
#include "../common/ttmessagelogger.h"
#include "../avstream/ttac3audioheader.h"

#include <QDir>
#include <QFile>

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

// Determine the real AC3 frame byte size of 'path' by reading its first sync
// frame header, the same frmsizecod lookup TTAC3AudioStream::readAudioHeader()
// and TTFFmpegWrapper::analyzeAcmod() use (avstream/ttac3audioheader.h's
// AC3FrameLength[fscod][frmsizecod], a word count -> *2 for bytes). The frame
// size scales with the stream's bit rate (384 kbit/s@48kHz = 1536 B, but
// 448 kbit/s = 1792 B and 192 kbit/s = 768 B are both real corpus material,
// see extern/ttaudiorepair.cpp) and must never be hardcoded. Assumes CBR (the
// whole file uses the same frmsizecod) -- buildRepairTable() re-checks this
// per-frame across the actual repair range and aborts the track if it isn't,
// so a wrong assumption here never causes an out-of-bounds read/write, only
// an incorrectly enabled/disabled repair item that a later, exact check
// would still catch downstream.
// Returns the frame byte size, or -1 (with *error set) if no valid AC3 sync
// frame could be found in the file's first bytes.
static qint64 ac3FrameByteSize(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("could not open file");
        return -1;
    }
    // A valid AC3 ES starts with a sync frame at offset 0; scan a small
    // prefix in case of leading junk bytes rather than requiring an exact
    // offset-0 match.
    static constexpr qint64 kScanLimit = 65536;
    QByteArray buf = file.read(kScanLimit);
    for (qint64 pos = 0; pos + 8 <= buf.size(); ++pos) {
        const uchar* d = reinterpret_cast<const uchar*>(buf.constData()) + pos;
        if (d[0] != 0x0B || d[1] != 0x77) continue;
        quint8 fscod      = (d[4] >> 6) & 0x03;
        quint8 frmsizecod = d[4] & 0x3F;
        if (fscod >= 3 || frmsizecod >= 38) continue;  // reserved/invalid, keep scanning
        qint64 bytes = static_cast<qint64>(AC3FrameLength[fscod][frmsizecod]) * 2;
        if (bytes > 0) return bytes;
    }
    if (error) *error = QStringLiteral("no valid AC3 sync frame found");
    return -1;
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
    // Write the VISIBLE list position as <Order>, not aItem.order(): the
    // reorder buttons swap list positions without touching mOrder, so the
    // stored order would otherwise still be the discovery order and a
    // manual arrangement would not survive save/reload (sortByProjectOrder
    // restores exactly this number).
    QDomElement audio = writeAudioSection(video, aStream->filePath(), i, aItem.getLanguage(), aItem.getDelayMs());

    // Repair items are tagged with the same visible list position (trackIndex()),
    // not nested under the audio list itself - filter by it here.
    for (const TTAudioRepairItem& repair : vItem->audioRepairList()) {
      if (repair.trackIndex() != i) continue;
      writeRepairSection(audio, repair.frameFrom(), repair.frameTo(), repair.channelMask(), repair.method());
    }
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
    writeSubtitleSection(video, sStream->filePath(), sItem.order(), sItem.getLanguage(), sItem.getDelayMs());
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

  // Stays "1.0" deliberately after the display-order unification: the file
  // format and the meaning of stored cut positions are unchanged (see the
  // note in parseCutSection). No migration distinguishes old from new files.
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

  // Read optional Language, Delay and Repair elements (added in TTCut-ng
  // 0.52+, 0.66+ and unreleased). Repair may occur multiple times; any other
  // child element (future additions) is silently ignored here.
  QString lang;
  int delayMs = 0;
  QList<TTAudioRepairItem> repairs;
  for (int n = 2; n < audioNodesList.size(); n++) {
    QDomNode node = audioNodesList.at(n);
    if (node.nodeName() == "Language") {
      lang = node.toElement().text();
    } else if (node.nodeName() == "Delay") {
      delayMs = node.toElement().text().toInt();
    } else if (node.nodeName() == "Repair") {
      QDomNodeList repairNodes = node.childNodes();
      qint64  frameFrom = 0;
      qint64  frameTo = 0;
      quint8  channelMask = 0;
      QString method = QStringLiteral("silence-fade");
      for (int r = 0; r < repairNodes.size(); r++) {
        QDomElement relem = repairNodes.at(r).toElement();
        if (relem.isNull()) continue;
        if (relem.tagName() == "FrameFrom")      frameFrom = relem.text().toLongLong();
        else if (relem.tagName() == "FrameTo")   frameTo = relem.text().toLongLong();
        else if (relem.tagName() == "Channels")  channelMask = static_cast<quint8>(relem.text().toUInt());
        else if (relem.tagName() == "Method")    method = relem.text();
      }
      // trackIndex = order: the visible <Order> position this Audio section
      // was saved at, which is exactly the position sortByProjectOrder()
      // restores the track to once loading finishes (see the comment in
      // TTAVData::onOpenAudioFinished).
      repairs.append(TTAudioRepairItem(order, frameFrom, frameTo, channelMask, method));
    }
  }

  // Load-time validation: a repair range saved against one AC3 file can point
  // past the end of a differently-sized file now sitting at that path (the
  // recording was re-demuxed/replaced after the project was saved). This is
  // defense-in-depth, not the only guard: buildRepairTable() re-validates the
  // range against the real file during the cut and aborts that track cleanly
  // if it's still out of bounds, so a stale range here was never going to
  // read/write outside the file either way. Catching it at load time instead
  // just surfaces the problem immediately - the entry is disabled, not
  // dropped (see TTAudioRepairItem::mEnabled), so it stays visible in the
  // repair list and the user can fix or delete it rather than discovering it
  // only when a cut aborts. The entry is NOT hidden: the marker's context
  // menu still offers "Edit repair..." for it, TTAVData::cutAudioTracks()
  // logs a warning for every disabled item it skips instead of skipping
  // silently, and TTCutMainWindow marks the corresponding AudioAnomaly
  // marker's text (final review I4).
  if (!repairs.isEmpty()) {
    qint64 audioFileSize = QFileInfo(name).size();
    QString frameSizeError;
    // The real per-frame byte size, not a hardcoded constant: it scales with
    // the stream's bit rate (see ac3FrameByteSize() above).
    qint64 frameBytes = ac3FrameByteSize(name, &frameSizeError);
    for (TTAudioRepairItem& repair : repairs) {
      // Structural sanity first (final review M5): a hand-edited or
      // truncated project file can carry a negative or reversed range. Those
      // never reach the file-size check meaningfully - a negative frameFrom
      // would make buildRepairTable read from the start of the file, a
      // reversed range would silently produce an empty table - so reject
      // them here, with the same "disable, never drop" rule as below.
      if (repair.frameFrom() < 0 || repair.frameTo() < repair.frameFrom()) {
        repair.setEnabled(false);
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("TTCutProjectData::parseAudioSection -> repair range %1-%2 on '%3' "
                    "is not a valid frame range (negative, or end before start) - "
                    "disabling this repair entry")
                .arg(repair.frameFrom())
                .arg(repair.frameTo())
                .arg(name));
        continue;
      }
      if (frameBytes <= 0) {
        // Can't determine the real frame size (file missing/unreadable/no
        // valid AC3 header) - never silently wave the item through under an
        // assumed size, and never silently drop it either; disable with a
        // reason so the user can investigate.
        repair.setEnabled(false);
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("TTCutProjectData::parseAudioSection -> could not determine "
                    "the AC3 frame size of '%1' (%2) - disabling repair entry "
                    "%3-%4 instead of validating it against an assumed size")
                .arg(name, frameSizeError)
                .arg(repair.frameFrom())
                .arg(repair.frameTo()));
        continue;
      }
      // frameTo is INCLUSIVE, so the range needs frames 0..frameTo to be
      // fully present: (frameTo + 1) * frameBytes bytes. The old
      // `frameTo * frameBytes >= size` test asked whether the last frame
      // STARTS inside the file and therefore accepted a range whose last
      // frame is cut off by the file's end (final review M4).
      if ((repair.frameTo() + 1) * frameBytes > audioFileSize) {
        repair.setEnabled(false);
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("TTCutProjectData::parseAudioSection -> repair range %1-%2 "
                    "on '%3' reaches past the file's end (%4 bytes, %5 bytes/"
                    "frame) - disabling this repair entry")
                .arg(repair.frameFrom())
                .arg(repair.frameTo())
                .arg(name)
                .arg(audioFileSize)
                .arg(frameBytes));
      }
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
  if (!repairs.isEmpty()) {
    avData->setPendingAudioRepairs(avItem, order, repairs);
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

  // NOTE (display-order unification): do NOT convert these positions for
  // H.26x projects. Cut positions were always chosen against the frame shown
  // at that navigation position, and decodeFrame() shows the display-rank
  // frame per position in both old and new builds (its behaviour is
  // unchanged). The stored number therefore already denotes a display
  // position; the engine now cuts exactly at the displayed frame (the old
  // engine cut ~B-frame-reorder frames off — that was the bug). A decode->
  // display conversion here would double-shift and break legacy projects.
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
}

/* /////////////////////////////////////////////////////////////////////////////
 * One <tag>text</tag> child under parent — the shape every section writer
 * below repeats per field.
 */
void TTCutProjectData::addTextElement(QDomElement& parent, const QString& tag, const QString& text)
{
  QDomElement elem = xmlDocument->createElement(tag);
  parent.appendChild(elem);
  elem.appendChild(xmlDocument->createTextNode(text));
}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
QDomElement TTCutProjectData::writeVideoSection(const QString& filePath, int order)
{
  QDomElement video = xmlDocument->createElement("Video");
  xmlRoot->appendChild(video);

  addTextElement(video, "Order", QString("%1").arg(order));

  addTextElement(video, "Name", filePath);

 return video;
}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
QDomElement TTCutProjectData::writeAudioSection(QDomElement& parent, const QString& filePath, int order, const QString& language, int delayMs)
{
  QDomElement audio = xmlDocument->createElement("Audio");
  parent.appendChild(audio);

  addTextElement(audio, "Order", QString("%1").arg(order));

  addTextElement(audio, "Name", filePath);

  if (!language.isEmpty()) {
    addTextElement(audio, "Language", language);
  }

  if (delayMs != 0) {
    addTextElement(audio, "Delay", QString::number(delayMs));
  }

  return audio;
}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
QDomElement TTCutProjectData::writeRepairSection(QDomElement& parent, qint64 frameFrom, qint64 frameTo, quint8 channelMask, const QString& method)
{
  QDomElement repair = xmlDocument->createElement("Repair");
  parent.appendChild(repair);

  addTextElement(repair, "FrameFrom", QString::number(frameFrom));

  addTextElement(repair, "FrameTo", QString::number(frameTo));

  addTextElement(repair, "Channels", QString::number(channelMask));

  addTextElement(repair, "Method", method);

  return repair;
}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
QDomElement TTCutProjectData::writeCutSection(QDomElement& parent, int cutIn, int cutOut, int order)
{
  QDomElement cut = xmlDocument->createElement("Cut");
  parent.appendChild(cut);

  addTextElement(cut, "Order", QString("%1").arg(order));

  addTextElement(cut, "CutIn", QString("%1").arg(cutIn));

  addTextElement(cut, "CutOut", QString("%1").arg(cutOut));

  return cut;
}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
QDomElement TTCutProjectData::writeMarkerSection(QDomElement& parent, int markerPos, int markerType, int order)
{
  QDomElement marker = xmlDocument->createElement("Marker");
  parent.appendChild(marker);

  addTextElement(marker, "Order", QString("%1").arg(order));

  addTextElement(marker, "MarkerPos", QString("%1").arg(markerPos));

  addTextElement(marker, "MarkerType", QString("%1").arg(markerType));

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

    addTextElement(elem, "Frame", QString::number(pt.frameIndex()));

    addTextElement(elem, "Type", TTStreamPoint::typeToString(pt.type()));

    addTextElement(elem, "Description", pt.description());

    addTextElement(elem, "Confidence", QString::number(pt.confidence(), 'f', 2));

    addTextElement(elem, "Duration", QString::number(pt.duration(), 'f', 2));

    // Exact AC3 frame range of an AudioAnomaly finding (final review I3),
    // both bounds inclusive. Written only when known, so nothing changes for
    // any other marker type; an older TTCut-ng ignores the two extra child
    // elements on load (unknown elements are skipped) and simply falls back
    // to the frameIndex/duration estimate, which is what it always did.
    if (pt.hasAudioFrameRange()) {
      addTextElement(elem, "AudioFrameFrom", QString::number(pt.audioFrameFrom()));

      addTextElement(elem, "AudioFrameTo", QString::number(pt.audioFrameTo()));
    }
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
      qint64 audioFrameFrom = -1, audioFrameTo = -1;

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
        else if (child.tagName() == "AudioFrameFrom")
          audioFrameFrom = child.text().toLongLong();
        else if (child.tagName() == "AudioFrameTo")
          audioFrameTo = child.text().toLongLong();
      }

      TTStreamPoint pt(frame, TTStreamPoint::stringToType(type),
                        desc, confidence, duration);
      // Only a well-formed inclusive pair counts; anything else (one element
      // missing, negative, reversed - a hand-edited project file) leaves the
      // point on the frameIndex/duration estimate instead of feeding a bogus
      // range into a repair.
      if (audioFrameFrom >= 0 && audioFrameTo >= audioFrameFrom)
        pt.setAudioFrameRange(audioFrameFrom, audioFrameTo);
      points.append(pt);
    }
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
    addTextElement(elem, "Source", "markad");

    addTextElement(elem, "Path", logoData.markadPath);
  } else {
    addTextElement(elem, "Source", "manual");

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
QDomElement TTCutProjectData::writeSubtitleSection(QDomElement& parent, const QString& filePath, int order, const QString& language, int delayMs)
{
  QDomElement subtitle = xmlDocument->createElement("Subtitle");
  parent.appendChild(subtitle);

  addTextElement(subtitle, "Order", QString("%1").arg(order));

  addTextElement(subtitle, "Name", filePath);

  if (!language.isEmpty()) {
    addTextElement(subtitle, "Language", language);
  }

  if (delayMs != 0) {
    addTextElement(subtitle, "Delay", QString::number(delayMs));
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

  // Read optional Language and Delay elements (added in TTCut-ng 0.52+ and 0.81+)
  QString lang;
  int delayMs = 0;
  for (int n = 2; n < subtitleNodesList.size(); n++) {
    if (subtitleNodesList.at(n).nodeName() == "Language") {
      lang = subtitleNodesList.at(n).toElement().text();
    } else if (subtitleNodesList.at(n).nodeName() == "Delay") {
      delayMs = subtitleNodesList.at(n).toElement().text().toInt();
    }
  }

  qDebug("TTCutProjectData::parseSubtitleSection -> before doOpenSubtitleStream...");
  avData->doOpenSubtitleStream(avItem, name, order);
  if (!lang.isEmpty()) {
    avData->setPendingSubtitleLanguage(avItem, order, lang);
  }
  if (delayMs != 0) {
    avData->setPendingSubtitleDelay(avItem, order, delayMs);
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
  addElement("CutDirPath",    TTSettings::instance()->cutDirPath());
  addElement("CutVideoName",  TTSettings::instance()->cutVideoName());
  addElement("CutAddSuffix",  TTSettings::instance()->cutAddSuffix() ? "true" : "false");

  // Muxing — read from the working set (transient, per-cut/per-project).
  // The persistent App-Defaults (Settings dialog) deliberately do NOT round-
  // trip through .ttcut: a project carries the cut-time choice, the user's
  // App-Defaults stay sacrosanct.
  TTSettings* s = TTSettings::instance();
  addElement("OutputContainer",    QString::number(s->workingOutputContainer()));
  addElement("MkvCreateChapters",  s->workingMkvCreateChapters() ? "true" : "false");
  addElement("MkvChapterInterval", QString::number(s->workingMkvChapterInterval()));
  addElement("MuxDeleteES",        s->workingMuxDeleteES() ? "true" : "false");
  addElement("MuxMode",            QString::number(s->workingMuxMode()));
  addElement("Mpeg2Target",        QString::number(s->workingMpeg2Target()));
  addElement("AudioOnlyFormat",    QString::number(s->workingAudioOnlyFormat()));

  // Encoder (active codec values — transient working values, persisted here
  // because they live in TTSettings as in-memory only, not in QSettings)
  addElement("EncoderPreset",  QString::number(s->encoderPreset()));
  addElement("EncoderCrf",     QString::number(s->encoderCrf()));
  addElement("EncoderProfile", QString::number(s->encoderProfile()));
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
      if (!validated.isEmpty()) TTSettings::instance()->setCutDirPath(validated);
      else qWarning("parseSettingsSection: rejected unsafe CutDirPath '%s'",
                    qPrintable(value));
    }
    else if (name == "CutVideoName") {
      // Filename only — must not contain '/' or control chars.
      bool ok = !value.contains('/') && !value.contains('\\');
      for (QChar c : value) if (c.unicode() < 0x20 || c.unicode() == 0x7F) ok = false;
      if (ok) TTSettings::instance()->setCutVideoName(value);
      else qWarning("parseSettingsSection: rejected unsafe CutVideoName '%s'",
                    qPrintable(value));
    }
    else if (name == "CutAddSuffix")       TTSettings::instance()->setCutAddSuffix(value == "true");
    // Muxing
    // Mux/Audio — load into the working set (transient). The persistent
    // App-Defaults stay untouched: a project must not silently rewrite the
    // user's settings dialog values.
    else if (name == "OutputContainer")    TTSettings::instance()->setWorkingOutputContainer(value.toInt());
    else if (name == "MkvCreateChapters")  TTSettings::instance()->setWorkingMkvCreateChapters(value == "true");
    else if (name == "MkvChapterInterval") TTSettings::instance()->setWorkingMkvChapterInterval(value.toInt());
    else if (name == "MuxDeleteES")        TTSettings::instance()->setWorkingMuxDeleteES(value == "true");
    else if (name == "MuxMode")            TTSettings::instance()->setWorkingMuxMode(value.toInt());
    else if (name == "Mpeg2Target")        TTSettings::instance()->setWorkingMpeg2Target(value.toInt());
    else if (name == "AudioOnlyFormat")    TTSettings::instance()->setWorkingAudioOnlyFormat(value.toInt());
    // Encoder (transient working values — see serialiser comment above)
    else if (name == "EncoderPreset")      TTSettings::instance()->setEncoderPreset(value.toInt());
    else if (name == "EncoderCrf")         TTSettings::instance()->setEncoderCrf(value.toInt());
    else if (name == "EncoderProfile")     TTSettings::instance()->setEncoderProfile(value.toInt());
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
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


