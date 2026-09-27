// Gate for the SRT subtitle core (audit run 14, map subtitle-core.md). It
// writes small .srt files into <workdir> and checks:
//   H1  a keep segment after the last cue writes nothing (it wrote that cue
//       again, end before start, text from a removed part);
//   H2  timing lines with a dot, a one-digit hour or fields after the end
//       time are read (they became cues at 00:00:00);
//   H4  a file mixing CRLF and LF keeps all cues (it kept the first);
//   H6  cues out of time order are found by the overlay lookup;
//   H3  overlapping cues are shown together by the overlay lookup.
//
//   usage: test_subtitle_core <workdir>
//
// Build via `cmake --build build --target test_subtitle_core`.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>

#include "avstream/ttcutparameter.h"
#include "avstream/ttfilebuffer.h"
#include "avstream/ttsrtsubtitlestream.h"
#include "avstream/ttsubtitleheaderlist.h"

static int failures = 0;
static void check(bool ok, const QString& what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
  if (!ok) ++failures;
}

static QString writeSrt(const QString& dir, const QString& name, const QByteArray& data)
{
  const QString path = QDir(dir).absoluteFilePath(name);
  QFile f(path);
  f.open(QIODevice::WriteOnly | QIODevice::Truncate);
  f.write(data);
  return path;
}

// "start..end text" per cue, one line each
static QString cues(TTSubtitleHeaderList* hl)
{
  QStringList out;
  for (int i = 0; i < hl->count(); ++i) {
    TTSubtitleHeader* h = hl->subtitleHeaderAt(i);
    out << QString("%1..%2 %3").arg(h->startMSec()).arg(h->endMSec()).arg(h->text());
  }
  return out.join(" | ");
}

// What the picture window shows at t.
static QString overlayAt(TTSubtitleHeaderList* hl, int t)
{
  return hl->textAt(t);
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: %s <workdir>\n", argv[0]); return 2; }
  const QString dir = QString::fromLocal8Bit(argv[1]);
  QDir().mkpath(dir);

  // H1
  {
    const QString src = writeSrt(dir, "basic.srt",
        "1\r\n00:00:01,000 --> 00:00:02,000\r\nEins\r\n\r\n"
        "2\r\n00:00:03,000 --> 00:00:04,000\r\nZwei\r\n\r\n"
        "3\r\n00:00:05,000 --> 00:00:06,000\r\nDrei\r\n\r\n");
    TTSrtSubtitleStream s{QFileInfo(src)};
    s.createHeaderList();
    const QString out = QDir(dir).absoluteFilePath("basic_cut.srt");
    QFile::remove(out);
    {
      TTFileBuffer tgt(out, QIODevice::WriteOnly);
      TTCutParameter cp(&tgt);
      cp.setNumPicturesWritten(0); cp.setCutInIndex(0); cp.setCutOutIndex(0);
      tgt.open();
      const int seg[2][2] = {{0, 2499}, {7000, 8999}};
      for (const auto& g : seg) {
        s.cut(g[0], g[1], &cp);
        cp.setCutInIndex(cp.getCutOutIndex() + 1);
      }
      tgt.close();
    }
    QFile f(out); f.open(QIODevice::ReadOnly);
    const QString written = QString::fromUtf8(f.readAll());
    check(written == "1\r\n00:00:01,000 --> 00:00:02,000\r\nEins\r\n\r\n",
          "H1: a keep segment after the last cue writes nothing - got: "
          + QString(written).replace("\r\n", "|"));
  }

  // H2
  {
    const QString src = writeSrt(dir, "formats.srt",
        "1\n00:00:01.000 --> 00:00:02.000\nPunkt\n\n"
        "2\n0:00:03,000 --> 0:00:04,000\nEinstellig\n\n"
        "3\n00:00:05,000 --> 00:00:06,000 X1:100 X2:200 Y1:10 Y2:20\nKoordinaten\n\n");
    TTSrtSubtitleStream s{QFileInfo(src)};
    s.createHeaderList();
    const QString got = cues(static_cast<TTSubtitleHeaderList*>(s.headerList()));
    check(got == "1000..2000 Punkt | 3000..4000 Einstellig | 5000..6000 Koordinaten",
          "H2: other timing-line shapes are read - got: " + got);
  }

  // H4
  {
    const QString src = writeSrt(dir, "mixed.srt",
        "1\r\n00:00:01,000 --> 00:00:02,000\r\nEins\r\n\r\n"
        "2\n00:00:03,000 --> 00:00:04,000\nZwei\n\n"
        "3\n00:00:05,000 --> 00:00:06,000\nDrei\n\n");
    TTSrtSubtitleStream s{QFileInfo(src)};
    s.createHeaderList();
    const QString got = cues(static_cast<TTSubtitleHeaderList*>(s.headerList()));
    check(got == "1000..2000 Eins | 3000..4000 Zwei | 5000..6000 Drei",
          "H4: mixed line ends keep all cues - got: " + got);
  }

  // H6
  {
    const QString src = writeSrt(dir, "order.srt",
        "1\n00:00:05,000 --> 00:00:06,000\nSpaet\n\n"
        "2\n00:00:01,000 --> 00:00:02,000\nFrueh\n\n");
    TTSrtSubtitleStream s{QFileInfo(src)};
    s.createHeaderList();
    auto* hl = static_cast<TTSubtitleHeaderList*>(s.headerList());
    check(overlayAt(hl, 1500) == "Frueh" && overlayAt(hl, 5500) == "Spaet",
          QString("H6: cues out of order are found - 1500: \"%1\", 5500: \"%2\"")
              .arg(overlayAt(hl, 1500), overlayAt(hl, 5500)));
  }

  // H3
  {
    const QString src = writeSrt(dir, "overlap.srt",
        "1\n00:00:01,000 --> 00:00:05,000\nLang\n\n"
        "2\n00:00:02,000 --> 00:00:03,000\nKurz\n\n");
    TTSrtSubtitleStream s{QFileInfo(src)};
    s.createHeaderList();
    auto* hl = static_cast<TTSubtitleHeaderList*>(s.headerList());
    const QString at = overlayAt(hl, 2500);
    check(at == "Lang\r\nKurz" && overlayAt(hl, 4000) == "Lang" && overlayAt(hl, 6000).isEmpty(),
          "H3: overlapping cues are shown together - at 2500: " + QString(at).replace("\r\n", "|"));
  }

  printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
  return failures == 0 ? 0 : 1;
}
