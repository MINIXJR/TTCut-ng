#!/usr/bin/env bash
# Gate: ttcut-audiofix -p assembles an ES from a slot plan.
#   k SIZE  copy one frame of SIZE bytes (silence frame if those bytes are no frame)
#   d SIZE  skip SIZE bytes (one op per dropped frame; the stats count ops)
#   s N     write N silence frames
# Built from a generated MP2: 100 frames of a sine, a 1 s silence master.
# For MP2 the silence frame is built from the header of the neighbouring
# real frame (all allocation zero), so it is no foreign frame in the track:
# libav skips the first MP2 frame of a file whose header differs from the
# second one's (mode, copyright, original, emphasis) - measured 2026-10-08
# on a recording that got ONE leading silence frame from the encoder master.
# Usage: gate_audiofix_assemble.sh <ttcut-audiofix-bin>
set -u
BIN="${1:?usage: gate_audiofix_assemble.sh <bin>}"
WORK=/usr/local/src/CLAUDE_TMP/TTCut-ng/audiofix_assemble; mkdir -p "$WORK"
PASS=0; FAIL=0
ok()  { echo "PASS: $1"; PASS=$((PASS+1)); }
bad() { echo "FAIL: $1"; FAIL=$((FAIL+1)); }
[ -x "$BIN" ] || { echo "FAIL: $BIN missing (cmake --build build --target ttcut-audiofix)"; exit 1; }

ffmpeg -v error -y -f lavfi -i "sine=f=440:r=48000" -t 2.4 -ac 2 -c:a mp2 -b:a 192k "$WORK/src.mp2" || { echo "FAIL: ffmpeg"; exit 1; }
ffmpeg -v error -y -f lavfi -i "anullsrc=r=48000:cl=stereo" -t 1 -c:a mp2 -b:a 192k "$WORK/sil.mp2" || { echo "FAIL: ffmpeg"; exit 1; }
FS=576   # MP2 192 kbit/s @ 48 kHz
N=$(( $(stat -c %s "$WORK/src.mp2") / FS ))
[ "$N" -ge 100 ] || { echo "FAIL: source has $N frames, want >= 100"; exit 1; }

# Plan: keep 10 (frames 0-9), drop 2 (10-11), keep 5 (12-16), silence 3,
# keep the rest (17..N-1).
{ for i in $(seq 1 10); do echo "k $FS"; done
  echo "d $FS"; echo "d $FS"
  for i in $(seq 1 5); do echo "k $FS"; done
  echo "s 3"
  for i in $(seq 17 $((N - 1))); do echo "k $FS"; done; } > "$WORK/plan.txt"
"$BIN" -p "$WORK/plan.txt" -s "$WORK/sil.mp2" "$WORK/src.mp2" "$WORK/out.mp2" > "$WORK/stats.txt" 2>&1; RC=$?
[ "$RC" = 0 ] && ok "exit 0" || bad "exit $RC: $(head -1 "$WORK/stats.txt")"
grep -q "^kept=$((N - 2)) dropped=2 silence=3 replaced=0 short=0 tail_bytes=0 first_copy_packet=0 first_copy_offset=0$" "$WORK/stats.txt" \
  && ok "stats: $(cat "$WORK/stats.txt")" || bad "stats: $(head -1 "$WORK/stats.txt")"

python3 - "$WORK" "$FS" "$N" <<'EOF' && ok "frame placement" || bad "frame placement"
import sys
w, fs, n = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
src = open(f"{w}/src.mp2", "rb").read(); out = open(f"{w}/out.mp2", "rb").read()
f = lambda d, i: d[i*fs:(i+1)*fs]
hdr = lambda fr: bytes([fr[0], fr[1], fr[2] & ~0x02, fr[3]])      # header without the padding bit
sil = hdr(f(src, 17)) + bytes(fs - 4)                              # header of the next real frame, nothing allocated
assert len(out) == (n - 2 + 3) * fs, len(out)
assert all(f(out, i) == f(src, i) for i in range(10))             # keep 10
assert all(f(out, 10 + i) == f(src, 12 + i) for i in range(5))    # drop 2, keep 5
assert all(f(out, 15 + i) == sil for i in range(3)), f(out, 15)[:8].hex()   # 3 silence frames
assert all(f(out, 18 + i) == f(src, 17 + i) for i in range(n - 17))
EOF
ERRS=$(ffmpeg -nostdin -v error -y -i "$WORK/out.mp2" -f s16le -ac 2 "$WORK/out.pcm" 2>&1 | wc -l)
python3 - "$WORK" <<'EOF' && [ "$ERRS" = 0 ] && ok "silence frames decode without error to digital silence" || bad "silence frames: $ERRS decoder error line(s) or samples not silent"
import sys, array
pcm = array.array("h"); pcm.frombytes(open(sys.argv[1] + "/out.pcm", "rb").read())
lo, hi = (16 * 1152 + 600) * 2, (17 * 1152) * 2                    # inside the second of the three silence frames
assert max(abs(x) for x in pcm[lo:hi]) <= 1, max(abs(x) for x in pcm[lo:hi])
EOF

