# Spot check: at fixed ES times, the original PTS each audio track carries and
# the original PTS of the picture TTCut shows there (video ES frame = rank in
# stream order).  usage: spot.py OUTDIR BASENAME PICKLE T1 T2 ...
import sys, hashlib, pickle, collections
OUT, BASE, PK = sys.argv[1:4]; times = [float(x) for x in sys.argv[4:]]
vp, ap, names = pickle.load(open(PK, "rb"))
KB = [32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640]
BR = [0,32,48,56,64,80,96,112,128,160,192,224,256,320,384]
def walk(d, fmt):
    o = []; p = 0
    while p + 7 < len(d):
        if fmt == "ac3": step = KB[(d[p+4] & 0x3F) >> 1] * 4
        else:
            h = int.from_bytes(d[p:p+4], "big"); step = 144 * BR[(h >> 12) & 15] * 1000 // 48000 + ((h >> 9) & 1)
        if step <= 0: break
        o.append(p); p += step
    o.append(p); return o
# video: stream order, display rank = index after sorting within reorder window is
# too fine here; use plain stream order rank (differs from display by <= 3 pictures)
print(f"Bild: {len(vp)} Pakete, erstes {vp[0]:.3f}, letztes {vp[-1]:.3f} (ES-Zeit des letzten: {(len(vp)-1)/50:.1f} s)")
info = dict(l.strip().split("=", 1) for l in open(f"{OUT}/{BASE}.info", encoding="utf-8") if "=" in l and not l.startswith("#"))
rows = {t: {} for t in times}; last = {}
a = -1
while f"audio_{a + 1}_file" in info:
    a += 1
    f = info[f"audio_{a}_file"]; fmt = info[f"audio_{a}_codec"]; fd = 0.032 if fmt == "ac3" else 0.024
    e = open(f"{OUT}/{f}", "rb").read(); eo = walk(e, fmt)
    hs = [hashlib.md5(e[eo[k]:eo[k+1]]).hexdigest() for k in range(len(eo) - 1)]
    best = None
    for i, pk in ap.items():
        m = collections.defaultdict(list)
        for h, p in pk: m[h].append(p)
        n = sum(1 for h in hs[::50] if h in m)
        if best is None or n > best[0]: best = (n, m, i)
    m = best[1]
    for t in times:
        k0 = int(t / fd)
        for k in range(k0, k0 + 400):
            ps = m.get(hs[k]) if k < len(hs) else None
            if ps and len(ps) == 1: rows[t][f] = (k * fd, ps[0]); break
    for k in range(len(hs) - 1, 0, -1):
        ps = m.get(hs[k])
        if ps and len(ps) == 1: last[f] = (k * fd, ps[0], len(hs) * fd); break
tr = list(last)
print("ES-Zeit | Bild-PTS dort | " + " | ".join(f"{f}: PTS (Ton minus Bild)" for f in tr))
for t in times:
    vi = int(t * 50); vpts = vp[vi] if vi < len(vp) else float("nan")
    print(f"{t:7.1f} | {vpts:9.3f} | " + " | ".join(f"{rows[t][f][1]:9.3f} ({rows[t][f][1]-vpts:+7.3f})" if f in rows[t] else "   -" for f in tr))
print("letzter echter Tonframe je Spur: " + " | ".join(f"{f}: ES {v[0]:.1f} s trägt PTS {v[1]:.3f}, Datei endet {v[2]:.1f} s" for f, v in last.items()))
