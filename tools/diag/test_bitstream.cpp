// Unit gate for avstream/ttbitstream (spec
// docs/superpowers/specs/2026-09-23-bitstream-unification-design.md):
// Exp-Golomb round trips at the limits, reader behaviour past the end,
// writer helpers, and a differential test of the new emulation-prevention
// pair against frozen copies of the two pairs it replaced
// (TTNaluParser::removeEmulationPrevention + addEmulationPrevention,
// ttHevcDeescape + ttHevcEscape). A differential mismatch is a decision for
// the user (spec, "EPB pair") - the harness prints the first differing input.
//   test_bitstream            exit 0 = PASS, 1 = FAIL
#include <cstdio>
#include <cstdint>
#include <random>
#include <QByteArray>
#include <QString>

#include "avstream/ttannexb.h"
#include "avstream/ttbitstream.h"

// ------------------------------------------------ frozen legacy reference
// Frozen copies of the implementations removed in the bit-stream
// unification (TTNaluParser::readBits / readExpGolombUE / readExpGolombSE /
// removeEmulationPrevention, the smart cut's addEmulationPrevention, the
// HEVC seam's ttHevcDeescape / ttHevcEscape), kept verbatim as the
// reference the new module was proven against.
static uint32_t legacyReadBits(const uint8_t* data, int dataSize, int& bitPos, int numBits)
{
    uint32_t value = 0;

    for (int i = 0; i < numBits; i++) {
        int byteIndex = bitPos / 8;
        if (byteIndex >= dataSize) return value;  // OOB guard
        int bitIndex = 7 - (bitPos % 8);

        value <<= 1;
        value |= (data[byteIndex] >> bitIndex) & 1;
        bitPos++;
    }

    return value;
}

static uint32_t legacyReadUE(const uint8_t* data, int dataSize, int& bitPos)
{
    // Count leading zeros
    int leadingZeros = 0;
    while (legacyReadBits(data, dataSize, bitPos, 1) == 0 && leadingZeros < 31) {
        leadingZeros++;
    }

    if (leadingZeros == 0) {
        return 0;
    }

    // Read the value bits
    uint32_t value = legacyReadBits(data, dataSize, bitPos, leadingZeros);
    return (1u << leadingZeros) - 1 + value;
}

static int32_t legacyReadSE(const uint8_t* data, int dataSize, int& bitPos)
{
    uint32_t ue = legacyReadUE(data, dataSize, bitPos);
    if (ue & 1) {
        return static_cast<int32_t>((ue + 1) / 2);
    } else {
        return -static_cast<int32_t>(ue / 2);
    }
}

static QByteArray legacyRemoveEp(const QByteArray& nal)
{
    QByteArray rbsp;
    rbsp.reserve(nal.size());
    for (int i = 0; i < nal.size(); ++i) {
        if (i + 2 < nal.size() &&
            (uint8_t)nal[i] == 0x00 &&
            (uint8_t)nal[i+1] == 0x00 &&
            (uint8_t)nal[i+2] == 0x03) {
            rbsp.append(nal[i]);
            rbsp.append(nal[i+1]);
            i += 2;  // skip the 0x03 escape byte
        } else {
            rbsp.append(nal[i]);
        }
    }
    return rbsp;
}

static QByteArray legacyAddEp(const QByteArray& rbsp)
{
    QByteArray nal;
    nal.reserve(rbsp.size() + rbsp.size() / 128);
    for (int i = 0; i < rbsp.size(); ++i) {
        if (i + 2 < rbsp.size() &&
            (uint8_t)rbsp[i] == 0x00 && (uint8_t)rbsp[i+1] == 0x00 &&
            ((uint8_t)rbsp[i+2] <= 0x03)) {
            nal.append(rbsp[i]);      // first 00
            nal.append(rbsp[i+1]);    // second 00
            nal.append((char)0x03);   // emulation prevention byte
            i += 1;  // skip one; for-loop's i++ skips another → consumed both 00s
            // Next iteration writes rbsp[i+2] (the original third byte)
        } else {
            nal.append(rbsp[i]);
        }
    }
    return nal;
}

static QByteArray legacyHevcDeescape(const QByteArray& nalData)
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

static QByteArray legacyHevcEscape(const QByteArray& rbsp)
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

static int gFailures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) { ++gFailures; printf("FAIL: %s\n", what); }
}

static QString hex(const QByteArray& b) { return QString::fromLatin1(b.toHex(' ')); }

