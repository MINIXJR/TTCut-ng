# Are the audio tracks of one demux run in sync with EACH OTHER? No video
# model: ES frame k of every track -> original packet PTS (payload MD5);
# at the same ES time all tracks must carry sound of the same original time.
# usage: tracksync.py OUTDIR BASENAME PICKLE
import sys, hashlib, pickle, collections
OUT, BASE, PK = sys.argv[1:4]
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
info = dict(l.strip().split("=", 1) for l in open(f"{OUT}/{BASE}.info", encoding="utf-8") if "=" in l and not l.startswith("#"))
tracks = []
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
    # ES time -> original PTS, only unique payloads
    pts = {}
    for k, h in enumerate(hs):
        ps = m.get(h)
        if ps and len(ps) == 1: pts[round(k * fd, 3)] = ps[0]
    tracks.append((f, fd, pts))
    print(f"{f}: {len(hs)} Frames, {len(pts)} eindeutig zugeordnet, Original-Spur {best[2]}")
ref = tracks[0]
for f, fd, pts in tracks[1:]:
    runs = []
    for t in sorted(ref[2]):
        # nearest ES time of the other track within half a frame
        cand = [round(t + d, 3) for d in (0, -0.008, 0.008, -0.016, 0.016, -0.024, 0.024)]
        q = next((c for c in cand if c in pts), None)
        if q is None: continue
        diff = round((pts[q] - q) - (ref[2][t] - t), 2)   # original offset of other minus ref
        if runs and abs(diff - runs[-1][0]) < 0.05: runs[-1][2] = t; runs[-1][3] += 1
        else: runs.append([diff, t, t, 1])
    big = [r for r in runs if r[3] >= 20]
    print(f"{f} gegen {ref[0]}: " + " | ".join(f"{r[0]*1000:+.0f} ms ab {r[1]:.0f} s" for r in big[:30]))