# Damaged keep: break the sync word of frame 20 -> that slot becomes silence.
python3 - "$WORK" "$FS" <<'EOF'
import sys
w, fs = sys.argv[1], int(sys.argv[2])
d = bytearray(open(f"{w}/src.mp2", "rb").read()); d[20*fs] = 0x00; open(f"{w}/dmg.mp2", "wb").write(d)
EOF
"$BIN" -p "$WORK/plan.txt" -s "$WORK/sil.mp2" "$WORK/dmg.mp2" "$WORK/out2.mp2" > "$WORK/stats2.txt" 2>&1
grep -q " replaced=1 " "$WORK/stats2.txt" && ok "damaged keep replaced" || bad "damaged keep: $(head -1 "$WORK/stats2.txt")"
python3 - "$WORK" "$FS" <<'EOF' && ok "damaged slot holds a silence frame, following frames unshifted" || bad "damaged slot"
import sys
w, fs = sys.argv[1], int(sys.argv[2])
src = open(f"{w}/src.mp2", "rb").read(); out = open(f"{w}/out2.mp2", "rb").read()
f = lambda d, i: d[i*fs:(i+1)*fs]
assert f(out, 21)[4:] == bytes(fs - 4) and f(out, 21)[:2] == b"\xff\xfd"   # input frame 20 -> output slot 21 (3 silence - 2 dropped)
assert f(out, 22) == f(src, 21)
EOF

# Plan running past the input: the k ops beyond EOF become silence, counted as short.
{ for i in $(seq 1 $((N + 2))); do echo "k $FS"; done; } > "$WORK/plan3.txt"
"$BIN" -p "$WORK/plan3.txt" -s "$WORK/sil.mp2" "$WORK/src.mp2" "$WORK/out3.mp2" > "$WORK/stats3.txt" 2>&1
grep -q "^kept=$N dropped=0 silence=0 replaced=0 short=2 tail_bytes=0 first_copy_packet=0 first_copy_offset=0$" "$WORK/stats3.txt" \
  && ok "short keeps become silence" || bad "short: $(head -1 "$WORK/stats3.txt")"

# Where the first COPIED frame comes from and where it went: packet index
# (k and d ops before it) and byte offset in the output. ttcut-demux proves
# its start offset on that frame. Here: 2 packets dropped, 3 silence frames,
# then a damaged packet (replaced), so packet 3 is the first copy, behind 4
# silence frames.
python3 - "$WORK" "$FS" <<'EOF'
import sys
w, fs = sys.argv[1], int(sys.argv[2])
d = bytearray(open(f"{w}/src.mp2", "rb").read()); d[2*fs] = 0x00; open(f"{w}/dmg2.mp2", "wb").write(d)
EOF
{ echo "d $FS"; echo "d $FS"; echo "s 3"; for i in $(seq 1 10); do echo "k $FS"; done; } > "$WORK/plan6.txt"
"$BIN" -p "$WORK/plan6.txt" -s "$WORK/sil.mp2" "$WORK/dmg2.mp2" "$WORK/out6.mp2" > "$WORK/stats6.txt" 2>&1
grep -q " replaced=1 .* first_copy_packet=3 first_copy_offset=$((4 * FS))$" "$WORK/stats6.txt" \
  && cmp -s <(tail -c +$((4 * FS + 1)) "$WORK/out6.mp2" | head -c "$FS") <(tail -c +$((3 * FS + 1)) "$WORK/dmg2.mp2" | head -c "$FS") \
  && ok "first copied frame reported: packet 3 at byte $((4 * FS))" || bad "first copy: $(head -1 "$WORK/stats6.txt")"

