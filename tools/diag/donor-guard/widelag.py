# Setup check: offset between the AC3 centre and the MP2 mid of a recording,
# searched over +/-20 s at a few loud spots (FFT cross-correlation, 4 s of AC3).
# usage: widelag.py AC3 MP2 t_s [t_s ...]
import subprocess, sys, numpy as np
AC3, MP2 = sys.argv[1], sys.argv[2]; TS = [int(v) for v in sys.argv[3:]]
FS = 48000
KB = [32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640]
BR = [0,32,48,56,64,80,96,112,128,160,192,224,256,320,384]
def walk_ac3(d):
    o = []; p = 0
    while p + 7 < len(d): o.append(p); p += KB[(d[p+4] & 0x3F) >> 1] * 4
    o.append(p); return o
def walk_mp2(d):
    o = []; p = 0
    while p + 4 <= len(d):
        h = int.from_bytes(d[p:p+4], "big"); o.append(p); p += 144 * BR[(h >> 12) & 15] * 1000 // FS + ((h >> 9) & 1)
    o.append(p); return o
def decode(b, ch, fmt):
    pre = ["-drc_scale", "0", "-f", "ac3"] if fmt == "ac3" else ["-f", "mp3"]
    raw = subprocess.run(["ffmpeg", "-v", "error", *pre, "-i", "pipe:0", "-f", "f32le", "-"], input=b, stdout=subprocess.PIPE, check=True).stdout
    return np.frombuffer(raw, dtype="<f4").reshape(-1, ch).astype(np.float64)
a = open(AC3, "rb").read(); m = open(MP2, "rb").read(); ao, mo = walk_ac3(a), walk_mp2(m)
print(f"AC3 {len(ao)-1} Frames = {(len(ao)-1)*0.032:.3f} s; MP2 {len(mo)-1} Frames = {(len(mo)-1)*0.024:.3f} s")
for t in TS:
    f0 = t * FS // 1536; fa, fe = f0 - 62, f0 + 63                      # 4 s of AC3
    ch = 6 if (a[ao[f0] + 6] >> 5) == 7 else 2
    x = decode(a[ao[fa]:ao[fe]], ch, "ac3"); c = x[:, 2] if ch == 6 else x.sum(axis=1) / 2
    base = fa * 1536
    g0 = base // 1152 - 834; g1 = fe * 1536 // 1152 + 834              # +/-20 s of MP2
    y = decode(m[mo[g0]:mo[g1]], 2, "mp2").sum(axis=1) / 2
    nominal = base - g0 * 1152
    n = 1 << int(np.ceil(np.log2(len(y) + len(c))))
    cross = np.fft.irfft(np.fft.rfft(y, n) * np.conj(np.fft.rfft(c, n)), n)[:len(y) - len(c) + 1]
    cs = np.concatenate(([0.0], np.cumsum(y ** 2))); e = cs[len(c):] - cs[:len(cs) - len(c)]
    cc = cross / (np.sqrt(e * np.dot(c, c)) + 1e-30)
    k = int(np.argmax(cc)); o = np.argsort(cc)[::-1]
    second = next(float(cc[j]) for j in o if abs(j - k) > 480)
    print(f"  t={t} s ({ch} Kanaele): bester Versatz {(k - nominal) / 48:+.1f} ms, Korrelation {cc[k]:.3f}; zweitbester (>10 ms entfernt) {second:.3f}")
