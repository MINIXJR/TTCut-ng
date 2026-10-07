# Offset of ES B against ES A along the whole file. usage: esdelta.py A B POINTS
import sys, subprocess
a = open(sys.argv[1], "rb").read(); b = open(sys.argv[2], "rb").read(); n = int(sys.argv[3])
br = int(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "stream=bit_rate", "-of", "csv=p=0", sys.argv[1]], stdout=subprocess.PIPE).stdout.decode().split()[0].strip(","))
bpm = br / 8000.0; runs = []; miss = 0
for k in range(n):
    off = int(len(a) * (k + 0.5) / n); pat = a[off:off + 4096]
    lo = max(0, off - 4_000_000); p = b.find(pat, lo, off + 4_000_000)
    if p < 0: miss += 1; continue
    d = round((p - off) / bpm, 1)
    if runs and runs[-1][0] == d: runs[-1][2] = off / bpm / 1000
    else: runs.append([d, off / bpm / 1000, off / bpm / 1000])
print(f"   {n} Messpunkte, {miss} ohne Treffer; B gegen A: " + "; ".join(f"{r[0]:+.1f} ms ({r[1]:.0f}-{r[2]:.0f} s)" for r in runs))
