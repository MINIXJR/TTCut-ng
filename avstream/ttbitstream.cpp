/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttbitstream.h"

TTBitReader::TTBitReader(const uint8_t* data, int size)
    : mData(data), mSizeBits(size > 0 ? size * 8 : 0)
{
}

TTBitReader::TTBitReader(const QByteArray& rbsp)
    : TTBitReader(reinterpret_cast<const uint8_t*>(rbsp.constData()), rbsp.size())
{
}

uint32_t TTBitReader::bits(int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) {
        v <<= 1;
        if (mPos >= mSizeBits) { mError = true; continue; }
        v |= (mData[mPos >> 3] >> (7 - (mPos & 7))) & 1u;
        ++mPos;
    }
    return v;
}

uint32_t TTBitReader::ue()
{
    if (mError) return 0;
    int zeros = 0;
    for (;;) {
        if (mPos >= mSizeBits) { mError = true; return 0; }
        if (bits(1)) break;
        if (++zeros > 31) { mError = true; return 0; }
    }
    if (zeros == 0) return 0;
    const uint32_t suffix = bits(zeros);
    if (mError) return 0;
    return ((1u << zeros) - 1u) + suffix;
}

int32_t TTBitReader::se()
{
    const uint32_t k = ue();
    return (k & 1u) ? static_cast<int32_t>((k + 1u) / 2u)
                    : -static_cast<int32_t>(k / 2u);
}

void TTBitReader::skip(int n)
{
    mPos += n;
    if (mPos > mSizeBits) { mError = true; mPos = mSizeBits; }
}

void TTBitWriter::bits(uint32_t v, int n)
{
    for (int i = n - 1; i >= 0; --i) {
        if ((mNumBits & 7) == 0) mBytes.append('\0');
        if ((v >> i) & 1u) {
            const int last = mBytes.size() - 1;
            mBytes[last] = char(uint8_t(mBytes.at(last)) | (0x80u >> (mNumBits & 7)));
        }
        ++mNumBits;
    }
}

void TTBitWriter::ue(uint32_t v)
{
    const uint64_t codeNum = uint64_t(v) + 1u;
    int len = 0;
    for (uint64_t t = codeNum; t; t >>= 1) ++len;      // 1..33
    bits(0, len - 1);
    if (len > 32) {
        bits(uint32_t(codeNum >> 32), len - 32);
        bits(uint32_t(codeNum), 32);
    } else {
        bits(uint32_t(codeNum), len);
    }
}

void TTBitWriter::se(int32_t v)
{
    const int64_t k = (v > 0) ? 2 * int64_t(v) - 1 : -2 * int64_t(v);
    ue(uint32_t(k));
}

void TTBitWriter::copyBits(TTBitReader& r, int n)
{
    while (n > 0) {
        const int c = n > 32 ? 32 : n;
        bits(r.bits(c), c);
        n -= c;
    }
}

void TTBitWriter::alignWith(int bit)
{
    while (mNumBits & 7) bits(bit ? 1u : 0u, 1);
}

void TTBitWriter::appendBytes(const uint8_t* p, int n)
{
    if (n <= 0) return;
    if (byteAligned()) {
        mBytes.append(reinterpret_cast<const char*>(p), n);
        mNumBits += 8 * n;
        return;
    }
    for (int i = 0; i < n; ++i) bits(p[i], 8);
}

void TTBitWriter::rbspTrailingBits()
{
    bits(1, 1);
    alignWith(0);
}

void ttOverwriteBits(uint8_t* data, int size, int bitPos, uint32_t v, int n)
{
    for (int i = n - 1; i >= 0; --i) {
        const int byteIndex = bitPos / 8;
        if (byteIndex >= size) return;
        const int bitIndex = 7 - (bitPos % 8);
        if (v & (1u << i)) data[byteIndex] |= uint8_t(1u << bitIndex);
        else               data[byteIndex] &= uint8_t(~(1u << bitIndex));
        ++bitPos;
    }
}

QByteArray ttRbspFromNal(const QByteArray& nalBody)
{
    QByteArray out;
    out.reserve(nalBody.size());
    int zeros = 0;
    for (int i = 0; i < nalBody.size(); ++i) {
        const uint8_t b = uint8_t(nalBody.at(i));
        if (zeros >= 2 && b == 0x03) { zeros = 0; continue; }
        out.append(char(b));
        zeros = (b == 0) ? zeros + 1 : 0;
    }
    return out;
}

QByteArray ttNalFromRbsp(const QByteArray& rbsp)
{
    QByteArray out;
    out.reserve(rbsp.size() + rbsp.size() / 128 + 8);
    int zeros = 0;
    for (int i = 0; i < rbsp.size(); ++i) {
        const uint8_t b = uint8_t(rbsp.at(i));
        if (zeros >= 2 && b <= 0x03) { out.append(char(0x03)); zeros = 0; }
        out.append(char(b));
        zeros = (b == 0) ? zeros + 1 : 0;
    }
    return out;
}
