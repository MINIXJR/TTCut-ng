# Sound against picture along a whole recording, from timestamps alone:
# every audio ES frame is mapped onto the original packet with the same
# payload; its place on the video timeline is the display rank of the picture
# at that PTS (video ES = all pictures in display order, frame 0 = first packet).
# usage: zoneaudit.py FPS OUTDIR BASENAME SEGMENT.ts [SEGMENT.ts ...]
import subprocess, sys, hashlib, bisect, collections, pickle, os
fps = float(sys.argv[1]); OUT, BASE = sys.argv[2:4]; segs = sys.argv[4:]; dur = 1.0 / fps
PKARG = segs.pop(0) if segs and segs[0].endswith(".pickle") else None
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); from eswalk import walk as _walk
def walk(d, fmt):
    fr = _walk(d, fmt); return [p for p, _ in fr] + [fr[-1][0] + fr[-1][1]]
cache = PKARG or f"{OUT}/../packets.pickle"
if os.path.exists(cache): vp, ap, names = pickle.load(open(cache, "rb"))
else:
    vp = []; ap = collections.defaultdict(list); names = {}
    for s in segs:
        for l in subprocess.run(["ffprobe", "-v", "error", "-show_entries", "stream=index,codec_type,codec_name", "-of", "csv=p=0", s], stdout=subprocess.PIPE).stdout.decode().splitlines():
            f = l.split(",")
            if len(f) >= 3: names[int(f[0])] = (f[2], f[1]) if f[1] in ("video", "audio") else (f[1], f[2])
        pr = subprocess.Popen(["ffprobe", "-v", "error", "-show_packets", "-show_data_hash", "md5", "-show_entries", "packet=stream_index,pts_time,data_hash", "-of", "default=nw=1", s], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        cur = {}
        for l in pr.stdout:
            k, _, v = l.rstrip("\n").partition("="); cur[k] = v
            if k == "data_hash":
                if cur.get("pts_time", "N/A") != "N/A":
                    i = int(cur["stream_index"]); p = float(cur["pts_time"])
                    if i == 0: vp.append(p)
                    else: ap[i].append((v.split(":")[-1].lower(), p))
                cur = {}
        pr.wait()
    pickle.dump((vp, dict(ap), names), open(cache, "wb"))
def runs(pts, back=0.5, sustain=10):
    r = [[]]
    for i, p in enumerate(pts):
        if len(r[-1]) >= 12:
            ref = sorted(r[-1][-12:])[-3]
            if p < ref - back and all(q < ref - back for q in pts[i:i + sustain]): r.append([])
        r[-1].append(p)
    return r
vruns = runs(vp)
h26x = any(c in ("h264", "hevc") for v in names.values() for c in v)
vs = []; base = []; n = 0
for r in vruns:
    s = sorted(set(r))
    if h26x and not vs: s = [p for p in s if p >= r[0] - 1e-6]
    vs.append(s); base.append(n); n += len(s)
holes = [(s[i], round((s[i+1]-s[i])/dur) - 1) for s in vs for i in range(len(s)-1) if round((s[i+1]-s[i])/dur) != 1]
print(f"Bild: {len(vp)} Pakete, {len(vruns)} Lauf/Läufe, {n} Bilder, {len(holes)} Löcher = {sum(h[1] for h in holes)*dur*1000:.0f} ms")
def vtime(p, r=None):
    if r is None: r = next((j for j in range(len(vs)) if vs[j][0] - 1 <= p <= vs[j][-1] + 1), 0)
    s = vs[r]; i = bisect.bisect_right(s, p + 1e-6) - 1
    if i < 0: return p - s[0]
    return (base[r] + i) * dur + min(p - s[i], dur)
info = dict(l.strip().split("=", 1) for l in open(f"{OUT}/{BASE}.info", encoding="utf-8") if "=" in l and not l.startswith("#"))
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
    runs = []; unm = 0
    for k, h in enumerate(hs):
        ps = m.get(h)
        if not ps or len(ps) != 1: unm += 1; continue
        err = (k * fd - vtime(ps[0])) * 1000
        if runs and abs(err - runs[-1][0]) < 1.0: runs[-1][2] = k * fd; runs[-1][3] += 1
        else: runs.append([err, k * fd, k * fd, 1, unm])
        unm = 0
    big = [r for r in runs if r[3] >= 20]
    errs = [r[0] for r in big]
    print(f"{f}: {len(hs)} Frames, {len(big)} Abschnitte; Ton gegen Bild min {min(errs):+.0f}, max {max(errs):+.0f} ms")
    print("   Verlauf: " + " | ".join(f"{r[0]:+.0f} ms ab {r[1]:.0f} s" for r in big[:40]))
