/*----------------------------------------------------------------------------*/
/* HEVC seam machinery — see tthevcseam.h. Port of the validated PoC          */
/* (CLAUDE_TMP/TTCut-ng/hevc_rasl/poc/hevc_seam_poc.py, 2026-07-21).          */
/*----------------------------------------------------------------------------*/

#include "tthevcseam.h"

#include <QtGlobal>

// ---------------------------------------------------------------- bit reader
namespace {

class THevcBitReader
{
public:
    explicit THevcBitReader(const QByteArray& data)
        : mData(reinterpret_cast<const quint8*>(data.constData()))
        , mSizeBits(data.size() * 8), mPos(0), mError(false) {}

    bool error() const { return mError; }
    int  pos() const { return mPos; }
    int  sizeBits() const { return mSizeBits; }

    int bit()
    {
        if (mPos >= mSizeBits) { mError = true; return 0; }
        int b = (mData[mPos >> 3] >> (7 - (mPos & 7))) & 1;
        ++mPos;
        return b;
    }
    quint32 bits(int n)
    {
        quint32 v = 0;
        for (int i = 0; i < n; ++i) v = (v << 1) | bit();
        return v;
    }
    quint32 ue()
    {
        int zeros = 0;
        while (!mError && bit() == 0) {
            if (++zeros > 31) { mError = true; return 0; }
        }
        quint32 suffix = (zeros > 0) ? bits(zeros) : 0;
        return ((1u << zeros) - 1) + suffix;
    }
    qint32 se()
    {
        quint32 k = ue();
        return (k & 1) ? static_cast<qint32>((k + 1) / 2)
                       : -static_cast<qint32>(k / 2);
    }
    void skip(int n) { mPos += n; if (mPos > mSizeBits) mError = true; }

private:
    const quint8* mData;
    int  mSizeBits;
    int  mPos;
    bool mError;
};

class THevcBitWriter
{
public:
    void bit(int b)
    {
        if ((mNumBits & 7) == 0) mBytes.append(char(0));
        if (b) mBytes[mBytes.size() - 1] =
            char(quint8(mBytes.at(mBytes.size() - 1)) | (1 << (7 - (mNumBits & 7))));
        ++mNumBits;
    }
    void bits(quint32 v, int n)
    {
        for (int i = n - 1; i >= 0; --i) bit((v >> i) & 1);
    }
    void ue(quint32 v)
    {
        quint32 vp1 = v + 1;
        int nb = 0;
        for (quint32 t = vp1; t; t >>= 1) ++nb;
        bits(0, nb - 1);
        bits(vp1, nb);
    }
    void se(qint32 v)
    {
        ue(v > 0 ? quint32(2 * v - 1) : quint32(-2 * v));
    }
    void alignOneZeros()          // rbsp stop bit + zero padding
    {
        bit(1);
        while (mNumBits & 7) bit(0);
    }
    int numBits() const { return mNumBits; }
    QByteArray data() const { return mBytes; }

private:
    QByteArray mBytes;
    int mNumBits = 0;
};

} // namespace

// -------------------------------------------------------------- EPB handling
QByteArray ttHevcDeescape(const QByteArray& nalData)
{
    QByteArray out;
    out.reserve(nalData.size());
    int zeros = 0;
    for (int i = 0; i < nalData.size(); ++i) {
        quint8 b = quint8(nalData.at(i));
        if (zeros >= 2 && b == 3) { zeros = 0; continue; }
        out.append(char(b));
        zeros = (b == 0) ? zeros + 1 : 0;
    }
    return out;
}

QByteArray ttHevcEscape(const QByteArray& rbsp)
{
    QByteArray out;
    out.reserve(rbsp.size() + 8);
    int zeros = 0;
    for (int i = 0; i < rbsp.size(); ++i) {
        quint8 b = quint8(rbsp.at(i));
        if (zeros >= 2 && b <= 3) { out.append(char(3)); zeros = 0; }
        out.append(char(b));
        zeros = (b == 0) ? zeros + 1 : 0;
    }
    return out;
}

int ttHevcStartCodeLen(const QByteArray& nal)
{
    if (nal.size() >= 4 && nal.at(0) == 0 && nal.at(1) == 0
        && nal.at(2) == 0 && nal.at(3) == 1) return 4;
    if (nal.size() >= 3 && nal.at(0) == 0 && nal.at(1) == 0
        && nal.at(2) == 1) return 3;
    return 0;
}

// Later tasks append the parsers/rewriter below this line.
