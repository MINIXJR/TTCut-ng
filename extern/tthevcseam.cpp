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

// ------------------------------------------------------------------ SPS parse
// Scaling list data walk with flat-16 tracking (H.265 7.3.4).
// A list is "flat 16" when every coefficient (and the DC coef for
// sizeId >= 2) decodes to 16 — numerically identical to scaling disabled.
// pred_matrix_id_delta references copy earlier lists, inheriting flatness;
// delta == 0 references the DEFAULT list, which is NOT flat -> not flat16.
static void parseScalingListData(THevcBitReader& r, bool* allFlat16)
{
    *allFlat16 = true;
    for (int sizeId = 0; sizeId < 4; ++sizeId) {
        for (int matrixId = 0; matrixId < 6;
             matrixId += (sizeId == 3) ? 3 : 1) {
            int predMode = r.bit();
            if (!predMode) {
                quint32 delta = r.ue();
                if (delta == 0)          // copies DEFAULT list (non-flat)
                    *allFlat16 = false;
                // delta > 0 copies an earlier parsed list: flatness inherited,
                // tracked implicitly via *allFlat16 over all explicit lists.
            } else {
                int coefNum = qMin(64, 1 << (4 + (sizeId << 1)));
                int nextCoef = 8;
                if (sizeId > 1) {
                    qint32 dcMinus8 = r.se();
                    if (dcMinus8 != 8) *allFlat16 = false;   // DC must be 16
                    nextCoef = dcMinus8 + 8;
                }
                for (int i = 0; i < coefNum; ++i) {
                    qint32 d = r.se();
                    nextCoef = (nextCoef + d + 256) % 256;
                    if (nextCoef != 16) *allFlat16 = false;
                }
            }
        }
    }
}

THevcSpsSeamInfo parseHevcSpsSeamInfo(const QByteArray& spsNal)
{
    THevcSpsSeamInfo info;
    int sc = ttHevcStartCodeLen(spsNal);
    QByteArray rbsp = ttHevcDeescape(spsNal.mid(sc));
    THevcBitReader r(rbsp);

    quint32 hdr = r.bits(16);
    if (((hdr >> 9) & 0x3F) != 33) {
        info.invalidReason = QStringLiteral("not an SPS NAL");
        return info;
    }
    r.bits(4);                                   // sps_video_parameter_set_id
    info.maxSubLayersMinus1 = int(r.bits(3));
    r.bit();                                     // temporal_id_nesting
    if (info.maxSubLayersMinus1 != 0) {
        info.invalidReason = QStringLiteral("sub-layers unsupported");
        return info;
    }
    // profile_tier_level(1, 0): 2+1+5+32+4x1+43+1 = 88 bits general + 8 level
    r.skip(88 + 8);

    info.spsId = int(r.ue());
    info.chromaFormatIdc = int(r.ue());
    if (info.chromaFormatIdc == 3) r.bit();      // separate_colour_plane
    info.picWidth  = int(r.ue());
    info.picHeight = int(r.ue());
    if (r.bit()) {                               // conformance_window
        r.ue(); r.ue(); r.ue(); r.ue();
    }
    info.bitDepthLuma   = int(r.ue()) + 8;
    info.bitDepthChroma = int(r.ue()) + 8;
    info.log2MaxPocLsb  = int(r.ue()) + 4;
    int subLayerOrdering = r.bit();
    // maxSubLayersMinus1 == 0: exactly one dpb/reorder/latency triple either way
    Q_UNUSED(subLayerOrdering);
    info.maxDecPicBufferingMinus1 = int(r.ue());
    r.ue();                                      // max_num_reorder_pics
    r.ue();                                      // max_latency_increase_plus1
    info.log2MinCbSizeMinus3  = int(r.ue());
    info.log2DiffMaxMinCbSize = int(r.ue());
    info.log2MinTbSizeMinus2  = int(r.ue());
    info.log2DiffMaxMinTbSize = int(r.ue());
    info.tuDepthInter = int(r.ue());
    info.tuDepthIntra = int(r.ue());
    info.scalingListEnabled = r.bit();
    if (info.scalingListEnabled) {
        info.scalingListDataPresent = r.bit();
        if (info.scalingListDataPresent) {
            bool flat = true;
            parseScalingListData(r, &flat);
            info.scalingListFlat16 = flat;
        } else {
            // Default lists active — NOT flat.
            info.scalingListFlat16 = false;
        }
    }
    info.ampEnabled = r.bit();
    info.saoEnabled = r.bit();
    info.pcmEnabled = r.bit();
    if (info.pcmEnabled) {
        info.invalidReason = QStringLiteral("PCM unsupported");
        return info;
    }
    info.numShortTermRefPicSets = int(r.ue());
    if (info.numShortTermRefPicSets != 0) {
        // st_ref_pic_set parsing in the SPS is not implemented; preflight
        // requires 0 anyway (all measured corpora).
        info.invalidReason = QStringLiteral("SPS RPS sets unsupported");
        return info;
    }
    info.longTermRefPicsPresent = r.bit();
    if (info.longTermRefPicsPresent) {
        info.invalidReason = QStringLiteral("long-term ref pics unsupported");
        return info;
    }
    info.temporalMvpEnabled = r.bit();
    info.strongIntraSmoothing = r.bit();
    // VUI and extensions are irrelevant for the seam — stop here.

    if (r.error()) {
        info.invalidReason = QStringLiteral("bitstream overrun");
        return info;
    }
    info.valid = true;
    return info;
}
