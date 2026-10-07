# Probe: can the head of an audio ES be cut by whole frames using the packet
# list of the original? usage: headcut_probe.py TS_FOR_PROBE V0 STREAM ES_FILE
import subprocess, sys, hashlib
ts, v0, st, es = sys.argv[1], float(sys.argv[2]), sys.argv[3], sys.argv[4]
out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", st, "-read_intervals", "%+3", "-show_packets", "-show_data_hash", "md5",
                      "-show_entries", "packet=pts_time,size,data_hash", "-of", "default=nw=1", ts], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout.decode()
pk = []; cur = {}
for l in out.splitlines():
    k, _, v = l.partition("="); cur[k] = v
    if k == "data_hash": pk.append((float(cur["pts_time"]), int(cur["size"]), v.split(":")[-1].lower())); cur = {}
n = min(range(len(pk)), key=lambda i: abs(pk[i][0] - v0))        # frame nearest to the reference
off = sum(p[1] for p in pk[:n])
d = open(es, "rb").read(off + pk[n][1] + pk[n+1][1])
ok0 = hashlib.md5(d[:pk[0][1]]).hexdigest() == pk[0][2]
okn = hashlib.md5(d[off:off + pk[n][1]]).hexdigest() == pk[n][2]
okn1 = hashlib.md5(d[off + pk[n][1]:off + pk[n][1] + pk[n+1][1]]).hexdigest() == pk[n+1][2]
print(f"   Spur {st}: Frame {n} liegt {1000*(pk[n][0]-v0):+.1f} ms am Bezug, Byte {off}; ES-Frame 0 = Paket 0: {ok0}; ES bei Byte {off} = Paket {n}: {okn}, danach Paket {n+1}: {okn1}")
