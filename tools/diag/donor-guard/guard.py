# How well does each MP2 track of a recording match the AC3 track next to a
# pretended 17 ms hole? Same fit as the hearing sample of 2026-10-05
# (a script outside the repository): 150 ms before and after the spot, 5 ms away from its
# edges, best lag within +/-200 ms. 5.1 spot: AC3 centre against MP2 mid (L+R)/2;
# stereo spot: AC3 L against MP2 L. One spot every 20 s; spots whose target
# channel is below -35 dBFS in the fit windows are skipped.
# usage: guard.py OUT.txt AC3 NAME=MP2 [NAME=MP2 ...]
import subprocess, sys, numpy as np
OUT, AC3 = sys.argv[1], sys.argv[2]
DONORS = [a.split("=", 1) for a in sys.argv[3:]]
FS = 48000; HALF = 12; W = 7200; G = 240; HOLE = 816; MAXLAG = 9600
KB = [32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640]
BR = [0,32,48,56,64,80,96,112,128,160,192,224,256,320,384]
def walk_ac3(d):
    o = []; lay = []; p = 0
    while p + 7 < len(d):
        assert d[p] == 0x0B and d[p+1] == 0x77, f"no AC3 sync at {p}"
        acmod = d[p+6] >> 5
        lfe = (d[p+6] & 1) if acmod == 7 else ((d[p+6] >> 2) & 1) if acmod == 2 else -1
        o.append(p); lay.append((acmod, lfe)); p += KB[(d[p+4] & 0x3F) >> 1] * 4
    o.append(p); return o, lay
def walk_mp2(d):
    o = []; p = 0
    while p + 4 <= len(d):
        h = int.from_bytes(d[p:p+4], "big")
        assert (h >> 21) == 0x7FF, f"no MP2 header at {p}"
        o.append(p); p += 144 * BR[(h >> 12) & 15] * 1000 // FS + ((h >> 9) & 1)
    o.append(p); return o
def decode(b, ch, fmt):
    pre = ["-drc_scale", "0", "-f", "ac3"] if fmt == "ac3" else ["-f", "mp3"]
    raw = subprocess.run(["ffmpeg", "-v", "error", *pre, "-i", "pipe:0", "-f", "f32le", "-"],
                         input=b, stdout=subprocess.PIPE, check=True).stdout
    return np.frombuffer(raw, dtype="<f4").reshape(-1, ch).astype(np.float64)
def db(p): return 10 * np.log10(p) if p > 1e-18 else -180.0
def fit(c, d, nominal, i0, i1):
    """best lag, gain, correlation and residual (dB) of donor d against target c"""
    blocks = [(i0 - G - W, i0 - G), (i1 + G, i1 + G + W)]
    cross = np.zeros(2 * MAXLAG + 1); e = np.zeros(2 * MAXLAG + 1); ec = 0.0
    for a, b in blocks:
        seg = d[a + nominal - MAXLAG:b + nominal + MAXLAG]
        cross += np.correlate(seg, c[a:b], "valid")
        cs = np.concatenate(([0.0], np.cumsum(seg ** 2)))
        e += cs[b - a:] - cs[:len(cs) - (b - a)]
        ec += float(np.dot(c[a:b], c[a:b]))
    cc = cross / (np.sqrt(e * ec) + 1e-30)
    k = int(np.argmax(cc)); g = cross[k] / (e[k] + 1e-30)
    res = ec - 2 * g * cross[k] + g * g * e[k]
    return k - MAXLAG, g, float(cc[k]), db(ec) - db(max(res, 1e-30))
a = open(AC3, "rb").read(); ao, lay = walk_ac3(a)
don = []
for name, path in DONORS:
    m = open(path, "rb").read(); don.append((name, m, walk_mp2(m)))
nfr = len(ao) - 1
rows = []; skipped_quiet = 0; skipped_layout = 0
for t in range(60, int(nfr * 1536 / FS) - 60, 20):
    f0 = t * FS // 1536; w0, w1 = f0 - HALF, f0 + HALF
    ls = set(lay[w0:w1 + 1])
    if len(ls) != 1 or next(iter(ls)) not in ((7, 1), (2, 0)): skipped_layout += 1; continue
    ch = 6 if (7, 1) in ls else 2
    x = decode(a[ao[w0]:ao[w1 + 1]], ch, "ac3")
    c = x[:, 2] if ch == 6 else x[:, 0]
    i0 = HALF * 1536; i1 = i0 + HOLE; base = w0 * 1536
    lvl = db(np.mean(np.r_[c[i0 - G - W:i0 - G], c[i1 + G:i1 + G + W]] ** 2))
    if lvl < -35: skipped_quiet += 1; continue
    row = [t, ch, lvl]
    for name, m, mo in don:
        g0 = base // 1152 - 30; g1 = (w1 + 1) * 1536 // 1152 + 30
        y = decode(m[mo[g0]:mo[g1]], 2, "mp2")
        d = y.sum(axis=1) / 2 if ch == 6 else y[:, 0]
        row += list(fit(c, d, base - g0 * 1152, i0, i1))
    rows.append(row)
with open(OUT, "w") as f:
    f.write("# t_s ch level_dBFS " + " ".join(f"{n}_lag {n}_gain {n}_cc {n}_snr" for n, _, _ in don) + "\n")
    for r in rows: f.write(" ".join(f"{v:.4f}" if isinstance(v, float) else str(v) for v in r) + "\n")
print(f"{AC3.split('/')[-1]}: {nfr} AC3-Frames, {len(rows)} Stellen gemessen "
      f"({sum(1 for r in rows if r[1] == 6)} in 5.1, {sum(1 for r in rows if r[1] == 2)} in Stereo), "
      f"{skipped_quiet} zu leise, {skipped_layout} mit Layoutwechsel/anderem Layout")
R = np.array(rows, dtype=float)
Q = [0, 5, 25, 50, 75, 95, 100]
for k, (name, _, _) in enumerate(don):
    lag, cc, snr = R[:, 3 + 4 * k], R[:, 5 + 4 * k], R[:, 6 + 4 * k]
    print(f"  {name}: Korrelation min/5/25/50/75/95/max % = " + " ".join(f"{v:.3f}" for v in np.percentile(cc, Q)))
    print(f"  {name}: Treffgenauigkeit dB      min/5/25/50/75/95/max % = " + " ".join(f"{v:.1f}" for v in np.percentile(snr, Q)))
    print(f"  {name}: Anteil Stellen mit Korrelation >= 0.90 / 0.95 / 0.97: "
          f"{np.mean(cc >= 0.90) * 100:.0f} % / {np.mean(cc >= 0.95) * 100:.0f} % / {np.mean(cc >= 0.97) * 100:.0f} %; "
          f"Versatz Median {np.median(lag) / 48:.1f} ms, bei Korrelation >= 0.95: {np.min(lag[cc >= 0.95]) / 48 if np.any(cc >= 0.95) else float('nan'):.1f} bis {np.max(lag[cc >= 0.95]) / 48 if np.any(cc >= 0.95) else float('nan'):.1f} ms")
