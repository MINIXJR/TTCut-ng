# End-to-end A/V check: how far does each audio track sit from the picture in
# MKV, compared with the original TS?  usage: chain.py ORIGINAL_TS MKV
# Video frames are matched by the MD5 of the decoded picture, audio frames by
# the MD5 of the packet payload. Result per track: (audio shift) - (video shift)
# in ms; positive = sound later against the picture than in the original.
import subprocess, sys, statistics, collections
TS, MKV = sys.argv[1:3]
def run(*a): return subprocess.run(a, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout.decode()
def vframes(f):
    md5 = [l.split(",")[-1].strip() for l in run("ffmpeg", "-v", "error", "-i", f, "-map", "0:v:0", "-fps_mode", "passthrough", "-f", "framemd5", "-").splitlines() if l and not l.startswith("#")]
    pts = []
    for l in run("ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "frame=best_effort_timestamp_time", "-of", "default=nw=1:nk=1", f).split():
        try: pts.append(float(l))
        except ValueError: pass
    if len(md5) != len(pts): sys.exit(f"{f}: {len(md5)} Bild-MD5 gegen {len(pts)} Zeitstempel - Messaufbau falsch")
    return md5, pts
def apackets(f):
    out = collections.defaultdict(list); cur = {}
    for l in run("ffprobe", "-v", "error", "-select_streams", "a", "-show_packets", "-show_data_hash", "md5", "-show_entries", "packet=stream_index,pts_time,data_hash", "-of", "default=nw=1", f).splitlines():
        k, _, v = l.partition("=")
        cur[k] = v
        if k == "data_hash":
            if cur.get("pts_time", "N/A") != "N/A": out[int(cur["stream_index"])].append((v, float(cur["pts_time"])))
            cur = {}
    return out
def uniq(pairs):
    pairs = list(pairs)
    c = collections.Counter(h for h, _ in pairs)
    return {h: p for h, p in pairs if c[h] == 1}
tm, tp = vframes(TS); mm, mp = vframes(MKV)
tv = uniq(zip(tm, tp)); mv = uniq(zip(mm, mp))
dv = [mv[h] - tv[h] for h in mv if h in tv]
if not dv: sys.exit("kein Bild gefunden")
cv = collections.Counter(round(x * 1000, 1) for x in dv)
dvm = statistics.median(dv)
print(f"  Bild: {len(dv)} von {len(mm)} Bildern der MKV im Original gefunden; Verschiebung {dict(cv.most_common(3))}")
names = {int(l.split(",")[0]): l.strip().split(",", 1)[1] for l in run("ffprobe", "-v", "error", "-select_streams", "a", "-show_entries", "stream=index,codec_name:stream_tags=language", "-of", "csv=p=0", TS).splitlines() if l}
ta = {i: uniq(p) for i, p in apackets(TS).items()}
for i, p in sorted(apackets(MKV).items()):
    ma = uniq(p); best = None
    for j, t in ta.items():
        d = [ma[h] - t[h] for h in ma if h in t]
        if d and (best is None or len(d) > len(best[1])): best = (j, d)
    if not best: print(f"  Ton MKV-Spur {i}: kein Paket im Original gefunden"); continue
    j, t = best[0], ta[best[0]]
    import bisect
    vt = sorted((mv[h], mv[h] - tv[h]) for h in mv if h in tv); vk = [x[0] for x in vt]
    runs = []
    for h, pm in sorted(ma.items(), key=lambda x: x[1]):
        if h not in t: continue
        k = min(max(bisect.bisect_left(vk, pm), 0), len(vt) - 1)
        e = round((pm - t[h] - vt[k][1]) * 1000, 1)
        if runs and runs[-1][0] == e: runs[-1][2] = pm; runs[-1][3] += 1
        else: runs.append([e, pm, pm, 1])
    runs = [r for r in runs if r[3] >= 5]
    print(f"  Ton {names.get(j, j):8s}: " + "; ".join(f"{r[0]:+.1f} ms (MKV {r[1]:.1f}-{r[2]:.1f} s, {r[3]} Pakete)" for r in runs))
