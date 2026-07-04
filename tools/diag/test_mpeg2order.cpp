/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Diag: TTMkvMergeProvider::buildMpeg2DisplayOrder — valid + fallback paths. */
/* Writes two synthetic mini-ES files (raw start codes, no real video):      */
/*   valid:   GOP[tref 2,0,1] GOP[tref 1,0]  -> expect [2,0,1, 4,3]          */
/*   broken:  GOP[tref 0,0,2] (duplicate)    -> expect empty (fallback)      */
/*----------------------------------------------------------------------------*/
#include <QCoreApplication>
#include <QFile>
#include <QVector>
#include <cstdio>
#include "extern/ttmkvmergeprovider.h"

static void addPic(QByteArray& b, int tref)
{
    b.append('\x00'); b.append('\x00'); b.append('\x01'); b.append('\x00');
    // temporal_reference: 10 bits, MSB-aligned in the next two bytes
    b.append((char)((tref >> 2) & 0xFF));
    b.append((char)((tref & 0x03) << 6));
    b.append('\x00');  // padding so the scanner's 5-byte lookahead is satisfied
}
static void addGop(QByteArray& b)
{
    b.append('\x00'); b.append('\x00'); b.append('\x01'); b.append('\xB8');
    b.append(4, '\x00');
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int fails = 0;

    QByteArray valid;
    addGop(valid); addPic(valid, 2); addPic(valid, 0); addPic(valid, 1);
    addGop(valid); addPic(valid, 1); addPic(valid, 0);
    QFile fv("/tmp/mpeg2order_valid.bin");
    fv.open(QIODevice::WriteOnly); fv.write(valid); fv.close();

    QVector<int> expect{2, 0, 1, 4, 3};
    QVector<int> got = TTMkvMergeProvider::buildMpeg2DisplayOrder(fv.fileName());
    if (got == expect) printf("PASS: valid stream -> %d entries as expected\n", got.size());
    else { printf("FAIL: valid stream, got %d entries\n", got.size()); fails++; }

    QByteArray broken;
    addGop(broken); addPic(broken, 0); addPic(broken, 0); addPic(broken, 2);
    QFile fb("/tmp/mpeg2order_broken.bin");
    fb.open(QIODevice::WriteOnly); fb.write(broken); fb.close();

    got = TTMkvMergeProvider::buildMpeg2DisplayOrder(fb.fileName());
    if (got.isEmpty()) printf("PASS: broken permutation -> empty list (fallback)\n");
    else { printf("FAIL: broken stream returned %d entries\n", got.size()); fails++; }

    QFile::remove(fv.fileName()); QFile::remove(fb.fileName());
    return fails;
}