static void testExpGolombRoundTrip()
{
    const uint32_t ues[] = { 0u, 1u, 2u, 3u, 7u, 8u, 255u, 256u, 65535u,
                             (1u << 31) - 2u, (1u << 31) - 1u, 1u << 31, 0xFFFFFFFEu };
    const int32_t ses[] = { 0, 1, -1, 2, -2, 127, -128, 2147483647, -2147483647 };
    TTBitWriter w;
    for (uint32_t v : ues) w.ue(v);
    for (int32_t v : ses) w.se(v);
    w.bits(0x5, 3);
    const int written = w.numBits();
    const QByteArray d = w.data();
    check(d.size() == (written + 7) / 8, "writer byte count");

    TTBitReader r(d);
    for (uint32_t v : ues) check(r.ue() == v, "ue round trip");
    for (int32_t v : ses) check(r.se() == v, "se round trip");
    check(r.bits(3) == 0x5, "bits after exp-golomb");
    check(!r.error(), "no error on valid data");
    check(r.pos() == written, "reader position == writer bits");

    // The legacy reader must decode what the new writer wrote.
    int pos = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(d.constData());
    for (uint32_t v : ues) check(legacyReadUE(p, d.size(), pos) == v, "legacy ue reads new writer");
    for (int32_t v : ses) check(legacyReadSE(p, d.size(), pos) == v, "legacy se reads new writer");
}

// On valid data (no read past the end) the new reader must return exactly
// what the legacy readers return, op for op.
static void testReaderMatchesLegacyOnValidData()
{
    std::mt19937 rng(20260923);
    for (int round = 0; round < 2000; ++round) {
        QByteArray d(64, '\0');
        for (char& c : d) c = char(rng() & 0xFF);
        // Make long zero runs common so large Exp-Golomb codes occur.
        for (int k = 0; k < 8; ++k) d[int(rng() % 64)] = 0;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(d.constData());
        TTBitReader r(d);
        int pos = 0;
        for (int op = 0; op < 40; ++op) {
            const int kind = int(rng() % 3);
            TTBitReader probe = r;               // look ahead: stop before any overrun
            if (kind == 0) { const int n = 1 + int(rng() % 32); probe.bits(n);
                             if (probe.error()) break;
                             check(r.bits(n) == legacyReadBits(p, d.size(), pos, n), "bits == legacy"); }
            else if (kind == 1) { probe.ue(); if (probe.error()) break;
                             check(r.ue() == legacyReadUE(p, d.size(), pos), "ue == legacy"); }
            else { probe.se(); if (probe.error()) break;
                             check(r.se() == legacyReadSE(p, d.size(), pos), "se == legacy"); }
            check(r.pos() == pos, "position == legacy");
        }
    }
}

static void testReaderPastTheEnd()
{
    { const QByteArray d("\xFF", 1); TTBitReader r(d);
      check(r.bits(4) == 0xF, "4 of 8 bits");
      check(r.bits(8) == 0xF0, "straddling read: available bits then zeros");
      check(r.error(), "straddling read sets error");
      check(r.pos() == 8, "position saturates at the end"); }
    { const QByteArray d("\x00", 1); TTBitReader r(d);
      check(r.ue() == 0 && r.error(), "ue over an all-zero buffer: 0 + error"); }
    { const QByteArray d("\x01", 1); TTBitReader r(d);
      check(r.ue() == 0 && r.error(), "ue whose suffix runs past the end: 0 + error"); }
    { const QByteArray d("\x00\x00\x00\x00\x80", 5); TTBitReader r(d);
      check(r.ue() == 0 && r.error(), "32 leading zeros: 0 + error"); }
    { const QByteArray d("\x80", 1); TTBitReader r(d);
      r.bits(16); r.setPos(0);
      check(r.ue() == 0 && r.se() == 0, "error is sticky: ue/se return 0 after it"); }
    { const QByteArray d("\xFF", 1); TTBitReader r(d);
      r.skip(20);
      check(r.error() && r.pos() == 8, "skip past the end: error, position clamped"); }
}

static void testWriterHelpers()
{
    { TTBitWriter w; w.bits(0x5, 3); w.alignWith(1);
      check(w.data() == QByteArray("\xBF", 1), "alignWith(1) pads with ones");
      w.alignWith(1); check(w.numBits() == 8, "alignWith on a boundary adds nothing"); }
    { TTBitWriter w; w.bits(0xAB, 8); w.rbspTrailingBits();
      check(w.data() == QByteArray("\xAB\x80", 2), "rbspTrailingBits on a boundary"); }
    { TTBitWriter w; w.bits(0x1, 1); w.rbspTrailingBits();
      check(w.data() == QByteArray("\xC0", 1), "rbspTrailingBits mid-byte"); }
    { TTBitWriter w; w.bits(0xCD, 8); const uint8_t b[] = { 0x12, 0x34 }; w.appendBytes(b, 2);
      check(w.data() == QByteArray("\xCD\x12\x34", 3), "appendBytes aligned"); }
    { TTBitWriter w; w.bits(0x1, 4); const uint8_t b[] = { 0xFF }; w.appendBytes(b, 1);
      check(w.data() == QByteArray("\x1F\xF0", 2) && w.numBits() == 12, "appendBytes unaligned"); }
    { const QByteArray src("\xDE\xAD\xBE\xEF", 4);
      TTBitReader r(src); r.skip(3);
      TTBitWriter w; w.bits(1, 1); w.copyBits(r, 20);
      TTBitReader a(src); a.skip(3);
      TTBitWriter e; e.bits(1, 1); for (int i = 0; i < 20; ++i) e.bits(a.bits(1), 1);
      check(w.data() == e.data() && w.numBits() == 21, "copyBits == bit-by-bit copy"); }
    { uint8_t buf[3] = { 0xFF, 0xFF, 0xFF };
      ttOverwriteBits(buf, 3, 5, 0x5, 4);
      check(buf[0] == 0xFA && buf[1] == 0xFF && buf[2] == 0xFF, "overwrite across a byte boundary");
      ttOverwriteBits(buf, 3, 22, 0x0, 4);
      check(buf[2] == 0xFC, "overwrite stops at the end of the buffer"); }
}

