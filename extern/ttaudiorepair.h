#ifndef TTAUDIOREPAIR_H
#define TTAUDIOREPAIR_H
#include <QMap>
#include <QByteArray>
#include <QString>
#include "ttaudiorepairitem.h"

struct AVFormatContext;

namespace TTAudioRepair {

// Replacement table: AC3 source frame number -> ready-to-write frame bytes.
using FrameTable = QMap<qint64, QByteArray>;

// Build replacement frames for one repair item. Decodes the item's source
// frames, silences the masked channels (5 ms raised-cosine fades at range
// start/end), re-encodes with the source's sample rate/bit rate and the
// given target acmod (-1 = keep the source channel layout). On failure
// returns an empty table and sets errorOut — callers MUST treat that as
// abort-the-cut, never as skip-the-repair (spec: Fehlerbild Punkt 2).
// Open audioFile with libavformat and find its first audio stream. On
// success *fmtCtx is open (caller closes it) and audioIdx set; on failure
// *fmtCtx is null and *error says why. Shared by buildRepairTable and the
// repair dialog's preview writer.
bool openFirstAudioStream(const QString& audioFile, AVFormatContext** fmtCtx, int* audioIdx, QString* error);

FrameTable buildRepairTable(const QString& audioFile,
                            const TTAudioRepairItem& item,
                            int targetAcmod,
                            QString* errorOut);
}
#endif
