/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include <cstdio>
#include "data/ttstreampoint.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

int main()
{
  CHECK(TTStreamPoint::typeToString(StreamPointType::AudioAnomaly) == "AudioAnomaly");
  CHECK(TTStreamPoint::stringToType("AudioAnomaly") == StreamPointType::AudioAnomaly);
  CHECK(TTStreamPoint::stringToType("SomethingUnknown") == StreamPointType::ManualMarker);
  TTStreamPoint p(51120, StreamPointType::AudioAnomaly, "Audio anomaly: C+LFE", 0.9f, 1.2f);
  CHECK(p.isAutoDetected());
  fprintf(stderr, "OK\n");
  return 0;
}
