/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTAVLOG_H
#define TTAVLOG_H

// Installs TTCut's process-global libav log callback (gated on
// TTSettings::logLibav(), routed through TTMessageLogger).
//
// Called once at startup (ttcutmain.cpp) and again after every
// mpv_terminate_destroy(): libmpv takes over the process-global av_log
// callback at mpv_create() and restores ffmpeg's DEFAULT stderr callback
// (not ours) when the last mpv context is destroyed — without this
// re-install, every later libav operation would log raw to the console.
void ttInstallAvLogCallback();

#endif // TTAVLOG_H
