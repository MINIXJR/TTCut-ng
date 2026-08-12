/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Diagnostic: what firstSequenceHeader() does with a header list that has no */
/* sequence header. Such a list is what a run reads back when a second        */
/* instance has overwritten the shared encode.m2v (see the 2026-08-12 design  */
/* note); before the range check it indexed past the end of the list and the  */
/* Q_ASSERT in QList::at aborted the whole process.                           */
/*                                                                            */
/* Usage: test_seqheader_missing <file.m2v>                                   */
/*   exit 0 = firstSequenceHeader() returned NULL (expected after the fix)    */
/*   exit 1 = it returned a header, or the list was unusable for this check   */
/*   SIGABRT = the unfixed behaviour                                          */
/*----------------------------------------------------------------------------*/
#include <QCoreApplication>
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

  TTMpeg2VideoStream stream(QFileInfo(QString::fromLocal8Bit(argv[1])));
  const int headers = stream.createHeaderList();
  printf("header list: %d entries\n", headers);

  // Non-vacuity: with an empty list firstSequenceHeader() throws on its own
  // (the size() == 0 guard), which is a different path and would prove
  // nothing about the range check.
  if (headers <= 0) {
    fprintf(stderr, "FAIL: the prepared file yields no headers at all - "
                    "this check needs a non-empty list without a sequence header\n");
    return 1;
  }

  TTSequenceHeader* seq = stream.headerList()->firstSequenceHeader();
  if (seq != nullptr) {
    fprintf(stderr, "FAIL: a sequence header was found - the file is not "
                    "the prepared one\n");
    return 1;
  }

  printf("PASS: firstSequenceHeader() returned NULL\n");
  return 0;
}
