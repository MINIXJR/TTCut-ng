# Packet lists of a recording (all segments) into one pickle:
# vp = video PTS in stream order, ap = {stream index: [(md5, pts), ...]}, names.
# usage: probe_packets.py OUT.pickle SEGMENT.ts [SEGMENT.ts ...]
import subprocess, sys, collections, pickle
out, segs = sys.argv[1], sys.argv[2:]
vp = []; ap = collections.defaultdict(list); names = {}
for s in segs:
    for l in subprocess.run(["ffprobe", "-v", "error", "-show_entries", "stream=index,codec_type,codec_name", "-of", "csv=p=0", s], stdout=subprocess.PIPE).stdout.decode().splitlines():
        f = l.split(",")
        if len(f) >= 3: names[int(f[0])] = (f[2], f[1])   # ffprobe prints index,codec_name,codec_type
    vidx = min(i for i, (t, c) in names.items() if t == "video")
    pr = subprocess.Popen(["ffprobe", "-v", "error", "-show_packets", "-show_data_hash", "md5", "-show_entries", "packet=stream_index,pts_time,data_hash", "-of", "default=nw=1", s], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    cur = {}
    for l in pr.stdout:
        k, _, v = l.rstrip("\n").partition("="); cur[k] = v
        if k == "data_hash":
            if cur.get("pts_time", "N/A") != "N/A":
                i = int(cur["stream_index"]); p = float(cur["pts_time"])
                if i == vidx: vp.append(p)
                elif names.get(i, ("", ""))[0] == "audio": ap[i].append((v.split(":")[-1].lower(), p))
            cur = {}
    pr.wait()
pickle.dump((vp, dict(ap), names), open(out, "wb"))
print(out, len(vp), "Bildpakete,", {i: len(v) for i, v in ap.items()})
