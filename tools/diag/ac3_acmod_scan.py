#!/usr/bin/env python3
"""Channel-layout changes of an AC3 elementary stream, frame by frame.

Walks the sync frames of the file and prints one line whenever acmod, lfeon,
bsid, fscod or frmsizecod differs from the previous frame, with the frame
index, its start time and its byte offset. Independent of TTCut-ng's own
parser - use it to check what the audio-change markers
(TTStreamPointAudioWorker::detectAudioChanges) should show, or to compare a
demuxed track against the original recording:

  ffmpeg -i rec.ts -map 0:a:N -c copy -f ac3 orig.ac3
  ac3_acmod_scan.py orig.ac3

The frame length comes from the header (fscod, frmsizecod); bytes that do not
start a valid frame are skipped one at a time and counted as "resync bytes".
AC3 only - E-AC3 (bsid > 10) has a different header.

Usage: ac3_acmod_scan.py <file.ac3>
"""
import mmap
import sys

BITRATES = [32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320,
            384, 448, 512, 576, 640]
MAIN_CHANNELS = [2, 1, 2, 3, 3, 4, 4, 5]      # by acmod
SAMPLE_RATES = [48000, 44100, 32000]          # by fscod


def frame_bytes(fscod, frmsizecod):
    kbit = BITRATES[frmsizecod >> 1]
    if fscod == 0:
        return 4 * kbit
    if fscod == 1:
        return 2 * (kbit * 320 // 147 + (frmsizecod & 1))
    return 6 * kbit


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip().splitlines()[-1])
    try:
        f = open(sys.argv[1], 'rb')
        data = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    except (OSError, ValueError) as e:
        sys.exit(f"cannot read {sys.argv[1]}: {e}")

    pos = frames = resync = 0
    seconds = 0.0
    prev = None
    while pos + 8 <= len(data):
        fscod, frmsizecod = data[pos + 4] >> 6, data[pos + 4] & 0x3F
        if data[pos] != 0x0B or data[pos + 1] != 0x77 or fscod == 3 or frmsizecod > 37:
            pos += 1
            resync += 1
            continue
        bsid = data[pos + 5] >> 3
        # 16 bits after bsid/bsmod: acmod(3), then cmixlev / surmixlev /
        # dsurmod (2 bits each, depending on acmod), then lfeon(1).
        bits = (data[pos + 6] << 8) | data[pos + 7]
        acmod = bits >> 13
        shift = 13
        if (acmod & 1) and acmod != 1:
            shift -= 2
        if acmod & 4:
            shift -= 2
        if acmod == 2:
            shift -= 2
        lfeon = (bits >> (shift - 1)) & 1

        cur = (acmod, lfeon, bsid, fscod, frmsizecod)
        if cur != prev:
            print(f"frame {frames:8d} t={seconds:10.3f}s off={pos:10d} "
                  f"acmod={acmod} lfeon={lfeon} layout={MAIN_CHANNELS[acmod]}.{lfeon} "
                  f"bsid={bsid} fscod={fscod} frmsizecod={frmsizecod}")
            prev = cur
        frames += 1
        seconds += 1536 / SAMPLE_RATES[fscod]
        pos += frame_bytes(fscod, frmsizecod)
    print("frames", frames, "resync bytes", resync)


if __name__ == '__main__':
    main()
