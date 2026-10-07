# Order of the first packets as the demuxer hands them out, and by file position.
# usage: order.py ORIGINAL_TS
import json, subprocess, sys
r = subprocess.run(["ffprobe", "-v", "error", "-read_intervals", "%+3",
    "-show_entries", "packet=stream_index,pts_time,dts_time,pos:stream=index,codec_type,codec_name", "-of", "json", sys.argv[1]],
    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
j = json.loads(r.stdout)
st = {s["index"]: s.get("codec_name", "?") for s in j["streams"]}
vi = next(s["index"] for s in j["streams"] if s.get("codec_type") == "video")
pk = j["packets"]
fv = next(k for k, p in enumerate(pk) if p["stream_index"] == vi)
vpos = int(pk[fv]["pos"]); vpts = float(pk[fv]["pts_time"]); vdts = pk[fv].get("dts_time")
print(f"   erstes Bildpaket: Nr. {fv} in Demuxer-Reihenfolge, Dateiposition {vpos}, PTS {vpts:.6f}, DTS {vdts}")
for s in sorted(st):
    if s == vi: continue
    mine = [(k, p) for k, p in enumerate(pk) if p["stream_index"] == s]
    if not mine: continue
    dem = sum(1 for k, p in mine if k < fv)
    byp = sum(1 for k, p in mine if int(p.get("pos", -1)) >= 0 and int(p["pos"]) < vpos)
    pes = sum(1 for k, p in mine if int(p.get("pos", -1)) >= 0)
    early = sum(1 for k, p in mine if "pts_time" in p and float(p["pts_time"]) < float(vdts)) if vdts else -1
    print(f"   Spur {s} {st[s]}: erstes PTS {float(mine[0][1]['pts_time']):.6f}, erste Dateiposition {mine[0][1].get('pos')}; vor dem ersten Bildpaket in Demuxer-Reihenfolge {dem}, PES-Anfänge mit kleinerer Dateiposition {byp} (von {pes} in 3 s); Pakete mit PTS < erstem Bild-DTS: {early}")
