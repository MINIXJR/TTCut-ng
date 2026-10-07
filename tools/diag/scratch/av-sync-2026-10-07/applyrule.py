# Build an audio ES from the original by a zonesim plan. The track is
# extracted by stream copy; ffprobe then lists its frames (pos, size, md5) and
# the plan's kept packets are found by md5, in order. A stand-in frame (copy
# of the previous one) goes where the plan says silence.
# usage: applyrule.py PLAN.json OUT.es INPUT_ARGS...
import sys, json, subprocess, os
plan = json.load(open(sys.argv[1])); out = sys.argv[2]; inargs = sys.argv[3:]
idx = int(os.path.basename(sys.argv[1]).split("_")[1].split(".")[0]); codec = plan["codec"]
raw = out + ".raw"
subprocess.run(["ffmpeg", "-v", "error", "-y", *inargs, "-map", f"0:{idx}", "-c:a", "copy", "-f", codec, raw], check=True, stdin=subprocess.DEVNULL)
import hashlib; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); from eswalk import walk
d = open(raw, "rb").read()
frames = [(pos, size, hashlib.md5(d[pos:pos + size]).hexdigest()) for pos, size in walk(d, codec)]
w = open(out, "wb"); prev = None; kept = sil = miss = 0; j = 0
for e in plan["plan"]:
    if e[0] == "keep":
        m = e[2]; j0 = j
        while j < len(frames) and frames[j][2] != m: j += 1
        if j >= len(frames):
            miss += 1; j = j0; w.write(prev if prev is not None else d[frames[0][0]:frames[0][0] + frames[0][1]]); continue
        pos, size, _ = frames[j]; prev = d[pos:pos + size]; w.write(prev); kept += 1; j += 1
    else:
        for _ in range(e[1]): w.write(prev if prev is not None else d[frames[0][0]:frames[0][0] + frames[0][1]]); sil += 1
w.close(); os.remove(raw)
print(f"{os.path.basename(out)}: {len(frames)} Frames im Auszug, {kept} behalten, {sil} Platzhalter, {miss} beschädigt -> Platzhalter")
