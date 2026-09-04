// Dumps one TTFrameIndexBundle field by field so two builds can be diffed.
//   usage: test_frameindex_dump <file.264|file.265>
// The trailing "d <decode> <display>" lines are the display-order map from
// the bundle.
#include <QCoreApplication>
#include <QString>
#include <cstdio>

#include "avstream/ttframeindex.h"
#include "avstream/ttframeindexer.h"

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: %s <es-file>\n", argv[0]); return 2; }
  const QString path = QString::fromLocal8Bit(argv[1]);

  TTFrameIndexBundle b;
  TTFrameIndexer ix;
  if (!ix.build(path, -1, nullptr)) { fprintf(stderr, "build failed: %s\n", qPrintable(ix.lastError())); return 1; }
  b = ix.bundle();

  printf("frames=%d gops=%d raw=%d rawmap=%d paff=%d mbsonly=%d log2maxfn=%d\n",
         static_cast<int>(b.index.size()), static_cast<int>(b.gops.size()), b.rawPacketCount,
         static_cast<int>(b.rawToMerged.size()), b.isPAFF ? 1 : 0, b.frameMbsOnlyFlag ? 1 : 0, b.log2MaxFrameNum);
  for (const TTFrameInfo& f : b.index)
    printf("f %d pts=%lld dts=%lld off=%lld size=%lld type=%d key=%d gop=%d field=%d poc=%d idr=%d lead=%d pfn=%d bot=%d\n",
           f.frameIndex, (long long)f.pts, (long long)f.dts, (long long)f.fileOffset, (long long)f.packetSize,
           f.frameType, f.isKeyframe ? 1 : 0, f.gopIndex, f.isFieldCoded ? 1 : 0, f.poc, f.isIDR ? 1 : 0,
           f.isDroppedLeading ? 1 : 0, f.paffFrameNum, f.isBottomField ? 1 : 0);
  for (const TTGOPInfo& g : b.gops)
    printf("g %d %d-%d pts=%lld..%lld closed=%d\n", g.gopIndex, g.startFrame, g.endFrame,
           (long long)g.startPts, (long long)g.endPts, g.isClosed ? 1 : 0);
  for (int i = 0; i < b.rawToMerged.size(); ++i) printf("r %d %d\n", i, b.rawToMerged[i]);

  const TTDisplayOrderMap& m = b.displayMap;
  printf("map valid=%d count=%d\n", m.isValid() ? 1 : 0, m.isValid() ? m.displayCount() : 0);
  for (int i = 0; i < b.index.size(); ++i)
    printf("d %d %d\n", i, m.isValid() ? m.decodeToDisplay(i) : i);
  return 0;
}
