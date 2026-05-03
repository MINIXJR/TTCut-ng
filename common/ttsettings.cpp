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
  QSettings settings;
  Q_UNUSED(settings);
  // Per-group field loads added in tasks 4-13.
}

void TTSettings::save()
{
  QSettings settings;
  Q_UNUSED(settings);
  // Per-group field saves added in tasks 4-13.
}

void TTSettings::resetToDefaults()
{
  // Per-group field resets added in tasks 4-13. For now this is a no-op.
}
