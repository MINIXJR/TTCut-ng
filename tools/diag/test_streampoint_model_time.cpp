// Gate for finding B3 of code-audit run 4 (2026-09-13): the marker list's
// time column subtracts the MPEG-2 field-picture extras before a marker,
// like the navigation display and the cut list do for the same frame.
//
// Marker at index 75 with 25 fps: 3.0 s without extras; with the extras
// {50, 60, 70} before it, 72 shown frames = 2.88 s -> "00:00:02". An extra
// AFTER the marker must not count, and setting the list must announce the
// changed display text (dataChanged with DisplayRole).
//
// Build via `cmake --build build --target test_streampoint_model_time`.
#include <QCoreApplication>
#include <QList>
#include <cstdio>

#include "data/ttstreampoint.h"
#include "data/ttstreampointmodel.h"

namespace {
int failures = 0;
void check(bool ok, const char* what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}
QString timeText(const TTStreamPointModel& m, int row)
{
  return m.data(m.index(row), Qt::DisplayRole).toString().section(QStringLiteral("  "), 0, 0);
}
} // namespace

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);

  TTStreamPointModel model;
  model.setFrameRate(25.0f);
  model.addPoint(TTStreamPoint(75, StreamPointType::ManualMarker, QStringLiteral("marker")));

  check(timeText(model, 0) == QStringLiteral("00:00:03"), "no extras: 75 / 25 fps -> 00:00:03");

  int changed = 0;
  QObject::connect(&model, &QAbstractItemModel::dataChanged,
                   [&](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
                     if (roles.contains(Qt::DisplayRole)) changed++;
                   });
  model.setExtraFrameIndices(QList<int>{50, 60, 70});
  check(timeText(model, 0) == QStringLiteral("00:00:02"), "extras {50,60,70}: (75 - 3) / 25 fps -> 00:00:02");
  check(changed == 1, "setting the extras announces the changed display text");

  model.setExtraFrameIndices(QList<int>{50, 60, 70});
  check(changed == 1, "setting the same list again announces nothing");

  model.setExtraFrameIndices(QList<int>{50, 80});
  check(timeText(model, 0) == QStringLiteral("00:00:02"), "extra after the marker does not count: (75 - 1) / 25 -> 2.96 s");

  model.setExtraFrameIndices(QList<int>());
  check(timeText(model, 0) == QStringLiteral("00:00:03"), "cleared: back to 00:00:03");
  check(model.data(model.index(0), TTStreamPointModel::FrameIndexRole).toInt() == 75, "frame index itself is untouched");

  printf("%s\n", failures ? "STREAMPOINT-MODEL-TIME FAIL" : "STREAMPOINT-MODEL-TIME PASS");
  return failures ? 1 : 0;
}
