# Where does each audio ES of a demux run start, measured on the original's
# timestamps? usage: es_start.py ORIGINAL_HEAD_TS OUTDIR BASENAME
import subprocess, sys, json, os, re
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
V0 = float(info["first_video_pts"])
streams = json.loads(run("ffprobe", "-v", "error", "-show_entries", "stream=index,codec_type,codec_name", "-of", "json", TS))["streams"]
orig = {}
for s in streams:
    if s.get("codec_type") != "audio" or s.get("codec_name") not in ("ac3", "mp2"): continue
    i, fmt = s["index"], s["codec_name"]
    run("ffmpeg", "-v", "error", "-y", "-t", "6", "-copyts", "-i", TS, "-map", f"0:{i}", "-c", "copy", "-f", "nut", "o.nut")
    pts = [float(l.split(",")[0]) for l in run("ffprobe", "-v", "error", "-show_entries", "packet=pts_time", "-of", "csv=p=0", "o.nut").decode().split()]
    run("ffmpeg", "-v", "error", "-y", "-i", "o.nut", "-c", "copy", "-f", fmt, "o.es")
    d = open("o.es", "rb").read(); o = walk(d, fmt)
    n = min(len(o) - 1, len(pts))
    orig[i] = (fmt, {d[o[k]:o[k+1]]: pts[k] for k in range(n)})
res = []
a = -1
while f"audio_{a + 1}_file" in info:
    a += 1
    f = info[f"audio_{a}_file"]; fmt = info[f"audio_{a}_codec"]
    if fmt not in ("ac3", "mp2"): res.append(f"{f}: Codec {fmt} nicht geprüft"); continue
    e = open(f"{OUT}/{f}", "rb").read(); eo = walk(e, fmt); dur = 0.032 if fmt == "ac3" else 0.024
    out = []
    for k in (0, 40):
        fr = e[eo[k]:eo[k+1]]
        hit = [(i, m[fr]) for i, (ff, m) in orig.items() if ff == fmt and fr in m]
        out.append(f"{(k*dur - (hit[0][1]-V0))*1000:+.1f}" if len(hit) == 1 else f"({len(hit)} Treffer)")
    tag = re.sub(r"^.*_", "", f)
    res.append(f"{tag} {out[0]}/{out[1]}")
print("   " + "  ".join(res))
for x in ("o.nut", "o.es"):
    if os.path.exists(x): os.remove(x)
