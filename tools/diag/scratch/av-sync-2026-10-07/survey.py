# Does the repair remux of ttcut-demux keep the timestamps of all streams at one
# fixed distance? Takes the first 80 MB of a recording, remuxes it with the
# options of ttcut-demux and maps every remuxed packet back to its original
# packet by the MD5 of its bytes.
# usage: survey.py ORIGINAL_TS [FFLAGS]     (FFLAGS default "+genpts+igndts", "-" = none)
import json, subprocess, sys, os
SRC = sys.argv[1]; FFLAGS = sys.argv[2] if len(sys.argv) > 2 else "+genpts+igndts"
HEAD, REP = "head.ts", "rep.ts"
def run(*a, **k): return subprocess.run(a, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **k)
with open(SRC, "rb") as i, open(HEAD, "wb") as o: o.write(i.read(80_000_000))
def packets(f):
    r = run("ffprobe", "-v", "error", "-read_intervals", "%+12", "-show_data_hash", "md5",
            "-show_entries", "packet=stream_index,pts_time,pos,data_hash:stream=index,codec_type,codec_name",
            "-of", "json", f)
    j = json.loads(r.stdout)
    st = {s["index"]: (s.get("codec_type", "?"), s.get("codec_name", "?")) for s in j["streams"]}
    pk = [(p["stream_index"], float(p["pts_time"]) if "pts_time" in p else None, int(p.get("pos", -1)), p["data_hash"]) for p in j["packets"]]
    return st, pk
ost, opk = packets(HEAD)
cmd = ["ffmpeg", "-y", "-v", "warning"] + ([] if FFLAGS == "-" else ["-fflags", FFLAGS]) + \
      ["-i", HEAD, "-c", "copy", "-avoid_negative_ts", "make_zero", "-map", "0:v:0", "-map", "0:a?", REP]
r = run(*cmd)
warn = [l for l in r.stderr.decode(errors="replace").splitlines() if l.strip()]
rst, rpk = packets(REP)
print(f"== {SRC}\n   fflags {FFLAGS}; Remux-Meldungen: {len(warn)}" + (f" (erste: {warn[0][:110]})" if warn else ""))
# file order at the start of the original
vidx = next(i for i, s in sorted(ost.items()) if s[0] == "video")
vpos = next(p[2] for p in opk if p[0] == vidx)
before = {}
for s, pts, pos, h in opk:
    if pos < vpos and s != vidx: before[s] = before.get(s, 0) + 1
vp = [p[1] for p in opk if p[0] == vidx and p[1] is not None]
v2 = [x for x in vp if x <= vp[0] + 2.0]
print(f"   Bild Spur {vidx} {ost[vidx][1]}: erstes Paket PTS {vp[0]:.6f}, kleinstes PTS in 2 s {min(v2):.6f} (Abstand {1000*(vp[0]-min(v2)):.0f} ms)")
print("   Pakete in der Datei vor dem ersten Bildpaket: " + (", ".join(f"Spur {s} {ost[s][1]}: {n}" for s, n in sorted(before.items())) or "keine"))
index = {}
for s, pts, pos, h in opk: index.setdefault(h, []).append((s, pts))
res = {}
for rs in sorted(rst):
    runs = []; miss = 0; n = 0; osrc = None
    for s, pts, pos, h in rpk:
        if s != rs: continue
        hit = index.get(h, [])
        if len(hit) != 1 or pts is None or hit[0][1] is None: miss += 1; n += 1; continue
        osrc = hit[0][0]; d = hit[0][1] - pts
        if not runs or abs(d - runs[-1][1]) > 2e-4: runs.append([n, d, 1])
        else: runs[-1][2] += 1
        n += 1
    res[rs] = runs
    txt = "; ".join(f"ab Paket {a}: {d:.6f} ({c} Pakete)" for a, d, c in runs[:6]) + (" …" if len(runs) > 6 else "")
    print(f"   remuxt Spur {rs} {rst[rs][1]} (Original-Spur {osrc}): {n} Pakete, {miss} ohne eindeutigen Treffer; Original minus remuxt: {txt}")
main = {rs: max(runs, key=lambda x: x[2])[1] for rs, runs in res.items() if runs}
ref = main.get(0)
for rs, runs in res.items():
    if not runs: continue
    j = [f"{1000*(d-main[rs]):+.1f} ms bei Paket {a} ({c} Pakete)" for a, d, c in runs if abs(d - main[rs]) > 2e-4]
    off = "" if ref is None or rs == 0 else f", Hauptabstand gegen Bild {1000*(main[rs]-ref):+.1f} ms"
    print(f"   -> Spur {rs}: " + ("ABWEICHUNG " + ", ".join(j[:4]) if j else "fester Abstand") + off)
os.remove(HEAD); os.remove(REP)
