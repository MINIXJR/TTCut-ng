# Per audio track: where does the ES deviate from the original's timeline?
# Maps every ES frame onto the original packet with the same payload and prints
# each place where (ES time - original PTS) changes, with the frames in between.
# usage: gapaudit.py ORIGINAL_TS OUTDIR BASENAME
import subprocess, sys, re
TS, OUT, BASE = sys.argv[1:4]
KB = [32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640]
BR = [0,32,48,56,64,80,96,112,128,160,192,224,256,320,384]
def walk(d, fmt):
    o = []; p = 0
    while p + 7 < len(d):
        o.append(p)
        if fmt == "ac3": step = KB[(d[p+4] & 0x3F) >> 1] * 4
        else:
            h = int.from_bytes(d[p:p+4], "big"); step = 144 * BR[(h >> 12) & 15] * 1000 // 48000 + ((h >> 9) & 1)
        if step <= 0: break
        p += step
    o.append(p); return o
def run(*a): return subprocess.run(a, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout
info = dict(l.strip().split("=", 1) for l in open(f"{OUT}/{BASE}.info", encoding="utf-8") if "=" in l and not l.startswith("#"))
import json
import hashlib, collections
names = {int(l.split(",")[0]): l.strip().split(",")[1] for l in run("ffprobe", "-v", "error", "-select_streams", "a", "-show_entries", "stream=index,codec_name", "-of", "csv=p=0", TS).decode().splitlines() if l}
raw = collections.defaultdict(list); cur = {}
for l in run("ffprobe", "-v", "error", "-select_streams", "a", "-show_packets", "-show_data_hash", "md5", "-show_entries", "packet=stream_index,pts_time,data_hash", "-of", "default=nw=1", TS).decode().splitlines():
    k, _, v = l.partition("="); cur[k] = v
    if k == "data_hash":
        if cur.get("pts_time", "N/A") != "N/A": raw[int(cur["stream_index"])].append((v.split(":")[-1].lower(), float(cur["pts_time"])))
        cur = {}
orig = {}
for i, pk in raw.items():
    fmt = names.get(i)
    if fmt not in ("ac3", "mp2"): continue
    m = {}
    for h, p in pk: m.setdefault(h, []).append(p)
    pts = [p for _, p in pk]
    holes = [(pts[k], pts[k+1]) for k in range(len(pts) - 1) if pts[k+1] - pts[k] > (0.040 if fmt == "ac3" else 0.030)]
    orig[i] = (fmt, m, holes)
a = -1
while f"audio_{a + 1}_file" in info:
    a += 1
    f = info[f"audio_{a}_file"]; fmt = info[f"audio_{a}_codec"]; fd = 0.032 if fmt == "ac3" else 0.024
    e = open(f"{OUT}/{f}", "rb").read(); eo = walk(e, fmt)
    best = max(((i, sum(1 for k in range(0, len(eo) - 1, 25) if hashlib.md5(e[eo[k]:eo[k+1]]).hexdigest() in m)) for i, (ff, m, h) in orig.items() if ff == fmt), key=lambda x: x[1])[0]
    m, holes = orig[best][1], orig[best][2]
    print(f"{re.sub(r'^.*_', '', f)}: Loecher im Original " + ", ".join(f"{(q-p-fd)*1000:.0f} ms nach PTS {p:.3f}" for p, q in holes))
    last = None; unm = 0
    for k in range(len(eo) - 1):
        h = m.get(hashlib.md5(e[eo[k]:eo[k+1]]).hexdigest())
        if not h or len(h) != 1: unm += 1; continue
        off = round((k * fd - h[0]) * 1000, 1)
        if last is not None and abs(off - last[0]) > 0.5:
            print(f"   ES-Frame {k} ({k*fd:.3f} s): ES-Zeit minus PTS aendert sich um {off-last[0]:+.1f} ms; davor {unm} Frames ohne Gegenstueck = {unm*fd*1000:.0f} ms; Original-Sprung {(h[0]-last[1]-fd)*1000:.0f} ms")
        last = (off, h[0]); unm = 0
