# Missing display slots of the video stream in a TS: sort the picture PTS and
# list every hole. usage: holes.py TS FPS
import subprocess, sys
ts, fps = sys.argv[1], float(sys.argv[2]); dur = 1.0 / fps
out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "packet=pts_time,dts_time,flags", "-of", "csv=p=0", ts], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout.decode()
rows = []
for l in out.splitlines():
    f = l.split(",")
    try: rows.append((float(f[0]), float(f[1]), f[2]))
    except (ValueError, IndexError): pass
p = sorted(r[0] for r in rows)
print(f"{len(p)} Bildpakete, PTS {p[0]:.3f}..{p[-1]:.3f}, Spanne/Dauer {(p[-1]-p[0])/dur+1:.1f} Plaetze")
tot = 0
for a, b in zip(p, p[1:]):
    miss = round((b - a) / dur) - 1
    if miss != 0:
        tot += miss; print(f"  Loch nach PTS {a:.3f} (+{a-p[0]:.3f} s): {miss} Bilder = {miss*dur*1000:.0f} ms" + ("  [doppelter PTS]" if miss < 0 else ""))
print(f"  Summe fehlender Plaetze: {tot} = {tot*dur*1000:.0f} ms")
# decode-order view around DTS jumps
for i in range(1, len(rows)):
    d = rows[i][1] - rows[i-1][1]
    if d > 0.5 or d < 0:
        print(f"  DTS-Sprung {d*1000:.0f} ms bei Paket {i}: " + " | ".join(f"pts {r[0]:.3f} dts {r[1]:.3f} {r[2]}" for r in rows[max(0,i-4):i+5]))
