/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Diagnostic: dump every start code TTFileBuffer's scanner reports for a     */
/* file, with offsets — isolates EOF phantom-header behaviour.               */
/*----------------------------------------------------------------------------*/

#include "../../avstream/ttfilebuffer.h"

#include <cstdio>

int main(int argc, char** argv)
{
  if (argc < 2) {
    fprintf(stderr, "usage: %s <file.m2v>\n", argv[0]);
    return 1;
  }

  TTFileBuffer buf(argv[1], QIODevice::ReadOnly);
  buf.open();
  fprintf(stderr, "file size: %llu\n", (unsigned long long)buf.size());

  int found = 0;
  try {
    while (!buf.atEnd()) {
      buf.nextStartCodeTS();
      quint64 pos = buf.position();
      quint8 type = 0xFF;
      buf.readByte(type);
      // The scanner leaves position just past "00 00 01"; the type byte we
      // read is the 4th start-code byte. Start-code offset = pos - 3.
      printf("start code 0x%02x at offset %llu (pos after scan %llu)\n",
             type, (unsigned long long)(pos >= 3 ? pos - 3 : pos),
             (unsigned long long)pos);
      if (++found > 200) { printf("... (capped)\n"); break; }
    }
  } catch (...) {
    printf("(EOF exception — scan ended)\n");
  }
  printf("total reported: %d\n", found);
  return 0;
}
