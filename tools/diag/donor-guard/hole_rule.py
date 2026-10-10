# Measurement: candidate rule for locating a hole in the marker's frames, and
# the donor fit on it. Hole = longest run of samples at or below
# (RMS of the 16 ms before the scan's boundary block) - T dB, per channel.
# usage: hole_rule.py AC3 MP2 t_s [t_s ...]
import subprocess, sys, numpy as np
FS = 48000; MARGIN = 94; W = 7200; G = 240; MAXLAG = 9600; BLK = 256
KB = [32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640]
BR = [0,32,48,56,64,80,96,112,128,160,192,224,256,320,384]
def walk_ac3(d):
    o = []; p = 0
    while p + 7 < len(d):
        assert d[p] == 0x0B and d[p+1] == 0x77
        o.append(p); p += KB[(d[p+4] & 0x3F) >> 1] * 4
    o.append(p); return o
def walk_mp2(d):
    o = []; p = 0
    while p + 4 <= len(d):
        h = int.from_bytes(d[p:p+4], "big"); assert (h >> 21) == 0x7FF
        o.append(p); p += 144 * BR[(h >> 12) & 15] * 1000 // FS + ((h >> 9) & 1)
    o.append(p); return o
def decode(b, ch, fmt):
    pre = ["-drc_scale", "0", "-f", "ac3"] if fmt == "ac3" else ["-f", "mp3"]
    raw = subprocess.run(["ffmpeg", "-v", "error", *pre, "-i", "pipe:0", "-f", "f32le", "-"],
                         input=b, stdout=subprocess.PIPE, check=True).stdout
    return np.frombuffer(raw, dtype="<f4").reshape(-1, ch).astype(np.float64)
def db(p): return 10 * np.log10(p) if p > 1e-18 else -180.0
def fit(pairs, nominal, i0, i1):
    """one lag for all (target, donor) pairs; gain and correlation per pair"""
    blocks = [(i0 - G - W, i0 - G), (i1 + G, i1 + G + W)]
    n = 2 * MAXLAG + 1; num = np.zeros(n); den_d = np.zeros(n); den_c = 0.0; per = []
    for c, d in pairs:
        cross = np.zeros(n); e = np.zeros(n); ec = 0.0
        for a, b in blocks:
            seg = d[a + nominal - MAXLAG:b + nominal + MAXLAG]
            cross += np.correlate(seg, c[a:b], "valid")
            cs = np.concatenate(([0.0], np.cumsum(seg ** 2)))
            e += cs[b - a:] - cs[:len(cs) - (b - a)]; ec += float(np.dot(c[a:b], c[a:b]))
        num += cross; den_d += e; den_c += ec; per.append((cross, e, ec))
    cc = num / (np.sqrt(den_d * den_c) + 1e-30); k = int(np.argmax(cc))
    return k - MAXLAG, float(cc[k]), [(cr[k] / (e[k] + 1e-30), cr[k] / (np.sqrt(e[k] * ec) + 1e-30)) for cr, e, ec in per]
def locate(c, lo, hi, T):
    """boundary block with the largest drop (3 blocks before vs 2 after), then the longest quiet run"""
    best = (-1e9, -1)
    for b in range(((lo + BLK - 1) // BLK) * BLK, hi, BLK):
        drop = db(float(np.mean(c[b - 3 * BLK:b] ** 2))) - db(float(np.mean(c[b:b + 2 * BLK] ** 2)))
        if drop > best[0]: best = (drop, b)
    b = best[1]; thr = np.sqrt(float(np.mean(c[b - 4 * BLK:b - BLK] ** 2))) * 10 ** (-T / 20)
    q = np.nonzero(np.abs(c[lo:hi]) <= thr)[0]
    runs = np.split(q, np.nonzero(np.diff(q) > 1)[0] + 1); run = max(runs, key=len)
    return best[0], lo + int(run[0]), lo + int(run[-1]) + 1, sorted((len(r) for r in runs), reverse=True)[:3]
a = open(sys.argv[1], "rb").read(); m = open(sys.argv[2], "rb").read(); ao, mo = walk_ac3(a), walk_mp2(m)
for t in sys.argv[3:]:
    f = int(round(float(t) * FS / 1536)); w0, w1 = f - MARGIN, f + MARGIN
    acmod = a[ao[f] + 6] >> 5; ch = 6 if acmod == 7 else 2
    x = decode(a[ao[w0]:ao[w1 + 1]], ch, "ac3"); base = w0 * 1536
    g0 = base // 1152 - 30; g1 = (w1 + 1) * 1536 // 1152 + 30
    y = decode(m[mo[g0]:mo[g1]], 2, "mp2"); nominal = base - g0 * 1152
    lo = (MARGIN - 2) * 1536; hi = (MARGIN + 3) * 1536          # marker frames f-1..f+1 plus one each side
    print(f"t={t} frame {f} acmod {acmod}")
    targets = [(2, y.sum(axis=1) / 2)] if ch == 6 else [(0, y[:, 0]), (1, y[:, 1])]
    for T in (20, 30, 40):
        found = [locate(x[:, c], lo, hi, T) for c, _ in targets]
        h0 = min(r[1] for r in found); h1 = max(r[2] for r in found)
        lag, cc, per = fit([(x[:, c], d) for c, d in targets], nominal, h0, h1)
        print(f"  T={T} dB: " + "; ".join(f"ch{c}: drop {r[0]:.1f} dB, hole {base + r[1]}..{base + r[2]} ({(r[2] - r[1]) / 48:.2f} ms), longest runs {r[3]}"
                                        for (c, _), r in zip(targets, found)))
        print(f"          union {(h1 - h0) / 48:.2f} ms, starts {(h0 - MARGIN * 1536) / 48:+.2f} ms after frame start; "
              f"donor shift {lag} samples ({lag / 48:+.2f} ms), correlation {cc:.3f}; " + "; ".join(f"gain {g:.3f} (cc {k:.3f})" for g, k in per))