# One leading silence frame in front of frames whose header differs from the
# encoder master's (original bit off, as a broadcaster sends it): libav must
# see the first packet at byte 0 and every frame of the file.
python3 - "$WORK" "$FS" <<'EOF'
import sys
w, fs = sys.argv[1], int(sys.argv[2])
d = bytearray(open(f"{w}/src.mp2", "rb").read())
for i in range(0, len(d) - 3, fs): d[i + 3] &= ~0x04
open(f"{w}/bc.mp2", "wb").write(d)
EOF
{ echo "s 1"; for i in $(seq 1 "$N"); do echo "k $FS"; done; } > "$WORK/plan4.txt"
"$BIN" -p "$WORK/plan4.txt" -s "$WORK/sil.mp2" "$WORK/bc.mp2" "$WORK/out4.mp2" > "$WORK/stats4.txt" 2>&1
first_pos() { ffprobe -v error -select_streams a:0 -show_entries packet=pos -of csv=p=0 "$1" 2>/dev/null | head -1 | tr -d ','; }
n_packets() { ffprobe -v error -select_streams a:0 -count_packets -show_entries stream=nb_read_packets -of csv=p=0 "$1" 2>/dev/null | tr -d ','; }
[ "$(first_pos "$WORK/out4.mp2")" = 0 ] && [ "$(n_packets "$WORK/out4.mp2")" = $((N + 1)) ] \
  && ok "one leading silence frame: libav reads all $((N + 1)) frames from byte 0" \
  || bad "one leading silence frame: libav starts at byte $(first_pos "$WORK/out4.mp2"), $(n_packets "$WORK/out4.mp2") of $((N + 1)) packets"

# A track with CRC whose first frame is joint stereo and the rest stereo
# (libtwolame, 128 kbit/s = 384 B frames): every built silence frame has
# the header of the frame behind it and a valid CRC.
if ffmpeg -hide_banner -encoders 2>/dev/null | grep -q libtwolame; then
    FS=384
    ffmpeg -v error -y -f lavfi -i "sine=f=440:r=48000" -f lavfi -i "sine=f=660:r=48000" \
        -filter_complex "[0][1]amerge=inputs=2" -t 2.4 -c:a libtwolame -b:a 128k \
        -mode joint_stereo -error_protection 1 "$WORK/crc.mp2" || { echo "FAIL: ffmpeg libtwolame"; exit 1; }
    NC=$(( $(stat -c %s "$WORK/crc.mp2") / FS ))
    { echo "s 2"; for i in $(seq 1 50); do echo "k $FS"; done; echo "s 3"; for i in $(seq 51 "$NC"); do echo "k $FS"; done; } > "$WORK/plan5.txt"
    "$BIN" -p "$WORK/plan5.txt" -s "$WORK/sil.mp2" "$WORK/crc.mp2" "$WORK/out5.mp2" > "$WORK/stats5.txt" 2>&1
    "$BIN" -a "$WORK/out5.mp2" > "$WORK/analyze5.txt" 2>&1; ARC=$?
    ERRS=$(ffmpeg -nostdin -v error -i "$WORK/out5.mp2" -f null - 2>&1 | wc -l)
    python3 - "$WORK" "$FS" <<'EOF' && [ "$ARC" = 0 ] && [ "$ERRS" = 0 ] && [ "$(first_pos "$WORK/out5.mp2")" = 0 ] \
      && ok "CRC track with a joint-stereo frame: silence frames carry the neighbour's header and a valid CRC" \
      || bad "CRC + joint stereo: analyze rc=$ARC ($(grep crc_bad "$WORK/analyze5.txt")), $ERRS decoder error line(s), first packet at byte $(first_pos "$WORK/out5.mp2")"
import sys
w, fs = sys.argv[1], int(sys.argv[2])
src = open(f"{w}/crc.mp2", "rb").read(); out = open(f"{w}/out5.mp2", "rb").read()
f = lambda d, i: d[i*fs:(i+1)*fs]
assert src[1] == 0xFC and (src[3] >> 6) == 1, src[:4].hex()        # frame 0 really has CRC and joint stereo
for i, nxt in ((0, 0), (1, 0), (52, 50), (53, 50), (54, 50)):       # silence frame -> the source frame behind it
    fr = f(out, i)
    assert fr[:4] == f(src, nxt)[:4] and fr[6:] == bytes(fs - 6), (i, fr[:8].hex(), f(src, nxt)[:4].hex())
assert f(out, 2) == f(src, 0) and f(out, 55) == f(src, 50)
EOF
else
    echo "note: ffmpeg has no libtwolame - CRC / joint stereo case not run"
fi

echo "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
