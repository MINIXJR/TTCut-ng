# Which original frame is frame 0 (and frame 30) of an extracted ES?
# usage: where0.py ORIGINAL_TS STREAM_INDEX FMT ES FIRST_VIDEO_PTS
import subprocess, sys
TS, IDX, FMT, ES, V0 = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], float(sys.argv[5])
KB = [32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640]
BR = [0,32,48,56,64,80,96,112,128,160,192,224,256,320,384]
def walk(d):
    o = []; p = 0
    while p + 7 < len(d):
        o.append(p)
        if FMT == "ac3": p += KB[(d[p+4] & 0x3F) >> 1] * 4
        else:
            h = int.from_bytes(d[p:p+4], "big"); p += 144 * BR[(h >> 12) & 15] * 1000 // 48000 + ((h >> 9) & 1)
    o.append(p); return o
def run(*a): return subprocess.run(a, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=True).stdout
run("ffmpeg", "-v", "error", "-y", "-t", "4", "-copyts", "-i", TS, "-map", f"0:{IDX}", "-c", "copy", "-f", "nut", "o.nut")
pts = [float(l.split(",")[0]) for l in run("ffprobe", "-v", "error", "-show_entries", "packet=pts_time", "-of", "csv=p=0", "o.nut").decode().split()]
run("ffmpeg", "-v", "error", "-y", "-i", "o.nut", "-c", "copy", "-f", FMT, "o.es")
o = open("o.es", "rb").read(); oo = walk(o); e = open(ES, "rb").read(); eo = walk(e)
assert len(oo) - 1 == len(pts)
dur = 0.032 if FMT == "ac3" else 0.024
for k in (0, 30):
    fr = e[eo[k]:eo[k + 1]]
    hits = [j for j in range(len(pts)) if o[oo[j]:oo[j + 1]] == fr]
    if len(hits) != 1: print(f"   ES-Frame {k}: {len(hits)} Treffer im Original"); continue
    j = hits[0]
    print(f"   ES-Frame {k} = Original-Frame {j}, PTS {pts[j]:.6f} = Bildanfang {(pts[j] - V0) * 1000:+.1f} ms; ES-Zeit minus (PTS - Bildanfang) = {(k * dur - (pts[j] - V0)) * 1000:+.1f} ms")
