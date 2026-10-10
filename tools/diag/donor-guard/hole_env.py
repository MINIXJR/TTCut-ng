# Measurement: per-channel envelope around a hole marker (decoded AC3), in
# 1 ms steps, to see how hole start and end can be told per channel.
# usage: hole_env.py AC3 t_s [t_s ...]
import subprocess, sys, numpy as np
FS = 48000; MARGIN = 20
KB = [32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640]
def walk_ac3(d):
    o = []; p = 0
    while p + 7 < len(d):
        assert d[p] == 0x0B and d[p+1] == 0x77, f"no AC3 sync at {p}"
        o.append(p); p += KB[(d[p+4] & 0x3F) >> 1] * 4
    o.append(p); return o
def channels(b6):
    acmod = b6 >> 5
    n = [2,1,2,3,3,4,4,5][acmod]
    bits = 3 + (2 if (acmod & 1 and acmod != 1) else 0) + (2 if acmod & 4 else 0) + (2 if acmod == 2 else 0)
    return acmod, n + ((b6 >> (7 - bits)) & 1)
def decode(b, ch):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-drc_scale", "0", "-f", "ac3", "-i", "pipe:0", "-f", "f32le", "-"],
                         input=b, stdout=subprocess.PIPE, check=True).stdout
    return np.frombuffer(raw, dtype="<f4").reshape(-1, ch).astype(np.float64)
def db(a): return 20 * np.log10(a) if a > 1e-9 else -180.0
a = open(sys.argv[1], "rb").read(); ao = walk_ac3(a)
for t in sys.argv[2:]:
    f = int(round(float(t) * FS / 1536))
    w0, w1 = f - MARGIN, f + MARGIN
    modes = {channels(a[ao[k] + 6]) for k in range(w0, w1 + 1)}
    assert len(modes) == 1, f"mode change near {t}: {modes}"
    acmod, ch = modes.pop()
    x = decode(a[ao[w0]:ao[w1 + 1]], ch)
    i = MARGIN * 1536
    print(f"t={t} frame {f} acmod {acmod} channels {ch}; ms relative to the start of frame {f}")
    print("   ms " + " ".join(f"ch{c:<3d}" for c in range(ch)))
    for ms in range(-45, 75):
        s = i + ms * 48
        print(f"{ms:5d} " + " ".join(f"{db(float(np.max(np.abs(x[s:s+48, c])))):5.0f}" for c in range(ch)))
