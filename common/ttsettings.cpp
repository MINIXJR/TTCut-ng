/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2026 / TTCut-ng                                    */
/*----------------------------------------------------------------------------*/

#include "ttsettings.h"

#include <QSettings>

TTSettings* TTSettings::sInstance = nullptr;

TTSettings* TTSettings::instance()
{
  if (sInstance == nullptr) {
    sInstance = new TTSettings();
    sInstance->load();
  }
  return sInstance;
}

void TTSettings::setInstance(TTSettings* override)
{
  // Caller owns the override pointer's lifetime. We do NOT delete sInstance
  // when overriding because the test fixture pattern frequently passes a
  // stack/RAII-owned pointer.
  sInstance = override;
}

TTSettings::TTSettings(QObject* parent)
  : QObject(parent)
{
}

TTSettings::~TTSettings()
{
}

void TTSettings::load()
{
  // Match TTCutSettings persistence target (QSettings("TTCut-ng", "TTCut-ng"))
  // so both code paths read/write the same on-disk file during the
  // Strangler-pattern migration window.
  QSettings settings("TTCut-ng", "TTCut-ng");
  settings.beginGroup("/Settings");
  // Per-group field loads added in tasks 4-13. Each task enters its own
  // sub-group (e.g. "Common", "Navigation") via beginGroup/endGroup using
  // the EXACT key strings inventoried in
  // docs/superpowers/plans/2026-05-03-task-1-qsettings-inventory.md
  // — that key-compatibility invariant lets the user's existing settings
  // survive the refactor.
  settings.endGroup();
}

void TTSettings::save()
{
  // Match TTCutSettings persistence target — see load().
  QSettings settings("TTCut-ng", "TTCut-ng");
  settings.beginGroup("/Settings");
  // Per-group field saves added in tasks 4-13.
  settings.endGroup();
}

void TTSettings::resetToDefaults()
{
  // Per-group field resets added in tasks 4-13. For now this is a no-op.
}
