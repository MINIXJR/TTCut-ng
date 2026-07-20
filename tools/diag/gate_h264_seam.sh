#!/usr/bin/env bash
# Gate: H.264 smart-cut seam quality (defect A heal / regression).
#
# Runs test_smartcut_seam on ONE keep-range, decodes source and output with
# framemd5 (-fps_mode passthrough as OUTPUT option: raw H.264 ES misdetects
# r_frame_rate as 2x and silently drops frames otherwise), aligns output
# position p to source display (cutIn+p) and verdicts:
#   COUNT   output frame count == cutOut-cutIn+1
#   DECERR  zero decoder errors while decoding the output
#   ORDER   no output frame's hash matches a WRONG source display
#           (catches the seam display-order inversion / duplicates)
#   COPY    every copy-region frame is bit-identical to its aligned source
#           frame, EXCEPT leading pictures resolved against re-encode
#           standins: those must be the RIGHT frame at re-encode quality —
#           SSIM(aligned) >= 0.90 AND SSIM(aligned) > SSIM(neighbor +/-1).
# The re-encode region (first R positions, R from the smart-cut log line
# "Selected R frames for encoding") is exempt from bit-identity.
#
# Usage: gate_h264_seam.sh <test_smartcut_seam-bin> <src-es> <cutIn> <cutOut> <fps> [injectmap]
set -euo pipefail
BIN="${1:?usage: gate_h264_seam.sh <seam-bin> <src-es> <cutIn> <cutOut> <fps> [injectmap]}"
SRC="${2:?}"; CI="${3:?}"; CO="${4:?}"; FPS="${5:?}"; INJ="${6:-}"
WORK=/usr/local/src/CLAUDE_TMP/TTCut-ng/eos_nonidr/gate_seam; mkdir -p "$WORK"
TAG="$(basename "$SRC")_${CI}_${CO}"
OUT="$WORK/cut_$TAG.264"; LOG="$WORK/log_$TAG.txt"

"$BIN" "$SRC" "$OUT" "$CI" "$CO" "$FPS" $INJ 2>"$LOG" || { echo "VERDICT: FAIL (smartcut rc)"; exit 1; }
R=$(grep -oP 'Selected \K[0-9]+(?= frames for encoding)' "$LOG" | head -1 || true); R=${R:-0}
# Second occurrence = tail re-encode at the cut-out (mid-GOP cut-out); those
# positions sit at the END of the output and are exempt from bit-identity.
T=$(grep -oP 'Selected \K[0-9]+(?= frames for encoding)' "$LOG" | sed -n 2p || true); T=${T:-0}
BRANCH=$(grep -q "SPS Unification" "$LOG" && echo UNI || echo STD)
SEAM=$(grep -oP 'extending re-encode to keyframe \K[0-9]+' "$LOG" | head -1 || true)
[ -z "$SEAM" ] && SEAM=$(grep -oP 'Stream-copy from I-slice \K[0-9]+' "$LOG" | head -1 || true)
echo "INFO: branch=$BRANCH reencode=$R tail=$T seamAU=${SEAM:-none} log=$LOG"

SRCMD5="$WORK/src_$(basename "$SRC").md5"
[ -s "$SRCMD5" ] || ffmpeg -v error -i "$SRC" -fps_mode passthrough -f framemd5 - 2>/dev/null \
    | grep -v '^#' | awk '{print $6}' > "$SRCMD5"
ffmpeg -v error -i "$OUT" -fps_mode passthrough -f framemd5 - 2>"$WORK/decerr_$TAG.txt" \
    | grep -v '^#' | awk '{print $6}' > "$WORK/out_$TAG.md5"

python3 - "$SRCMD5" "$WORK/out_$TAG.md5" "$CI" "$CO" "$R" "$SRC" "$OUT" "$WORK/decerr_$TAG.txt" "$T" <<'PY'
import os, subprocess, sys
src = open(sys.argv[1]).read().split()
out = open(sys.argv[2]).read().split()
ci, co, r = int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
srces, outes, decerr = sys.argv[6], sys.argv[7], sys.argv[8]
tail = int(sys.argv[9])
work = os.path.dirname(sys.argv[1])
rc = 0
def verdict(name, ok, msg=""):
    global rc
    print(f"VERDICT: {'PASS' if ok else 'FAIL'} {name} {msg}")
    if not ok: rc = 1
n_expect = co - ci + 1
verdict("COUNT", len(out) == n_expect, f"({len(out)}/{n_expect})")
errs = sum(1 for l in open(decerr) if l.strip())
verdict("DECERR", errs == 0, f"({errs} decoder error lines)")
if len(out) != n_expect:
    sys.exit(1)   # positional alignment is meaningless below
srcmap = {}
for d, h in enumerate(src):
    srcmap.setdefault(h, []).append(d)
def ssim(a_es, a_idx, b_es, b_idx):
    if b_idx < 0 or b_idx >= len(src):
        return -1.0
    for es, idx, png in ((a_es, a_idx, "tmp_a.png"), (b_es, b_idx, "tmp_b.png")):
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", es,
                        "-vf", f"select=eq(n\\,{idx})", "-fps_mode", "passthrough",
                        "-update", "1", os.path.join(work, png)], check=True)
    p = subprocess.run(["ffmpeg", "-i", os.path.join(work, "tmp_a.png"),
                        "-i", os.path.join(work, "tmp_b.png"),
                        "-lavfi", "ssim", "-f", "null", "-"],
                       capture_output=True, text=True)
    for tok in p.stderr.split():
        if tok.startswith("All:"):
            return float(tok[4:].split()[0])
    return -1.0
order_ok, copy_ok = True, True
suspects = []
for p, h in enumerate(out):
    aligned = ci + p
    if h == src[aligned]:
        continue
    hits = srcmap.get(h, [])
    if hits:                          # matches a WRONG display -> inversion/dup
        order_ok = False
        print(f"  ORDER violation: out[{p}] (slot {aligned}) matches source display {hits}")
    elif r <= p < len(out) - tail:    # copy region, matches nothing -> SSIM check
        suspects.append(p)
verdict("ORDER", order_ok)
for p in suspects[:8]:
    aligned = ci + p
    s_al = ssim(outes, p, srces, aligned)
    s_nb = max(ssim(outes, p, srces, aligned - 1), ssim(outes, p, srces, aligned + 1))
    # >= not >: on static content / source duplicate frames the aligned and
    # neighbor SSIM legitimately tie — content is then indistinguishable.
    ok = s_al >= 0.90 and s_al >= s_nb
    print(f"  copy-region no-match out[{p}] (slot {aligned}): "
          f"SSIM(aligned)={s_al:.4f} SSIM(best neighbor)={s_nb:.4f} -> {'ok' if ok else 'BAD'}")
    if not ok: copy_ok = False
if len(suspects) > 8:
    print(f"  ... {len(suspects)-8} more no-match copy positions (not SSIM-checked)")
    copy_ok = False
verdict("COPY", copy_ok, f"({len(suspects)} standin-resolved position(s))")
sys.exit(rc)
PY
