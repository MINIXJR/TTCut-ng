#!/usr/bin/env bash
# Gate: HEVC display-order map RASL alignment.
#
# Verifies decodeFrame(N) (the still path) lands on ffmpeg's DISPLAY frame N
# (independent ground truth) after the first-CRA RASL leading pics are dropped
# from the display dimension. Pre-fix this was off by a constant (+7 on the
# reference stream); post-fix N=0 is the CRA (first actually-displayed frame).
#
# Metric: Pearson correlation, NOT raw SSIM/MAD. ffmpeg's `format=gray`
# range-expands Y (limited 16-235 -> full 0-255) while our decoder Y is raw
# limited-range, so absolute-difference metrics show a brightness-scaled offset
# on IDENTICAL frames. Pearson is invariant to that linear range/colorspace
# difference: same frame -> ~1.0, wrong frame -> low (a built-in wrong-pair
# control is included).
#
# Requires: a built `dump_img` helper (decodeFrame(N) -> gray PGM), ffmpeg,
# python3 + numpy + Pillow.
#
# Usage: gate_hevc_align.sh <dump_img> <es> [N ...]
set -euo pipefail
DUMP="${1:?usage: gate_hevc_align.sh <dump_img-binary> <es> [N ...]}"; shift
ES="${1:?need an elementary-stream path}"; shift
NS=("$@"); [ ${#NS[@]} -eq 0 ] && NS=(0 1 1190 5000)
OUT=/usr/local/src/CLAUDE_TMP/TTCut-ng/gate_align; mkdir -p "$OUT"

for N in "${NS[@]}"; do
  "$DUMP" "$ES" "$N" "$OUT/dec_$N.pgm" >/dev/null 2>&1
  ffmpeg -v error -y -i "$ES" -vf "select=eq(n\,$N),format=gray" -vframes 1 "$OUT/ff_$N.png" 2>/dev/null
done
# wrong-pair control: a deliberately offset ground-truth frame (Ns[0]+1000)
CTRL=$(( ${NS[0]} + 1000 ))
ffmpeg -v error -y -i "$ES" -vf "select=eq(n\,$CTRL),format=gray" -vframes 1 "$OUT/ff_ctrl.png" 2>/dev/null

python3 - "$OUT" "${NS[@]}" <<'PY'
import sys, numpy as np
from PIL import Image
outdir = sys.argv[1]; Ns = [int(x) for x in sys.argv[2:]]
def pgm(p):
    f=open(p,'rb'); assert f.readline().strip()==b'P5'
    w,h=map(int,f.readline().split()); f.readline()
    return np.frombuffer(f.read(w*h),np.uint8).reshape(h,w).astype(float)
def png(p): return np.asarray(Image.open(p).convert('L'),float)
def pear(a,b):
    n=min(a.size,b.size); return np.corrcoef(a.ravel()[:n], b.ravel()[:n])[0,1]
rc=0
for N in Ns:
    r=pear(pgm(f"{outdir}/dec_{N}.pgm"), png(f"{outdir}/ff_{N}.png"))
    ok = r>0.99
    print(f"{'PASS' if ok else 'FAIL'} decodeFrame({N}) vs ffmpeg-display-{N}  r={r:.5f}")
    if not ok: rc=1
# wrong-pair control: decodeFrame(Ns[0]) vs a far-offset ground-truth frame
# (Ns[0]+1000) must NOT correlate — confirms the metric discriminates.
a=pgm(f"{outdir}/dec_{Ns[0]}.pgm"); b=png(f"{outdir}/ff_ctrl.png")
rc_ctrl=pear(a,b)
print(f"CONTROL decodeFrame({Ns[0]}) vs ffmpeg-display-{Ns[0]+1000} r={rc_ctrl:.5f} (expect LOW)")
if rc_ctrl>0.9: rc=1
sys.exit(rc)
PY
