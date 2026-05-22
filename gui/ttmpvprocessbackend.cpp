/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttmpvprocessbackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include "../common/ttsettings.h"
#include "../common/ttmessagelogger.h"

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

TTMpvProcessBackend::TTMpvProcessBackend(QObject* parent)
  : ITTMpvBackend(parent)
{
}

TTMpvProcessBackend::~TTMpvProcessBackend()
{
  shutdown();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

//! Prepare the backend (socket path, stale file cleanup).
//! mpv itself is not launched here — that happens on the first loadfile command.
//! Idempotent: tears down any existing process/socket before creating fresh ones.
bool TTMpvProcessBackend::start()
{
  // Tear down any pre-existing state so start() is safe to call repeatedly
  shutdown();

  // Per-process unique socket path to avoid collisions between instances
  mSocketPath = QDir(TTSettings::instance()->tempDirPath()).filePath(
      QString("mpv-ipc-%1.sock").arg(QCoreApplication::applicationPid()));
  QFile::remove(mSocketPath);   // remove stale socket from a previous run

  mProcess = new QProcess(this);
  connect(mProcess,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this,
          &TTMpvProcessBackend::onProcessFinished);
  connect(mProcess, &QProcess::errorOccurred,
          this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart)
      emit mpvError(QStringLiteral("mpv not found or not executable — is mpv installed?"));
  });
  connect(mProcess, &QProcess::started,
          this, &TTMpvProcessBackend::connectIpcSocket);

  mPlaybackEndedEmitted = false;
  return true;
}

//! Shut down mpv and the IPC socket cleanly.
void TTMpvProcessBackend::shutdown()
{
  if (mSocket) {
    mSocket->disconnect(this);
    mSocket->close();
    delete mSocket;
    mSocket = nullptr;
  }

  if (mProcess) {
    if (mProcess->state() == QProcess::Running) {
      mProcess->terminate();
      if (!mProcess->waitForFinished(2000))
        mProcess->kill();
      mProcess->waitForFinished(1000);
    }
    delete mProcess;
    mProcess = nullptr;
  }

  if (!mSocketPath.isEmpty())
    QFile::remove(mSocketPath);

  mRxBuffer.clear();
  mPlaybackEndedEmitted = false;
}

// ---------------------------------------------------------------------------
// Render target
// ---------------------------------------------------------------------------

void TTMpvProcessBackend::attachToWidget(QWidget* target)
{
  mTarget = target;
}

void TTMpvProcessBackend::detach()
{
  mTarget = nullptr;
}

// ---------------------------------------------------------------------------
// mpv command API
// ---------------------------------------------------------------------------

//! Route a generic mpv command.
//! Special case: "loadfile" launches the mpv process with the full CLI args.
void TTMpvProcessBackend::command(const QStringList& args)
{
  if (args.isEmpty())
    return;

  if (args[0] == QLatin1String("loadfile") && args.size() >= 2) {
    if (!mProcess) {
      TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
          "TTMpvProcessBackend::command: backend not started");
      return;
    }

    // Build the fixed mpv CLI arguments
    QStringList mpvArgs;
    mpvArgs << "--no-osc"
            << "--no-input-default-bindings"
            << "--keep-open=no"
            << "--hr-seek=yes"
            << "--hr-seek-framedrop=no"
            << QString("--input-ipc-server=%1").arg(mSocketPath);

    // Embed into a widget when one is attached
    if (mTarget) {
      mpvArgs << "--vo=x11,xv"
              << QString("--wid=%1").arg(mTarget->winId());
    }

    // Any additional args passed by the caller (e.g. --start=, --audio-file=)
    for (int i = 2; i < args.size(); ++i)
      mpvArgs << args[i];

    // The file to play
    mpvArgs << args[1];

    if (TTSettings::instance()->logUI())
      qDebug() << "TTMpvProcessBackend: starting mpv:" << mpvArgs;

    mPlaybackEndedEmitted = false;
    mProcess->start("mpv", mpvArgs);
    // QProcess::started → connectIpcSocket is wired in start(), not here

  } else {
    // All other commands go through the IPC socket
    if (!mSocket) {
      TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
          "TTMpvProcessBackend::command: socket not available, dropping command");
      return;
    }
    QVariantList elements;
    for (const QString& a : args)
      elements << a;
    sendCommand(elements);
  }
}

