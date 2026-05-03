/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2026 / TTCut-ng                                    */
/*----------------------------------------------------------------------------*/

#ifndef TTSETTINGS_H
#define TTSETTINGS_H

#include <QObject>
#include <QString>
#include <QStringList>

class TTSettings : public QObject
{
  Q_OBJECT

public:
  // Singleton access. Lazy-creates a default instance on first call.
  // setInstance() lets tests inject a fixture; pass nullptr to discard
  // and re-create on next instance() call.
  static TTSettings* instance();
  static void setInstance(TTSettings* override);

  TTSettings(QObject* parent = nullptr);
  ~TTSettings() override;

  // Persistence
  void load();              // QSettings -> fields
  void save();              // fields -> QSettings
  void resetToDefaults();   // re-initialise to compile-time defaults

  // Per-group getters/setters added in tasks 4-13.
  // Skeleton commit has no fields yet.

signals:
  // Per-group selective change signals added in tasks 4-13.

private:
  static TTSettings* sInstance;
};

#endif
