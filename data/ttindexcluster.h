/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTINDEXCLUSTER_H
#define TTINDEXCLUSTER_H

#include <QList>

// A run of frame indices whose neighbours lie at most `gap` apart. first and
// last are index VALUES, count the number of members (a run may have holes
// up to the gap). Used for the defect and audio-gap markers (TTAVData) and
// the LFE islands of the audio anomaly scan.
struct TTIndexCluster
{
  int first;
  int last;
  int count;
};

inline QList<TTIndexCluster> ttClusterIndices(const QList<int>& ascending, int gap)
{
  QList<TTIndexCluster> clusters;
  if (ascending.isEmpty()) return clusters;

  TTIndexCluster c{ ascending.first(), ascending.first(), 1 };
  for (int i = 1; i < ascending.size(); ++i) {
    if (ascending[i] - c.last <= gap) {
      c.last = ascending[i];
      ++c.count;
    } else {
      clusters.append(c);
      c = { ascending[i], ascending[i], 1 };
    }
  }
  clusters.append(c);
  return clusters;
}

#endif // TTINDEXCLUSTER_H