//! Send a set_property command via IPC.
void TTMpvProcessBackend::setProperty(const QString& name, const QVariant& value)
{
  sendCommand(QVariantList{ QStringLiteral("set_property"), name, value });
}

//! Send an observe_property command via IPC.
void TTMpvProcessBackend::observeProperty(const QString& name)
{
  sendCommand(QVariantList{ QStringLiteral("observe_property"), mNextObserveId++, name });
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

//! Connect the QLocalSocket to mpv's IPC socket.
//! mpv creates the socket shortly after startup, so we retry briefly.
void TTMpvProcessBackend::connectIpcSocket()
{
  // Tear down any pre-existing socket before creating a fresh one
  if (mSocket) {
    mSocket->disconnect(this);
    mSocket->close();
    delete mSocket;
    mSocket = nullptr;
  }

  mSocket = new QLocalSocket(this);
  connect(mSocket, &QLocalSocket::readyRead,
          this, &TTMpvProcessBackend::onSocketReadyRead);

  // mpv needs ~100 ms to create the socket file. connectToServer() fails
  // instantly while the file is still absent (ENOENT), and waitForConnected()
  // does NOT block in that case — so a real sleep between retries is required,
  // otherwise the loop races through in microseconds and always fails.
  const int maxRetries = 40;          // 40 × 50 ms = 2 s budget
  for (int i = 0; i < maxRetries; ++i) {
    mSocket->connectToServer(mSocketPath);
    if (mSocket->waitForConnected(50)) {
      emit connected();
      return;   // success
    }
    mSocket->abort();
    QThread::msleep(50);
  }

  TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
      QString("TTMpvProcessBackend: could not connect to mpv IPC socket at %1").arg(mSocketPath));
}

//! Serialise a JSON command and write it to the IPC socket.
void TTMpvProcessBackend::sendCommand(const QVariantList& elements)
{
  if (!mSocket || mSocket->state() != QLocalSocket::ConnectedState) {
    if (TTSettings::instance()->logUI())
      qDebug() << "TTMpvProcessBackend::sendCommand: socket not connected, dropping command";
    return;
  }

  QJsonObject obj;
  obj[QStringLiteral("command")] = QJsonArray::fromVariantList(elements);
  QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
  data.append('\n');
  mSocket->write(data);
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

//! Accumulate data from the IPC socket and dispatch complete JSON lines.
void TTMpvProcessBackend::onSocketReadyRead()
{
  mRxBuffer.append(mSocket->readAll());

  while (true) {
    int nl = mRxBuffer.indexOf('\n');
    if (nl < 0)
      break;

    QByteArray line = mRxBuffer.left(nl).trimmed();
    mRxBuffer.remove(0, nl + 1);

    if (line.isEmpty())
      continue;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError) {
      emit mpvError(QString("IPC JSON parse error: %1 (raw: %2)")
                    .arg(err.errorString(), QString::fromUtf8(line)));
      continue;
    }

    QJsonObject obj = doc.object();
    QString event   = obj.value(QStringLiteral("event")).toString();

    if (event == QLatin1String("property-change")) {
      QString name    = obj.value(QStringLiteral("name")).toString();
      QVariant value  = obj.value(QStringLiteral("data")).toVariant();
      emit propertyChanged(name, value);

    } else if (event == QLatin1String("file-loaded")) {
      emit fileLoaded();

    } else if (event == QLatin1String("end-file")) {
      if (!mPlaybackEndedEmitted) {
        mPlaybackEndedEmitted = true;
        emit playbackFinished();
      }
    }
    // All other events (e.g. "seek", "start-file", ...) are silently ignored.
  }
}

//! Called when the mpv process exits.
void TTMpvProcessBackend::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
  Q_UNUSED(exitCode)
  Q_UNUSED(status)

  // Guard against double-emit if an "end-file" IPC event already fired
  if (!mPlaybackEndedEmitted) {
    mPlaybackEndedEmitted = true;
    emit playbackFinished();
  }
}
