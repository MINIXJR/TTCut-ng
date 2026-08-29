/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Diagnostic: createHeaderList() against a stream that ends mid-data.        */
/*                                                                            */
/* Two failure modes this covers, both seen on a real recording whose last    */
/* byte is 0xB8 (group_start_code):                                           */
/*                                                                            */
/*   1. Non-termination. readByte(quint8*,int) used to reset readPos to       */
/*      writePos in its EOF catch, which clears the condition atEnd() tests   */
/*      (isAtEnd && readPos > writePos). The parser then re-read the same      */
/*      trailing byte forever - 26.7 million log lines in 45 s, memory        */
/*      climbing with one header object per turn. The gate script measures    */
/*      this with a timeout; this harness only has to return.                 */
/*                                                                            */
/*   2. Phantom headers. A header whose readHeader() hit EOF is still added   */
/*      to the list, carrying parsed uninitialised stack data. Every entry    */
/*      must therefore sit on a real 00 00 01 <type> start code in the file.  */
/*                                                                            */
/* Usage: test_headerlist_eof <file.m2v>                                      */
/*   exit 0 = list terminates and every header sits on a real start code      */
/*   exit 1 = a phantom header is in the list                                 */
/*   exit 2 = usage / the file yields no headers at all (check is vacuous)    */
/*----------------------------------------------------------------------------*/
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>

#include "avstream/ttmpeg2videostream.h"
#include "avstream/ttvideoheaderlist.h"

#include <cstdio>

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);

  if (argc < 2) {
    fprintf(stderr, "usage: %s <file.m2v>\n", argv[0]);
    return 2;
  }

  const QString path = QString::fromLocal8Bit(argv[1]);

  QFile raw(path);
  if (!raw.open(QIODevice::ReadOnly)) {
    fprintf(stderr, "cannot open %s\n", qPrintable(path));
    return 2;
  }
  const qint64 fileSize = raw.size();

  TTMpeg2VideoStream stream{QFileInfo(path)};
  const int headers = stream.createHeaderList();
  printf("file size: %lld / header list: %d entries\n",
         (long long)fileSize, headers);

  if (headers <= 0) {
    fprintf(stderr, "FAIL: no headers at all - this check needs a non-empty list\n");
    return 2;
  }

  int phantoms = 0;
  for (int i = 0; i < headers; i++) {
    const quint64 off  = stream.headerList()->headerAt(i)->headerOffset();
    const quint8  type = stream.headerList()->headerTypeAt(i);

    // A header must sit on its own start code: 00 00 01 <type> at headerOffset.
    if ((qint64)off + 4 > fileSize) {
      printf("PHANTOM #%d type 0x%02x at offset %llu: past EOF\n",
             i, type, (unsigned long long)off);
      phantoms++;
      continue;
    }

    char sc[4];
    raw.seek((qint64)off);
    if (raw.read(sc, 4) != 4) {
      printf("PHANTOM #%d type 0x%02x at offset %llu: short read\n",
             i, type, (unsigned long long)off);
      phantoms++;
      continue;
    }

    if (sc[0] != 0x00 || sc[1] != 0x00 || sc[2] != 0x01 || (quint8)sc[3] != type) {
      printf("PHANTOM #%d type 0x%02x at offset %llu: file has %02x %02x %02x %02x\n",
             i, type, (unsigned long long)off,
             (quint8)sc[0], (quint8)sc[1], (quint8)sc[2], (quint8)sc[3]);
      phantoms++;
    }
  }

  if (phantoms > 0) {
    fprintf(stderr, "FAIL: %d phantom header(s) in the list\n", phantoms);
    return 1;
  }

  printf("PASS: %d headers, all on real start codes\n", headers);
  return 0;
}
