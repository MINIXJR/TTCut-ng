/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally (c) 2019 Minei3oat / github.com/Minei3oat                       */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTSRTSUBTITLESTREAM
// ----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Overview
// -----------------------------------------------------------------------------
//
//                               +- TTMpegAudioStream
//             +- TTAudioStream -|
//             |                 +- TTAC3AudioStream
// TTAVStream -|
//             |
//             +- TTVideoStream - TTMpeg2VideoStream
//             |
//             +- TTSubtitleStream - TTSrtSubtitleStream
//
// -----------------------------------------------------------------------------

#include "ttsrtsubtitlestream.h"
#include "ttsubtitleheaderlist.h"

#include "../common/istatusreporter.h"
#include "../common/ttexception.h"
#include "ttcutparameter.h"

#include <QRegularExpression>
#include <QStringDecoder>

namespace {

// TTFileBuffer::readLine (avstream/ttfilebuffer.cpp) maps each raw input
// byte 1:1 onto a QChar - de-facto Latin-1 decoding. SRT files produced by
// the ttcut-demux workflow are UTF-8, so multi-byte sequences (e.g. german
// umlauts) come out mangled ("ü" -> "Ã¼") unless corrected here. The
// byte->QChar mapping is lossless, so the original bytes can be recovered
// via toLatin1() and re-decoded as UTF-8. Genuinely Latin-1-encoded legacy
// SRT files are not valid UTF-8, so decoding them as UTF-8 fails and we
// fall back to the raw Latin-1 interpretation for those.
QString decodeSrtLine(const QString &rawLine)
{
  const QByteArray bytes = rawLine.toLatin1();
  QStringDecoder dec(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
  QString out = dec.decode(bytes);
  if (dec.hasError())
    return QString::fromLatin1(bytes);
  return out;
}

// hh:mm:ss,zzz --> hh:mm:ss,zzz, also with a dot for the comma, a one-digit
// hour, fewer millisecond digits and fields after the end time (position).
const QRegularExpression& srtTimingLine()
{
  static const QRegularExpression re(
      R"(^\s*(\d{1,2}):(\d{2}):(\d{2})[,.](\d{1,3})\s*-->\s*(\d{1,2}):(\d{2}):(\d{2})[,.](\d{1,3}))");
  return re;
}

// Milliseconds of one time from capture group `first` on (h, m, s, fraction).
int srtMSec(const QRegularExpressionMatch& m, int first)
{
  return ((m.captured(first).toInt() * 60 + m.captured(first + 1).toInt()) * 60
          + m.captured(first + 2).toInt()) * 1000
       + m.captured(first + 3).leftJustified(3, '0').toInt();
}

} // namespace

// /////////////////////////////////////////////////////////////////////////////
// -----------------------------------------------------------------------------
// *** TTSrtSubtitleStream: Srt subtitle stream class
// -----------------------------------------------------------------------------
// /////////////////////////////////////////////////////////////////////////////

//! Constructor with QFileInfo and start position
TTSrtSubtitleStream::TTSrtSubtitleStream(const QFileInfo &f_info)
  :TTSubtitleStream(f_info)
{
  log = TTMessageLogger::getInstance();
}

//! Destructor
TTSrtSubtitleStream::~TTSrtSubtitleStream()
{
}

//! Return the stream type
TTAVTypes::AVStreamType TTSrtSubtitleStream::streamType() const
{
  return TTAVTypes::srt_subtitle;
}

//! Return the stream length as QTime
QTime TTSrtSubtitleStream::streamLengthTime()
{
  if (!header_list || header_list->count() == 0) return QTime(0, 0, 0, 0);
  const TTSubtitleHeader* lastHeader = static_cast<const TTSubtitleHeader*>(header_list->at(header_list->count()-1));
  return lastHeader->endTime();
}

//! Cut the subtitle stream
//!
//! Deliberately without qApp->processEvents(). The three calls that used to sit
//! next to the statusReport emissions were there to keep the window painting
//! during a synchronous cut, and both of today's task callers have moved off
//! the GUI thread (TTCutPreviewTask, TTH26xCutTask); pumping a pool thread's
//! event queue mid-write is at best pointless. They were also
//! pumping for nothing even on the GUI thread: statusReport is connected only by
//! TTOpenSubtitleTask, which disconnects again in its cleanUp(), so by cut()
//! time the signal has no receiver at all and no progress message is produced.
//! The remaining GUI-thread caller (the MPEG-2 branch of TTAVData::onDoCut)
//! therefore loses no feedback - only the repaint during a text-file write over
//! an in-memory header list.
void TTSrtSubtitleStream::cut(int start, int end, TTCutParameter* cp)
{
  int index = header_list->searchTimeIndex(start);
  cp->setCutOutIndex(cp->getCutInIndex()+end-start);
  TTFileBuffer* stream_buffer = cp->getTargetStreamBuffer();
  int picsWritten = cp->getNumPicturesWritten();
  int progress = 0;
  int offsett = cp->getCutInIndex()-start;

  emit statusReport(StatusReportArgs::Start, tr("Cutting subtitles"), header_list->searchTimeIndex(end) - index + 1);

  while (index < header_list->count())
  {
    if (mAbort) {
      mAbort = false;
      throw TTAbortException("User abort request in TTSrtSubtitleStream::cut!");
    }
    const TTSubtitleHeader* header = static_cast<const TTSubtitleHeader*>(header_list->at(index));
    if (header->startMSec() > end)
      return;
    // Ended before this segment: after the last cue searchTimeIndex() answers
    // with that last one, which must not be written again.
    if (header->endMSec() < start) {
      index++;
      continue;
    }

    picsWritten++;
    QTime subtitleStart  = header->startMSec() <= start ? QTime::fromMSecsSinceStartOfDay(start) : header->startTime();
    QTime subtitleEnd    = header->endMSec() <= end ? header->endTime() : QTime::fromMSecsSinceStartOfDay(end);
    QString subtitleCode = QString("%1\r\n%2 --> %3\r\n%4\r\n\r\n")
        .arg(picsWritten)
        .arg(subtitleStart.addMSecs(offsett).toString("hh:mm:ss,zzz"))
        .arg(subtitleEnd.addMSecs(offsett).toString("hh:mm:ss,zzz"))
        .arg(header->text());

    QByteArray utf8 = subtitleCode.toUtf8();
    stream_buffer->directWrite(reinterpret_cast<const quint8*>(utf8.constData()), utf8.length());

    cp->setNumPicturesWritten(picsWritten);
    index++;
    progress++;
    emit statusReport(StatusReportArgs::Step, tr("Cutting subtitles"), progress);
  }
  emit statusReport(StatusReportArgs::Step, tr("Copying subtitle segment"), progress);
  emit statusReport(StatusReportArgs::Finished, tr("Subtitle cut finished"), progress);
}

//! Read subtitles
int TTSrtSubtitleStream::createHeaderList()
{
  header_list = new TTSubtitleHeaderList( 100 );

  try
  {
    emit statusReport(StatusReportArgs::Start, tr("Creating subtitle header list"), stream_buffer->size());

    // Lines split on LF and a CR before it is dropped: CRLF, LF and files
    // mixing both read the same.
    auto nextLine = [this]() {
      QString line = stream_buffer->readLine("\n");
      if (line.endsWith('\r')) line.chop(1);
      return line;
    };

    int counter = -1;
    while (!stream_buffer->atEnd())
    {
      QString line;
      while (line.isEmpty() && !stream_buffer->atEnd())
        line = nextLine().simplified();
      if (line.isEmpty())
        break;
      if (line.toInt() != counter + 1 && counter != -1)
        log->warningMsg("TTSrtSubtitleStream", __LINE__,
                        QString("Subtitles in %1 missing. Reading subtitle %2, last was %3.").arg(fileName()).arg(counter).arg(line));
      counter = line.toInt();

      const QString timing = nextLine().simplified();

      QString text;
      do
      {
        line = nextLine();
        text.append(line);
        text.append("\r\n");
        if (text.size() > 65536) break;  // Limit subtitle text to 64KB
      }
      while (!line.isEmpty());
      while(text.right(2) == "\r\n")
        text = text.left(text.length()-2);

      const QRegularExpressionMatch m = srtTimingLine().match(timing);
      if (!m.hasMatch()) {
        log->warningMsg("TTSrtSubtitleStream", __LINE__,
                        QString("%1: subtitle %2 skipped, unreadable timing line \"%3\"")
                            .arg(fileName()).arg(counter).arg(timing));
        continue;
      }
      TTSubtitleHeader* header = new TTSubtitleHeader();
      header->setStartTime(QTime::fromMSecsSinceStartOfDay(srtMSec(m, 1)));
      header->setEndTime(QTime::fromMSecsSinceStartOfDay(srtMSec(m, 5)));
      // Decode the fully assembled text block, after the 64KB raw-byte cap
      // above, so the cap continues to operate on the same raw byte count
      // as before this fix (decoding can shrink multi-byte UTF-8 sequences
      // into fewer QChars, which would silently loosen the cap if applied
      // earlier). The index/timestamp lines are pure ASCII and need no
      // decoding.
      header->setText(decodeSrtLine(text));

      header_list->append(header);

      emit statusReport(StatusReportArgs::Step, tr("Creating subtitle header list"), stream_buffer->position());
    }
    // File order is not always time order (merged or hand-edited files);
    // lookup and cut need it.
    static_cast<TTSubtitleHeaderList*>(header_list)->sort();
    emit statusReport(StatusReportArgs::Finished, tr("Subtitle header list created"), stream_buffer->position());
  }
  catch (TTFileBufferException)
  {
  }

  logHeaderListCreated(header_list->count(), "hh:mm:ss.zzz");

  return header_list->count();
}

