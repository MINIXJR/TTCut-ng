#!/bin/bash
# Test files for the donor fill (tools/diag/test_donorfill.cpp,
# test_donorfill_app.cpp): 20 s of band-limited noise as 5.1 and as stereo
# AC3, each with three holes of 816 samples (17 ms) in every channel and
# once without, plus two-channel donors that carry the same sound later by a
# known number of samples, and one donor with unrelated sound.
#
# Every channel is noise of its own (anoisesrc, one seed each): a fill taken
# from the wrong channel does not fit. The centre of the 5.1 file carries the
# sound, the other channels are 20 to 24 dB quieter.
#
# An input sample n lands at n + 256 in the decoded AC3 (encoder delay), so
# with frames of 1536 samples the holes lie, in decoded samples:
#   A  240000..240815   inside frame 156
#   B  383600..384415   across the boundary of frames 249/250 (384000)
#   C  527484..528299   ends 84 samples before frame 344 (528384)
# ffmpeg's AC3 encoder smears the edges, so the quiet part found is shorter
# (measured 2026-10-10: 289 to 478 samples).
#
#   usage: make_donorfill_sample.sh <outdir>
set -e
OUT="${1:?usage: make_donorfill_sample.sh <outdir>}"
mkdir -p "$OUT"
FF="ffmpeg -y -v error"
# One mono noise input: seed, amplitude.
N() { echo "-f lavfi -i anoisesrc=c=white:s=$1:a=$2:r=48000:d=20"; }
$FF $(N 1 0.05) $(N 2 0.05) $(N 3 0.5) -f lavfi -i "anullsrc=r=48000:cl=mono:d=20" $(N 4 0.03) $(N 5 0.03) \
  -filter_complex "[0:a][1:a][2:a][3:a][4:a][5:a]join=inputs=6:channel_layout=5.1(side):map=0.0-FL|1.0-FR|2.0-FC|3.0-LFE|4.0-SL|5.0-SR,lowpass=f=5000" \
  -c:a pcm_f32le "$OUT/src51.wav"
$FF $(N 6 0.3) $(N 7 0.3) -filter_complex "[0:a][1:a]join=inputs=2:channel_layout=stereo:map=0.0-FL|1.0-FR,lowpass=f=5000" \
  -c:a pcm_f32le "$OUT/src20.wav"
H="(1-between(n\,239744\,240559)-between(n\,383344\,384159)-between(n\,527228\,528043))"
$FF -i "$OUT/src51.wav" -af "aeval=val(0)*$H|val(1)*$H|val(2)*$H|val(3)|val(4)*$H|val(5)*$H" -c:a ac3 -b:a 384k "$OUT/hole51.ac3"
$FF -i "$OUT/src51.wav" -c:a ac3 -b:a 384k "$OUT/clean51.ac3"
$FF -i "$OUT/src20.wav" -af "aeval=val(0)*$H|val(1)*$H" -c:a ac3 -b:a 192k "$OUT/hole20.ac3"
$FF -i "$OUT/src20.wav" -c:a ac3 -b:a 192k "$OUT/clean20.ac3"
# Donors: the stereo downmix, delayed by 300 or 700 samples.
MIX="pan=stereo|c0=c0+0.707*c2|c1=c1+0.707*c2"
$FF -i "$OUT/src51.wav" -af "$MIX,adelay=delays=300S:all=1" -c:a mp2 -b:a 192k "$OUT/donor51_300.mp2"
$FF -i "$OUT/src51.wav" -af "$MIX,adelay=delays=700S:all=1" -c:a mp2 -b:a 192k "$OUT/donor51_700.mp2"
$FF -i "$OUT/src51.wav" -af "$MIX,adelay=delays=300S:all=1" -c:a ac3 -b:a 192k "$OUT/donor51_300.ac3"
$FF -i "$OUT/src20.wav" -af "adelay=delays=300S:all=1" -c:a mp2 -b:a 192k "$OUT/donor20_300.mp2"
# A layout the fill does not support.
$FF -i "$OUT/src20.wav" -ac 1 -c:a ac3 -b:a 96k "$OUT/mono.ac3"
# A two-channel track with other sound. Written last: the harnesses take its
# presence as "the set is complete".
$FF $(N 8 0.3) $(N 9 0.3) -filter_complex "[0:a][1:a]join=inputs=2:channel_layout=stereo:map=0.0-FL|1.0-FR,lowpass=f=5000" \
  -c:a mp2 -b:a 192k "$OUT/unrelated.mp2"
rm -f "$OUT/src51.wav" "$OUT/src20.wav"
echo "$OUT"
