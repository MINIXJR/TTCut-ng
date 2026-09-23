/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTBITSTREAM_H
#define TTBITSTREAM_H

#include <QByteArray>
#include <cstdint>

// Bit-level access to H.264/H.265 RBSP data (emulation prevention removed):
// the one reader, writer and emulation-prevention pair of the code base.
//
// Reader on bad input: a read past the end sets a sticky error flag and the
// missing bits read as 0; once the flag is set ue()/se() return 0; more than
// 31 leading zeros also set the flag and return 0. A return value alone does
// not end every syntax loop (ref_pic_list_modification only exits on idc 3),
// so parsers that loop on read values must also stop on error()/atEnd().
class TTBitReader
{
public:
    TTBitReader(const uint8_t* data, int size);
    explicit TTBitReader(const QByteArray& rbsp);   // rbsp must outlive the reader
    TTBitReader(QByteArray&&) = delete;               // a temporary would dangle

    uint32_t bits(int n);                            // 0 <= n <= 32, MSB first
    bool     flag() { return bits(1) != 0; }
    uint32_t ue();
    int32_t  se();
    void     skip(int n);

    int  pos() const         { return mPos; }
    void setPos(int bitPos)  { mPos = bitPos; }
    int  sizeBits() const    { return mSizeBits; }
    bool byteAligned() const { return (mPos & 7) == 0; }
    bool error() const       { return mError; }
    bool atEnd() const       { return mPos >= mSizeBits; }

private:
    const uint8_t* mData;
    int  mSizeBits;
    int  mPos = 0;
    bool mError = false;
};

// Appending writer; the last byte is zero-padded, so data() equals what a
// zero-initialised buffer trimmed to (numBits()+7)/8 bytes would hold.
class TTBitWriter
{
public:
    void bits(uint32_t v, int n);                    // 0 <= n <= 32, MSB first
    void flag(bool b) { bits(b ? 1u : 0u, 1); }
    void ue(uint32_t v);
    void se(int32_t v);                              // |v| <= 2^31 - 1
    void copyBits(TTBitReader& r, int n);
    void alignWith(int bit);                         // pad to the next byte boundary
    void appendBytes(const uint8_t* p, int n);
    void rbspTrailingBits();                         // stop bit + zero padding

    int  numBits() const     { return mNumBits; }
    bool byteAligned() const { return (mNumBits & 7) == 0; }
    QByteArray data() const  { return mBytes; }

private:
    QByteArray mBytes;
    int mNumBits = 0;
};

// Overwrite a fixed-width field in place (frame_num, poc_lsb); stops
// silently at the end of the buffer.
void ttOverwriteBits(uint8_t* data, int size, int bitPos, uint32_t v, int n);

// Emulation prevention (H.264 7.4.1 / H.265 7.4.2): remove / insert the
// 0x03 after two zero bytes. Input and output exclude the start code.
QByteArray ttRbspFromNal(const QByteArray& nalBody);
QByteArray ttNalFromRbsp(const QByteArray& rbsp);

#endif // TTBITSTREAM_H
