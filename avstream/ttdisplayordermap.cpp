#include "ttdisplayordermap.h"

#include "../common/ttmessagelogger.h"

#include <QList>
#include <QPair>

// H.264/HEVC level limits cap the DPB at 16 frames; no conforming stream can
// reorder further than that.
static const int REORDER_DEPTH = 16;

QVector<int> TTDisplayOrderMap::displayRanksFromPoc(const QVector<TTPocEntry>& entries)
{
    const int n = entries.size();
    QVector<int> decodeToDisplay(n, -1);
    QList<QPair<int, int>> dpb;   // (poc, decodeIdx)
    int nextRank = 0;

    auto emitMin = [&]() {
        int best = 0;
        for (int j = 1; j < dpb.size(); ++j)
            if (dpb[j].first < dpb[best].first) best = j;
        decodeToDisplay[dpb[best].second] = nextRank++;
        dpb.removeAt(best);
    };

    for (int i = 0; i < n; ++i) {
        // IDR: nothing after it (decode order) displays before it, and POC
        // restarts — flush the reorder buffer first. Non-IDR I (open GOP/CRA)
        // does NOT flush: its leading B/RASL pictures display before it.
        if (entries[i].isIDR) {
            while (!dpb.isEmpty()) emitMin();
        }
        dpb.append(qMakePair(entries[i].poc, i));
        if (dpb.size() > REORDER_DEPTH) emitMin();
    }
    while (!dpb.isEmpty()) emitMin();

    return decodeToDisplay;
}

void TTDisplayOrderMap::build(const QVector<TTPocEntry>& entries)
{
    buildFromRanks(displayRanksFromPoc(entries));
}

void TTDisplayOrderMap::buildFromRanks(const QVector<int>& decodeToDisplay)
{
    mDecodeToDisplay = decodeToDisplay;
    mDisplayToDecode = QVector<int>(decodeToDisplay.size(), -1);
    for (int i = 0; i < decodeToDisplay.size(); ++i) {
        const int rank = decodeToDisplay[i];
        if (rank < 0 || rank >= mDisplayToDecode.size()) {  // not a permutation
            mDecodeToDisplay.clear();
            mDisplayToDecode.clear();
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("display-order map rejected: rank %1 out of range").arg(rank));
            return;
        }
        mDisplayToDecode[rank] = i;
    }
}

int TTDisplayOrderMap::decodeToDisplay(int decodeIdx) const
{
    if (decodeIdx < 0 || decodeIdx >= mDecodeToDisplay.size()) return decodeIdx;
    return mDecodeToDisplay[decodeIdx];
}

int TTDisplayOrderMap::displayToDecode(int displayPos) const
{
    if (displayPos < 0 || displayPos >= mDisplayToDecode.size()) return displayPos;
    return mDisplayToDecode[displayPos];
}

// Implemented in the next commit (parser-POC collection).
TTDisplayOrderMap TTDisplayOrderMap::buildFromFile(const QString& filePath)
{
    Q_UNUSED(filePath);
    return TTDisplayOrderMap();
}
