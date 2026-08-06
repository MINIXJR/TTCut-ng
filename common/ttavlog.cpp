/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttavlog.h"

#include "ttmessagelogger.h"
#include "ttsettings.h"

#include <cstdarg>
#include <cstring>

extern "C" {
#include <libavutil/log.h>
}

// ---------------------------------------------------------------------------
// libav log callback: gated on TTSettings::logLibav() (default off, since
// libav is very chatty). When enabled, maps AV_LOG_ levels onto matching
// TTMessageLogger severities and strips trailing newlines that libav emits.
// ---------------------------------------------------------------------------
static void ttAvLogCallback(void* avcl, int level, const char* fmt, va_list vl)
{
  if (!TTSettings::instance()->logLibav()) return;
  if (level > av_log_get_level()) return;
  char buf[1024];
  int prefix = 0;
  av_log_format_line(avcl, level, fmt, vl, buf, sizeof(buf), &prefix);
  size_t n = std::strlen(buf);
  while (n && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
  if (!n) return;
  TTMessageLogger* log = TTMessageLogger::getInstance();
  // libav emits UTF-8; fromLocal8Bit would mangle non-ASCII codec/file
  // names on non-UTF-8 locales.
  QString qmsg = QString::fromUtf8(buf, static_cast<int>(n));
  if      (level <= AV_LOG_ERROR)   log->errorMsg("libav", 0, qmsg);
  else if (level <= AV_LOG_WARNING) log->warningMsg("libav", 0, qmsg);
  else if (level <= AV_LOG_INFO)    log->infoMsg("libav", 0, qmsg);
  else                              log->debugMsg("libav", 0, qmsg);
}

void ttInstallAvLogCallback()
{
  av_log_set_callback(ttAvLogCallback);
}
