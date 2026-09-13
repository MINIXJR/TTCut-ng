/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTENCODERNAMES_H
#define TTENCODERNAMES_H

// The option names the encoders consume and the settings pages list. The
// index into each table is what TTSettings stores (encoderPreset/Profile,
// mpeg2Target), so the order is part of the persisted format: append, never
// reorder. One table each; the Smart Cut engine and every combo box read
// the same one.
namespace TTEncoderNames {

// libx264 / libx265 speed presets, --preset
inline constexpr const char* const kPresets[] = {
    "ultrafast", "superfast", "veryfast", "faster", "fast",
    "medium", "slow", "slower", "veryslow"
};
inline constexpr int kPresetCount = int(sizeof(kPresets) / sizeof(kPresets[0]));

// libx264 --profile
inline constexpr const char* const kH264Profiles[] = {
    "baseline", "main", "high", "high10", "high422", "high444"
};
inline constexpr int kH264ProfileCount = int(sizeof(kH264Profiles) / sizeof(kH264Profiles[0]));

// libx265 --profile
inline constexpr const char* const kH265Profiles[] = {
    "main", "main10", "main12", "main422-10", "main444-10"
};
inline constexpr int kH265ProfileCount = int(sizeof(kH265Profiles) / sizeof(kH265Profiles[0]));

// mplex -f targets as listed to the user (the index is the stored
// mpeg2Target; the format number in brackets is what mplex gets).
inline constexpr const char* const kMpeg2MuxTargets[] = {
    "Generic MPEG1 (f0)", "VCD (f1)", "user-rate VCD (f2)", "Generic MPEG2 (f3)",
    "SVCD (f4)", "user-rate SVCD (f5)", "VCD Stills (f6)",
    "DVD with NAV sectors (f8)", "DVD (f9)"
};
inline constexpr int kMpeg2MuxTargetCount = int(sizeof(kMpeg2MuxTargets) / sizeof(kMpeg2MuxTargets[0]));

} // namespace TTEncoderNames

#endif // TTENCODERNAMES_H