static bool epbConsistent(const QByteArray& x)
{
    const QByteArray a = ttRbspFromNal(x);
    const QByteArray b = legacyRemoveEp(x);
    const QByteArray c = legacyHevcDeescape(x);
    const QByteArray e1 = ttNalFromRbsp(x);
    const QByteArray e2 = legacyHevcEscape(x);
    const QByteArray e3 = legacyAddEp(x);
    const bool ok = (a == b) && (a == c) && (e1 == e2) && (e1 == e3) && (ttRbspFromNal(e1) == x);
    if (!ok) {
        printf("EPB DIFF input=[%s]\n  ttRbspFromNal=[%s]\n  removeEmulationPrevention=[%s]\n"
               "  ttHevcDeescape=[%s]\n  ttNalFromRbsp=[%s]\n  ttHevcEscape=[%s]\n"
               "  addEmulationPrevention=[%s]\n",
               qPrintable(hex(x)), qPrintable(hex(a)), qPrintable(hex(b)), qPrintable(hex(c)),
               qPrintable(hex(e1)), qPrintable(hex(e2)), qPrintable(hex(e3)));
    }
    return ok;
}

static void testEpbDifferential()
{
    const char alphabet[] = { 0x00, 0x01, 0x02, 0x03, char(0xFF) };
    bool ok = true;
    for (int len = 0; len <= 4 && ok; ++len) {
        int total = 1; for (int i = 0; i < len; ++i) total *= 5;
        for (int idx = 0; idx < total && ok; ++idx) {
            QByteArray x; int v = idx;
            for (int i = 0; i < len; ++i) { x.append(alphabet[v % 5]); v /= 5; }
            ok = epbConsistent(x);
        }
    }
    std::mt19937 rng(4711);
    for (int round = 0; round < 100000 && ok; ++round) {
        QByteArray x(1 + int(rng() % 40), '\0');
        for (char& c : x) {
            const uint32_t k = rng() % 100;
            c = (k < 70) ? char(0) : (k < 80) ? char(1) : (k < 90) ? char(2) : (k < 95) ? char(3) : char(rng() & 0xFF);
        }
        ok = epbConsistent(x);
    }
    check(ok, "EPB implementations agree (differential)");
}

// avstream/ttannexb: start codes and NAL ends at the edges.
static void testAnnexB()
{
    auto u = [](const char* s) { return reinterpret_cast<const uint8_t*>(s); };
    check(ttStartCodeLength(QByteArray("\x00\x00\x00\x01\x67", 5)) == 4, "start code length 4");
    check(ttStartCodeLength(QByteArray("\x00\x00\x01\x67", 4)) == 3, "start code length 3");
    check(ttStartCodeLength(QByteArray("\x00\x01\x67", 3)) == 0, "no start code");
    int sc = 0;
    check(ttNextStartCode(u("\xAA\x00\x00\x01\x65"), 5, 0, &sc) == 1 && sc == 3,
          "3-byte start code after a leading byte");
    check(ttNextStartCode(u("\x00\x00\x00\x01\x67"), 5, 0, &sc) == 0 && sc == 4,
          "4-byte start code");
    check(ttNextStartCode(u("\x11\x00\x00\x01"), 4, 0, &sc) == -1,
          "start code without a NAL header byte is not reported");
    check(ttNalEnd(u("\x65\x88\x00\x00\x00\x01\x41"), 7, 1) == 2,
          "zero byte before a 4-byte start code ends the NAL");
    check(ttNalEnd(u("\x65\x00\x00\x03\x01\x02"), 6, 1) == 6,
          "escaped 00 00 03 is not a NAL end");
}

int main()
{
    testAnnexB();
    testExpGolombRoundTrip();
    testReaderMatchesLegacyOnValidData();
    testReaderPastTheEnd();
    testWriterHelpers();
    testEpbDifferential();
    printf("%s\n", gFailures ? "BITSTREAM FAIL" : "BITSTREAM PASS");
    return gFailures ? 1 : 0;
}
