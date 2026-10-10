# Reference measurement: the hole rule, donor fit (+/-50 ms) and fill on the
# generated fixture, to take thresholds for the gate from a measurement.
# usage: fixture_check.py HOLE.ac3 CLEAN.ac3 CH DONOR FMT frame [frame ...]
import subprocess, sys, numpy as np
FS = 48000; W = 7200; G = 240; S = 2400; BLK = 256; XF = 96
def decode(path, ch, fmt):
    pre = ["-drc_scale", "0", "-f", "ac3"] if fmt == "ac3" else ["-f", "mp3"]
    raw = subprocess.run(["ffmpeg", "-v", "error", *pre, "-i", path, "-f", "f32le", "-"], stdout=subprocess.PIPE, check=True).stdout
    return np.frombuffer(raw, dtype="<f4").reshape(-1, ch).astype(np.float64)
def db(p): return 10 * np.log10(p) if p > 1e-18 else -180.0
def locate(c, lo, hi, T=30):
    best = (-1e9, -1)
    for b in range(((lo + BLK - 1) // BLK) * BLK, hi, BLK):
        drop = db(float(np.mean(c[b - 3 * BLK:b] ** 2))) - db(float(np.mean(c[b:b + 2 * BLK] ** 2)))
        if drop > best[0]: best = (drop, b)
    b = best[1]; thr = np.sqrt(float(np.mean(c[b - 4 * BLK:b - BLK] ** 2))) * 10 ** (-T / 20)
    q = np.nonzero(np.abs(c[lo:hi]) <= thr)[0]
    runs = np.split(q, np.nonzero(np.diff(q) > 1)[0] + 1); run = max(runs, key=len)
    return best[0], lo + int(run[0]), lo + int(run[-1]) + 1
def fit(pairs, h0, h1):
    blocks = [(h0 - G - W, h0 - G), (h1 + G, h1 + G + W)]
    n = 2 * S + 1; num = np.zeros(n); dd = np.zeros(n); cc0 = 0.0; per = []
    for c, d in pairs:
        cross = np.zeros(n); e = np.zeros(n); ec = 0.0
        for a, b in blocks:
            seg = d[a - S:b + S]
            cross += np.correlate(seg, c[a:b], "valid")
            cs = np.concatenate(([0.0], np.cumsum(seg ** 2))); e += cs[b - a:] - cs[:len(cs) - (b - a)]
            ec += float(np.dot(c[a:b], c[a:b]))
        num += cross; dd += e; cc0 += ec; per.append((cross, e))
    cc = num / (np.sqrt(dd * cc0) + 1e-30); k = int(np.argmax(cc))
    return k - S, float(cc[k]), [cr[k] / (e[k] + 1e-30) for cr, e in per]
def ramp(n): return 0.5 - 0.5 * np.cos(np.pi * (np.arange(n) + 0.5) / n)
hole_f, clean_f, ch, donor_f, fmt = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5]
x = decode(hole_f, ch, "ac3"); ref = decode(clean_f, ch, "ac3"); y = decode(donor_f, 2, fmt)
tg = [(2, y.sum(axis=1) / 2)] if ch == 6 else [(0, y[:, 0]), (1, y[:, 1])]
for f in map(int, sys.argv[6:]):
    lo, hi = (f - 2) * 1536, (f + 3) * 1536
    found = [locate(x[:, c], lo, hi) for c, _ in tg]
    h0 = min(r[1] for r in found); h1 = max(r[2] for r in found)
    lag, cc, gains = fit([(x[:, c], d) for c, d in tg], h0, h1)
    acc = []; out = []
    r = np.arange(h0 - XF, h1 + XF); w = np.ones(len(r)); w[:XF] = ramp(XF); w[-XF:] = ramp(XF)[::-1]
    for (c, d), g in zip(tg, gains):
        fill = (1 - w) * x[r, c] + w * g * d[r + lag]
        t = ref[h0:h1, c]
        acc.append(db(float(np.mean(t ** 2))) - db(float(np.mean((t - fill[XF:-XF]) ** 2))))
        out.append(db(float(np.mean(x[h0:h1, c] ** 2))) - db(float(np.mean(t ** 2))))
    print(f"frame {f}: drops {[round(r[0], 1) for r in found]} dB; hole {h0}..{h1} ({h1 - h0} samples, per channel {[(r[1], r[2]) for r in found]}); "
          f"frames touched {(h0 - XF) // 1536}..{(h1 + XF - 1) // 1536}; end is {((h1 // 1536) + 1) * 1536 - h1} before the next frame; "
          f"shift {lag}, match {cc:.3f}, gains {[round(g, 3) for g in gains]}; hole level vs clean {[round(o, 1) for o in out]} dB; "
          f"fill closer to clean than silence by {[round(a, 1) for a in acc]} dB")
