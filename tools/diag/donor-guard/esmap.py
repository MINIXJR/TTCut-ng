# Where does a frame of the ORIGINAL transport stream sit in the demuxed ES?
# Cut a short clip of one audio stream (stream copy), take its 10th frame with
# its PTS, find the same bytes in the ES and count the frames before it.
# Prints ES time minus (PTS - first video PTS): 0 = the ES frame sits where
# its original timestamp says; the same value at every spot = constant shift.
# usage: esmap.py TS FIRST_VIDEO_PTS NAME:INDEX:FMT:ES [...] -- offset_s [...]
import subprocess, sys, os, bisect
args = sys.argv[1:]; cut = args.index("--")
TS, V0 = args[0], float(args[1]); TRACKS = [a.split(":", 3) for a in args[2:cut]]; OFFS = args[cut + 1:]
KB = [32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640]
BR = [0,32,48,56,64,80,96,112,128,160,192,224,256,320,384]
def walk(d, fmt):
    o = []; p = 0
    while p + 7 < len(d):
        o.append(p)
        if fmt == "ac3": p += KB[(d[p+4] & 0x3F) >> 1] * 4
        else:
            h = int.from_bytes(d[p:p+4], "big"); p += 144 * BR[(h >> 12) & 15] * 1000 // 48000 + ((h >> 9) & 1)
    o.append(p); return o
def run(*a): return subprocess.run(a, stdout=subprocess.PIPE, check=True, stderr=subprocess.DEVNULL).stdout
for name, idx, fmt, es in TRACKS:
    d = open(es, "rb").read(); offs = walk(d, fmt); dur = 0.032 if fmt == "ac3" else 0.024
    print(f" {name} ({fmt}, {len(offs) - 1} Frames im ES):")
    for off in OFFS:
        run("ffmpeg", "-v", "error", "-y", "-ss", off, "-t", "2", "-copyts", "-i", TS, "-map", f"0:{idx}", "-c", "copy", "-f", "nut", "clip.nut")
        pts = [float(l.split(",")[0]) for l in run("ffprobe", "-v", "error", "-show_entries", "packet=pts_time", "-of", "csv=p=0", "clip.nut").decode().split()]
        run("ffmpeg", "-v", "error", "-y", "-i", "clip.nut", "-c", "copy", "-f", fmt, "clip.es")
        c = open("clip.es", "rb").read(); co = walk(c, fmt)
        assert len(co) - 1 == len(pts), (len(co) - 1, len(pts))
        frame = c[co[10]:co[11]]
        hit = d.find(frame); again = d.find(frame, hit + 1) if hit >= 0 else -1
        if hit < 0: print(f"   ab {off:>5} s: Frame mit PTS {pts[10]:.6f} im ES NICHT gefunden"); continue
        k = bisect.bisect_left(offs, hit); assert offs[k] == hit, "hit not on a frame boundary"
        print(f"   ab {off:>5} s: PTS {pts[10]:.6f} -> ES-Frame {k} = {k * dur:9.3f} s; ES-Zeit minus (PTS - Bildanfang) = {(k * dur - (pts[10] - V0)) * 1000:+7.1f} ms"
              + ("" if again < 0 else "  (Frame kommt mehrfach vor!)"))
for f in ("clip.nut", "clip.es"):
    if os.path.exists(f): os.remove(f)
