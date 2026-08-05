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
#include "../data/ttcutparameter.h"

#include <QCoreApplication>
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
  TTSubtitleHeader* lastHeader = (TTSubtitleHeader*)header_list->at(header_list->count()-1);
  return lastHeader->endTime();
}

//! Cut the subtitle stream
void TTSrtSubtitleStream::cut(int start, int end, TTCutParameter* cp)
{
  int index = header_list->searchTimeIndex(start);
  cp->setCutOutIndex(cp->getCutInIndex()+end-start);
  TTFileBuffer* stream_buffer = cp->getTargetStreamBuffer();
  int picsWritten = cp->getNumPicturesWritten();
  int progress = 0;
  int offsett = cp->getCutInIndex()-start;

  emit statusReport(StatusReportArgs::Start, tr("Cutting subtitles"), header_list->searchTimeIndex(end) - index + 1);
  qApp->processEvents();

  while (index < header_list->count())
  {
    if (mAbort) {
      mAbort = false;
      throw TTAbortException("User abort request in TTSrtSubtitleStream::cut!");
    }
    TTSubtitleHeader* header = (TTSubtitleHeader*)header_list->at(index);
    if (header->startMSec() > end)
      return;

    picsWritten++;
    QTime subtitleStart  = header->startMSec() <= start ? QTime::fromMSecsSinceStartOfDay(start) : header->startTime();
    QTime subtitleEnd    = header->endMSec() <= end ? header->endTime() : QTime::fromMSecsSinceStartOfDay(end);
    QString subtitleCode = QString("%1\r\n%2 --> %3\r\n%4\r\n\r\n")
        .arg(picsWritten)
        .arg(subtitleStart.addMSecs(offsett).toString("hh:mm:ss,zzz"))
        .arg(subtitleEnd.addMSecs(offsett).toString("hh:mm:ss,zzz"))
        .arg(header->text());

    QByteArray utf8 = subtitleCode.toUtf8();
    stream_buffer->directWrite((quint8*)utf8.constData(), utf8.length());

    cp->setNumPicturesWritten(picsWritten);
    index++;
    progress++;
    emit statusReport(StatusReportArgs::Step, tr("Cutting subtitles"), progress);
    qApp->processEvents();
  }
  emit statusReport(StatusReportArgs::Step, tr("Copying subtitle segment"), progress);
  emit statusReport(StatusReportArgs::Finished, tr("Subtitle cut finished"), progress);
  qApp->processEvents();
}

//! Read subtitles
int TTSrtSubtitleStream::createHeaderList()
{
  header_list = new TTSubtitleHeaderList( 100 );

  try
  {
    emit statusReport(StatusReportArgs::Start, tr("Creating subtitle header list"), stream_buffer->size());

    QString lineEnd;
    quint8 byte = 0;
    quint8 lastByte = 0;
    while (!stream_buffer->atEnd())
    {
      stream_buffer->readByte(byte);
      if (byte == '\n')
      {
        lineEnd = lastByte == '\r' ? "\r\n" : "\n";
        break;
      }
      lastByte = byte;
    }
    stream_buffer->seekAbsolute(0);

    QString line;
    int counter = -1;

    while (!stream_buffer->atEnd())
    {
      while (line.isEmpty())
      {
        if (stream_buffer->atEnd())
          return header_list->count();
        line  = stream_buffer->readLine(lineEnd).simplified();
      }
      if (line.toInt() != counter + 1 && counter != -1)
        log->warningMsg("TTSrtSubtitleStream", __LINE__,
                        QString("Subtitles in %1 missing. Reading subtitle %2, last was %3.").arg(fileName()).arg(counter).arg(line));
      counter = line.toInt();

      line = stream_buffer->readLine(lineEnd).simplified();
      TTSubtitleHeader* header = new TTSubtitleHeader();
      header->setStartTime(QTime::fromString(line.left(12), "hh:mm:ss,zzz"));
      header->setEndTime(QTime::fromString(line.right(12), "hh:mm:ss,zzz"));

      QString text;
      do
      {
        line = stream_buffer->readLine(lineEnd);
        text.append(line);
        text.append("\r\n");
        if (text.size() > 65536) break;  // Limit subtitle text to 64KB
      }
      while (!line.isEmpty());
      while(text.right(2) == "\r\n")
        text = text.left(text.length()-2);
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
    emit statusReport(StatusReportArgs::Finished, tr("Subtitle header list created"), stream_buffer->position());
  }
  catch (TTFileBufferException)
  {
  }

  log->debugMsg(__FILE__, __LINE__, QString("header list created: %1").arg(header_list->count()));
  log->debugMsg(__FILE__, __LINE__, QString("abs stream length:   %1").arg(streamLengthTime().toString("hh:mm:ss.zzz")));

  return header_list->count();
}

