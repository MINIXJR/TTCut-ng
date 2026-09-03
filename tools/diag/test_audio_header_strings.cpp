// What does the audio list actually get from the first audio header?
//
// TTAudioItem::getBitrate() and its siblings call the string getters through
// TTAudioHeader* (TTAudioStream::headerAt). The AC3 and MPEG audio headers
// declare those getters with a different return type than the base
// (QString& vs const QString&), so they hide instead of override. This probe
// prints what arrives through the base pointer next to what the concrete
// class computes.
//
//   usage: test_audio_header_strings <file.mp2|file.ac3>
#include <QCoreApplication>
#include <QFileInfo>

#include "avstream/ttac3audioheader.h"
#include "avstream/ttac3audiostream.h"
#include "avstream/ttmpegaudioheader.h"
#include "avstream/ttmpegaudiostream.h"

#include <cstdio>

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: %s <file.mp2|file.ac3>\n", argv[0]); return 2; }

  QFileInfo fi(QString::fromLocal8Bit(argv[1]));
  TTAudioStream* stream = nullptr;
  const QString suffix = fi.suffix().toLower();
  if (suffix == "ac3")                          stream = new TTAC3AudioStream(fi);
  else if (suffix == "mp2" || suffix == "mp3")  stream = new TTMPEGAudioStream(fi);
  if (!stream) { fprintf(stderr, "unsupported: %s\n", qPrintable(suffix)); return 2; }

  const int n = stream->createHeaderList();
  TTAudioHeader* base = stream->headerAt(0);
  printf("file=%s headers=%d\n", qPrintable(fi.fileName()), n);
  printf("via TTAudioHeader*: desc=\"%s\" mode=\"%s\" bitrate=\"%s\" samplerate=\"%s\"\n",
         qPrintable(base->descString()), qPrintable(base->modeString()),
         qPrintable(base->bitRateString()), qPrintable(base->sampleRateString()));
  if (auto* h = dynamic_cast<TTAC3AudioHeader*>(base))
    printf("via TTAC3AudioHeader*: desc=\"%s\" mode=\"%s\" bitrate=\"%s\" samplerate=\"%s\"\n",
           qPrintable(h->descString()), qPrintable(h->modeString()),
           qPrintable(h->bitRateString()), qPrintable(h->sampleRateString()));
  if (auto* h = dynamic_cast<TTMpegAudioHeader*>(base))
    printf("via TTMpegAudioHeader*: desc=\"%s\" mode=\"%s\" bitrate=\"%s\" samplerate=\"%s\"\n",
           qPrintable(h->descString()), qPrintable(h->modeString()),
           qPrintable(h->bitRateString()), qPrintable(h->sampleRateString()));
  delete stream;
  return 0;
}
