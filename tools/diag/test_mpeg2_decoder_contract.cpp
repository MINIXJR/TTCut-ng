// Gate for the TTMpeg2Decoder contracts of audit run 13 (map
// mpeg2-decoder.md):
//   H1  every decoder has its own TFrameInfo (it was one file-scope static
//       shared by all instances, across threads);
//   H2  moveToFrameIndex reaches the right picture when a GOP has no
//       sequence header of its own (it stopped at the first I picture after
//       the previous header: the whole GOP showed pictures of the GOP before);
//   H4  a quick-jump thumbnail is the picture at its frame index (the worker
//       decoded one picture more: it showed index + 1);
//   H5  a null header list is refused with ArgumentNull (only both lists
//       null were, one null list crashed later).
//
//   usage: test_mpeg2_decoder_contract <file.m2v> <same-without-one-seq-header.m2v>
//
// Build via `cmake --build build --target test_mpeg2_decoder_contract`.
#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QMap>

#include <cstdio>

#include "avstream/ttmpeg2videostream.h"
#include "avstream/ttvideoindexlist.h"
#include "avstream/ttavtypes.h"
#include "gui/ttquickjumpworker.h"
#include "mpeg2decoder/ttmpeg2decoder.h"

static int failures = 0;
static void check(bool ok, const QString& what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
  if (!ok) ++failures;
}

static TTMpeg2VideoStream* openStream(const QString& path)
{
  auto* vs = new TTMpeg2VideoStream(QFileInfo(path));
  vs->createHeaderList();
  vs->createIndexList();
  vs->indexList()->sortDisplayOrder();
  return vs;
}

// The RGB32 picture at pos, copied out of the decoder's buffer.
static QImage pictureAt(TTMpeg2Decoder& d, int pos)
{
  d.moveToFrameIndex(pos);
  const TFrameInfo* fi = d.getFrameInfo();
  if (!fi || !fi->Y) return QImage();
  return QImage(fi->Y, fi->width, fi->height, QImage::Format_RGB32).copy();
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc < 3) {
    fprintf(stderr, "usage: %s <file.m2v> <same-without-one-seq-header.m2v>\n", argv[0]);
    return 2;
  }
  TTMpeg2VideoStream* vs = openStream(QString::fromLocal8Bit(argv[1]));
  TTMpeg2VideoStream* stripped = openStream(QString::fromLocal8Bit(argv[2]));
  const int n = vs->frameCount();

  // H1: two decoders, two frame structs
  {
    TTMpeg2Decoder a(vs->filePath(), vs->indexList(), vs->headerList());
    TTMpeg2Decoder b(vs->filePath(), vs->indexList(), vs->headerList());
    a.moveToFrameIndex(10);
    b.moveToFrameIndex(20);
    check(a.getFrameInfo() && b.getFrameInfo() && a.getFrameInfo() != b.getFrameInfo(),
          "H1: two decoders return two TFrameInfo structs");
  }

  // H2: every position, original vs one sequence header removed
  {
    TTMpeg2Decoder a(vs->filePath(), vs->indexList(), vs->headerList());
    TTMpeg2Decoder b(stripped->filePath(), stripped->indexList(), stripped->headerList());
    int differ = 0, first = -1;
    for (int p = 0; p < n; ++p) {
      if (pictureAt(a, p) != pictureAt(b, p)) { if (first < 0) first = p; ++differ; }
    }
    check(stripped->frameCount() == n && differ == 0,
          QString("H2: GOP without its own sequence header - %1 of %2 positions differ (first %3)")
              .arg(differ).arg(n).arg(first));
  }

  // H4: quick-jump thumbnails (half size - at full size QImage::scaled returns a
  // shallow copy over the worker's decoder buffer, gone with the worker)
  // against the decoder picture scaled the same way
  {
    TTMpeg2Decoder d(vs->filePath(), vs->indexList(), vs->headerList());
    const QList<int> frames = {0, 12, 250, 1000, 2012};
    const QImage probe = pictureAt(d, 0);
    const QSize thumbSize = probe.size() / 2;
    TTQuickJumpWorker worker(vs->filePath(), TTAVTypes::mpeg2_demuxed_video, frames,
                             thumbSize, vs->indexList(), vs->headerList());
    worker.setAutoDelete(false);
    QMap<int, QImage> thumbs;
    QObject::connect(&worker, &TTQuickJumpWorker::thumbnailReady,
                     [&](int idx, const QImage& img) { thumbs[idx] = img; });
    worker.runSynchron();
    int wrong = 0;
    QStringList detail;
    for (int f : frames) {
      const QImage got = thumbs.value(f);
      if (got.isNull()) { ++wrong; detail << QString("%1 missing").arg(f); continue; }
      const QImage want = pictureAt(d, f).scaled(thumbSize, Qt::IgnoreAspectRatio,
                                                 Qt::SmoothTransformation);
      if (got != want) { ++wrong; detail << QString::number(f); }
    }
    check(thumbs.size() == frames.size() && wrong == 0,
          QString("H4: quick-jump thumbnail = picture at its index (%1 of %2 wrong: %3)")
              .arg(wrong).arg(frames.size()).arg(detail.join(",")));
  }

  // H5: one null list is refused
  {
    bool refused = false;
    try {
      TTMpeg2Decoder d(vs->filePath(), vs->indexList(), nullptr);
    } catch (const TTMpeg2DecoderException&) {
      refused = true;
    }
    check(refused, "H5: a null header list throws TTMpeg2DecoderException");
  }

  printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
  return failures == 0 ? 0 : 1;
}
