# Rule "anchor every audio frame on its own timestamp", computed on the packet
# lists of the original:
#   picture timeline = TTCut's frame index: pictures in display order, frame i
#   shown at i/fps; a PTS with no picture is a hole.
#   audio: frame with PTS p -> ES slot k = round(vtime(p) / fd); p in a hole or
#   before the first picture -> drop; slot already taken -> drop; slots skipped
#   -> that many silence frames.
# PTS discontinuities (backward jumps) split video and audio into runs that are
# matched by order. usage: zonesim.py PICKLE FPS
import sys, pickle, bisect, collections
PK, fps = sys.argv[1], float(sys.argv[2]); dur = 1.0 / fps
PLAN = sys.argv[3] if len(sys.argv) > 3 else None
import json, os
vp, ap, names = pickle.load(open(PK, "rb"))
def runs(pts, back=0.5, sustain=10):
    # a new run only where the stream stays below the recent level for
    # `sustain` packets; the level is the 3rd-highest PTS of the last 12
    # packets, so a single stray PTS (corrupt packet) counts for nothing
    r = [[]]
    for i, p in enumerate(pts):
        if len(r[-1]) >= 12:
            ref = sorted(r[-1][-12:])[-3]
            if p < ref - back and all(q < ref - back for q in pts[i:i + sustain]):
                r.append([])
        r[-1].append(p)
    return r
vruns = runs(vp)
h26x = any(c in ("h264", "hevc") for v in names.values() for c in v)
vs = []; base = []; n = 0
for r in vruns:
    s = sorted(set(r))
    if h26x and not vs: s = [p for p in s if p >= r[0] - 1e-6]     # cold start: leading pictures are not shown
    vs.append(s); base.append(n); n += len(s)
dup = len(vp) - n
holes = [(s[i], round((s[i+1]-s[i])/dur) - 1) for s in vs for i in range(len(s)-1) if round((s[i+1]-s[i])/dur) != 1]
print(f"Bild: {len(vp)} Pakete, {len(vruns)} Lauf/Läufe, {dup} doppelte PTS, {n} Bilder = {n/fps:.1f} s; {len(holes)} Löcher = {sum(h[1] for h in holes)*dur*1000:.0f} ms"
      + (f" (Läufe beginnen bei {[round(r[0],1) for r in vruns]})" if len(vruns) > 1 else ""))
def vtime(p, r):
    s = vs[r]; i = bisect.bisect_right(s, p + 1e-6) - 1
    if i < 0: return None
    if p >= s[i] + dur - 1e-6: return None          # hole
    return (base[r] + i) * dur + (p - s[i])
for idx in sorted(ap):
    codec = next((c for c in names[idx] if c in ("ac3", "eac3", "mp2")), names[idx][1]); fd = {"ac3": 0.032, "eac3": 0.032, "mp2": 0.024}.get(codec)
    if fd is None: print(f"Spur {idx} ({codec}): übersprungen"); continue
    pts = [p for _, p in ap[idx]]
    aruns = runs(pts)
    if len(aruns) != len(vruns): print(f"Spur {idx} ({codec}): {len(aruns)} Ton-Läufe gegen {len(vruns)} Bild-Läufe - Zuordnung nach PTS-Bereich"); 
    last = -1; drops = []; ins = []; kept = 0; head_drop = 0; errs = []; plan = []; pi = -1
    for r, ar in enumerate(aruns):
        vr = r if len(aruns) == len(vruns) else max(range(len(vruns)), key=lambda j: -abs(vs[j][0] - ar[0]))
        for p in ar:
            pi += 1
            t = vtime(p, vr)
            if t is None:
                if last < 0 and vr == 0 and p < vs[0][0]: head_drop += 1
                else: drops.append(p)
                continue
            k = last + 1 if abs(t - (last + 1) * fd) <= 0.75 * fd else int(t / fd + 0.5)
            if k <= last: drops.append(p); continue
            if k > last + 1: ins.append((last + 1, k - last - 1, p)); plan.append(["sil", k - last - 1])
            last = k; kept += 1; errs.append((k * fd - t) * 1000); plan.append(["keep", pi, ap[idx][pi][0]])
    tail = n * dur - (last + 1) * fd
    if PLAN: json.dump({"codec": codec, "plan": plan, "video_frames": n}, open(os.path.join(PLAN, f"plan_{idx}.json"), "w"))
    # cluster drops by time (gap > 2 s = new cluster)
    cl = []
    for p in drops:
        if cl and p - cl[-1][1] < 2: cl[-1][1] = p; cl[-1][2] += 1
        else: cl.append([p, p, 1])
    print(f"Spur {idx} ({codec}): {len(pts)} Pakete; vor Bildanfang {head_drop}; behalten {kept}; entfernt {len(drops)} Frames = {len(drops)*fd*1000:.0f} ms in {len(cl)} Gruppen; "
          f"Stille {sum(c for _, c, _ in ins)} Frames = {sum(c for _, c, _ in ins)*fd*1000:.0f} ms an {len(ins)} Stellen; Ende: Bild endet {tail*1000:+.0f} ms nach dem letzten Ton; "
          f"Fehler ES-Zeit minus Bildzeit: min {min(errs):+.1f}, max {max(errs):+.1f} ms")
    for c in cl[:12]: print(f"      entfernt {c[2]} ab PTS {c[0]:.3f} (Bildzeit {vtime(c[0], 0) if vtime(c[0],0) is not None else float('nan'):.1f} s)" if False else f"      entfernt {c[2]} Frames = {c[2]*fd*1000:.0f} ms, PTS {c[0]:.3f}-{c[1]:.3f}")
    for k0, c, p in ins[:12]: print(f"      Stille {c} Frames = {c*fd*1000:.0f} ms vor ES-Frame {k0} ({k0*fd:.1f} s), nächster Ton-PTS {p:.3f}")
