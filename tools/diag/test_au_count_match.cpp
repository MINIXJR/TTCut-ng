/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Diagnostic: the two access-unit splitters must count the same pictures.    */
/* TTNaluParser (smart cut) and TTFrameIndexer (libav packets, source of the  */
/* display-order map) each split the ES on their own; the smart cut refuses   */
/* to cut when the map and the parser disagree. Prints both counts and every  */
/* parser access unit that holds no slice NAL.                                */
/* Usage: test_au_count_match <es-file> [<expected count>]                    */
/*----------------------------------------------------------------------------*/

#include "../../avstream/ttframeindexer.h"
#include "../../avstream/ttnaluparser.h"

#include <QCoreApplication>

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: %s <es-file> [<expected count>]\n", argv[0]); return 2; }
  const QString path = QString::fromLocal8Bit(argv[1]);

  TTNaluParser parser;
  if (!parser.openFile(path) || !parser.parseFile()) { printf("FAIL: parser could not read %s\n", argv[1]); return 1; }
  TTFrameIndexer indexer;
  if (!indexer.build(path, -1, nullptr)) { printf("FAIL: indexer: %s\n", qPrintable(indexer.lastError())); return 1; }

  const int parserCount  = parser.accessUnitCount();
  const int indexerCount = static_cast<int>(indexer.bundle().index.size());
  int withoutSlice = 0;
  for (int i = 0; i < parserCount; ++i) {
    const TTAccessUnit au = parser.accessUnitAt(i);
    bool hasSlice = false;
    for (int nalIndex : au.nalIndices)
      if (parser.nalUnitAt(nalIndex).isSlice) { hasSlice = true; break; }
    if (hasSlice) continue;
    ++withoutSlice;
    printf("access unit %d holds no slice (offset %lld, %lld bytes)\n", i,
           static_cast<long long>(au.startOffset), static_cast<long long>(au.endOffset - au.startOffset));
  }
  printf("parser %d, indexer %d, access units without a slice %d\n", parserCount, indexerCount, withoutSlice);

  int failures = 0;
  if (parserCount != indexerCount) { printf("FAIL: the two splitters count differently\n"); ++failures; }
  if (argc > 2 && parserCount != atoi(argv[2])) { printf("FAIL: parser counts %d, expected %s\n", parserCount, argv[2]); ++failures; }
  if (failures == 0) printf("PASS: both splitters count %d pictures\n", parserCount);
  return failures == 0 ? 0 : 1;
}
