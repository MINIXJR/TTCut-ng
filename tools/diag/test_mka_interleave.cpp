// Is an audio-only MKA interleaved, and does its mux report progress?
//
// Code-audit run 7, hypothesis H8 (measured): muxAudioOnly() copied the
// tracks one after the other and left the interleaving to
// av_interleaved_write_frame, which flushes after its 10 s
// max_interleave_delta. Two 60 s tracks came out as track 1 0-50 s, then
// track 2 0-50 s, then 10 s interleaved; a 2 h recording is two blocks.
// Players have to seek back and forth through the file for that. It also
// emitted no progressChanged at all.
//
// Muxes the given audio file twice into one MKA and checks, in file order:
//   - timestamps never step back by more than 1 s between two packets
//     (interleaved; before: a jump back of about the whole track length)
//   - each track's payload equals the source (stream copy, nothing lost)
//   - at least one progressChanged arrived, the last one at >= 99 %
//
//   usage: test_mka_interleave <audio> <workdir>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QVector>
#include <cstdio>

#include "extern/ttmkvmergeprovider.h"

extern "C" {
#include <libavformat/avformat.h>
}

static int fail(const char* what) { fprintf(stderr, "FAIL: %s\n", what); return 1; }

struct Packets {
  bool ok = false;
  QVector<int>     stream;     // stream index per packet, file order
  QVector<double>  seconds;    // pts in seconds per packet, file order
  QVector<QByteArray> md5;     // payload MD5 per stream
};

static Packets readPackets(const QString& file)
{
  Packets p;
  AVFormatContext* ctx = nullptr;
  if (avformat_open_input(&ctx, file.toUtf8().constData(), nullptr, nullptr) < 0) return p;
  if (avformat_find_stream_info(ctx, nullptr) < 0) { avformat_close_input(&ctx); return p; }
  QVector<QCryptographicHash*> hash;
  for (unsigned i = 0; i < ctx->nb_streams; ++i)
    hash.append(new QCryptographicHash(QCryptographicHash::Md5));
  AVPacket* pkt = av_packet_alloc();
  while (av_read_frame(ctx, pkt) >= 0) {
    const AVStream* st = ctx->streams[pkt->stream_index];
    p.stream.append(pkt->stream_index);
    p.seconds.append(pkt->pts == AV_NOPTS_VALUE ? 0.0 : pkt->pts * av_q2d(st->time_base));
    hash[pkt->stream_index]->addData(QByteArrayView(reinterpret_cast<const char*>(pkt->data), pkt->size));
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  for (QCryptographicHash* h : hash) { p.md5.append(h->result().toHex()); delete h; }
  avformat_close_input(&ctx);
  p.ok = true;
  return p;
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc < 3) { fprintf(stderr, "usage: %s <audio> <workdir>\n", argv[0]); return 2; }
  const QString audio = argv[1];
  const QDir    work(argv[2]);
  const QString out   = work.filePath("two_tracks.mka");
  QFile::remove(out);

  TTMkvMergeProvider provider;
  int lastPercent = -1, reports = 0;
  QObject::connect(&provider, &TTMkvMergeProvider::progressChanged,
                   [&](int percent, const QString&) { lastPercent = percent; ++reports; });
  if (!provider.muxAudioOnly(out, {audio, audio}, {"deu", "eng"}))
    return fail(qPrintable("muxAudioOnly failed: " + provider.lastError()));

  const Packets mka = readPackets(out);
  const Packets src = readPackets(audio);
  if (!mka.ok || !src.ok) return fail("cannot read the MKA or the source");
  if (mka.md5.size() != 2) return fail("MKA does not have two streams");

  double worstStepBack = 0.0;
  for (int i = 1; i < mka.seconds.size(); ++i)
    worstStepBack = qMax(worstStepBack, mka.seconds[i - 1] - mka.seconds[i]);
  int switches = 0;
  for (int i = 1; i < mka.stream.size(); ++i)
    if (mka.stream[i] != mka.stream[i - 1]) ++switches;
  printf("%d packets, %d stream switches, worst step back %.3f s\n",
         int(mka.stream.size()), switches, worstStepBack);
  printf("progress: %d report(s), last %d %%\n", reports, lastPercent);

  if (worstStepBack > 1.0) return fail("tracks are not interleaved (timestamps step back by more than 1 s)");
  if (mka.md5[0] != src.md5[0] || mka.md5[1] != src.md5[0])
    return fail("a track's payload differs from the source");
  if (reports == 0) return fail("muxAudioOnly reported no progress");
  if (lastPercent < 99) return fail("last progress report below 99 %");

  printf("PASS: interleaved, both tracks intact, progress reported\n");
  return 0;
}
