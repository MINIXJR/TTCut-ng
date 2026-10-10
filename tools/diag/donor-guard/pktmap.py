# Map the packets of a remuxed TS back to the frames of the original TS by
# their bytes and compare the timestamps. Also: where an extracted ES starts.
# usage: pktmap.py ORIGINAL_TS REMUXED_TS STREAM_INDEX FMT [ES]
import subprocess, sys
ORIG, REP, IDX, FMT = sys.argv[1:5]; ES = sys.argv[5] if len(sys.argv) > 5 else None
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
def frames(ts, tag):
    run("ffmpeg", "-v", "error", "-y", "-t", "4", "-copyts", "-i", ts, "-map", f"0:{IDX}", "-c", "copy", "-f", "nut", f"{tag}.nut")
    pts = [float(l.split(",")[0]) for l in run("ffprobe", "-v", "error", "-show_entries", "packet=pts_time", "-of", "csv=p=0", f"{tag}.nut").decode().split()]
    run("ffmpeg", "-v", "error", "-y", "-i", f"{tag}.nut", "-c", "copy", "-f", FMT, f"{tag}.es")
    d = open(f"{tag}.es", "rb").read(); o = walk(d); assert len(o) - 1 == len(pts), (tag, len(o) - 1, len(pts))
    return pts, [d[o[k]:o[k + 1]] for k in range(len(pts))]
op, of = frames(ORIG, "o"); rp, rf = frames(REP, "r")
index = {}
for j, f in enumerate(of): index.setdefault(f, []).append(j)
print(f"  Original: {len(op)} Frames ab PTS {op[0]:.6f}; remuxt: {len(rp)} Pakete ab PTS {rp[0]:.6f}")
last = None
for k, f in enumerate(rf[:60]):
    h = index.get(f, [])
    if len(h) != 1: print(f"   remuxtes Paket {k} (PTS {rp[k]:.6f}): {len(h)} Treffer im Original"); continue
    diff = op[h[0]] - rp[k]
    if last is None or abs(diff - last) > 1e-6:
        print(f"   ab remuxtem Paket {k} (PTS {rp[k]:.6f}) = Original-Frame {h[0]} (PTS {op[h[0]]:.6f}): Original minus remuxt = {diff:.6f} s")
    last = diff
if ES:
    e = open(ES, "rb").read(); eo = walk(e); f0 = e[eo[0]:eo[1]]
    k = [i for i, f in enumerate(rf) if f == f0]
    print(f"   ES-Frame 0 = remuxtes Paket {k} mit PTS {[f'{rp[i]:.6f}' for i in k]}")
