#!/usr/bin/env python3
"""Generate the TTCut-ng logo as SVG.

The film strip is one continuous ribbon: both edges are Catmull-Rom curves
through matching key points, frames and perforations are laid out by arc
length (perspective: proportional to the local strip width), and the face
colour changes from the back side to the front side inside the top bend.
The scissors cut in the gap between the last two frames; each ring lies
exactly opposite its blade.

Two profiles, approved by the user on 2026-10-25:
  large  the full wave, for 48 px and up (scalable icon)
  small  a crop starting just before the top bend, wider strip, bolder
         lines, for 16/22/24/32 px (24 px = the 22 px drawing padded)

Usage:
  gen-logo.py --profile large|small [--size 16|22|32] [--part all|film|rings]
              --tux ui/pixmaps/Tux.svg --out FILE.svg
--part film / rings draw only the strip or only the two finger rings, without
shadow; tools/diag/gate_logo.sh uses them to check that the rings stay clear.
"""
import argparse
import math
import re

# key points: (edge A, edge B); A = left/outer at the top bend, B = the other edge
KEYS = [
    ((30, 168), (34, 190)),     # 0 tail start (cut end)
    ((72, 187), (98, 223)),     # 1
    ((110, 202), (180, 248)),   # 2 bottom bend (B is the outer, wide edge)
    ((120, 172), (214, 218)),   # 3
    ((126, 122), (214, 160)),   # 4
    ((150, 66), (218, 116)),    # 5 top bend (A is the outer, wide edge)
    ((246, 34), (232, 150)),    # 6
    ((420, 222), (280, 258)),   # 7
    ((487, 407), (313, 453)),   # 8 near end
]

LARGE = dict(wscale=1.0, ufrom=0.0, ndesc=5, lastk=1.45, room=0.16,
             open=17.0, pdist=48.0, tipx=120.0, rdist=190.0, rx=46.0, ry=32.0, armk=1.0,
             edge=3.5, fst=2, bst=3, perforation=True, shadow=True, dx=5.0,
             viewbox="0 0 512 512")
SMALL = dict(wscale=1.5, ufrom=4.5, ndesc=3, lastk=1.8, room=0.16,
             open=24.0, pdist=30.0, tipx=40.0, rdist=150.0, rx=40.0, ry=28.0, armk=1.6,
             shadow=False, dx=5.0)
# outline widths and crop per size; the viewBoxes frame the drawing as approved
SMALL_SIZES = {
    32: dict(edge=16, fst=10, bst=12, perforation=True, viewbox="18 -12 482 482"),
    22: dict(edge=22, fst=13, bst=16, perforation=True, viewbox="18 -13 484 484"),
    16: dict(edge=30, fst=18, bst=22, perforation=False, viewbox="18 -15 488 488"),
}

MARGIN = 0.17          # frame inset from the strip edges (fraction of the width)
NS = 4000              # arc length table resolution

METADATA = '''<metadata>
  <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#" xmlns:dc="http://purl.org/dc/elements/1.1/">
    <rdf:Description>
      <dc:title>TTCut-ng</dc:title>
      <dc:creator>MINIXJR (logo); Tux: Larry Ewing, Simon Budig, Garrett LeSage</dc:creator>
      <dc:rights>Tux: Larry Ewing (lewing@isc.tamu.edu) and The GIMP</dc:rights>
      <dc:source>https://commons.wikimedia.org/wiki/File:Tux.svg</dc:source>
    </rdf:Description>
  </rdf:RDF>
</metadata>'''


def col(a, b, f):
    f = max(0.0, min(1.0, f))
    a = [int(a[i:i+2], 16) for i in (1, 3, 5)]
    b = [int(b[i:i+2], 16) for i in (1, 3, 5)]
    return "#" + "".join(f"{round(x + (y-x)*f):02x}" for x, y in zip(a, b))


def smooth(x):
    x = max(0.0, min(1.0, x))
    return x*x*(3-2*x)


def poly(pts):
    return " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)


def rot(v, deg):
    a = math.radians(deg)
    return v[0]*math.cos(a) - v[1]*math.sin(a), v[0]*math.sin(a) + v[1]*math.cos(a)


def catmull(pts, u):
    i = min(int(u), len(pts)-2)
    t = u - i
    p0 = pts[max(i-1, 0)]
    p1, p2 = pts[i], pts[i+1]
    p3 = pts[min(i+2, len(pts)-1)]
    return tuple(0.5*((2*p1[k]) + (-p0[k]+p2[k])*t + (2*p0[k]-5*p1[k]+4*p2[k]-p3[k])*t*t
                      + (-p0[k]+3*p1[k]-3*p2[k]+p3[k])*t**3) for k in (0, 1))


class Strip:
    """Strip geometry: u runs 0..K-1 along the key points, f 0..1 across (edge A to B)."""

    def __init__(self, wscale):
        keys = [(((a[0]+b[0])/2 + (a[0]-b[0])/2*wscale, (a[1]+b[1])/2 + (a[1]-b[1])/2*wscale),
                 ((a[0]+b[0])/2 + (b[0]-a[0])/2*wscale, (a[1]+b[1])/2 + (b[1]-a[1])/2*wscale))
                for a, b in KEYS]
        self.ea = [k[0] for k in keys]
        self.eb = [k[1] for k in keys]
        self.umax = len(keys) - 1
        self.us = [self.umax*i/NS for i in range(NS+1)]
        self.arc = [0.0]
        for i in range(1, NS+1):
            self.arc.append(self.arc[-1] + math.dist(self.pt(self.us[i-1], 0.5), self.pt(self.us[i], 0.5)))

    def pt(self, u, f):
        a, b = catmull(self.ea, u), catmull(self.eb, u)
        return a[0] + (b[0]-a[0])*f, a[1] + (b[1]-a[1])*f

    def width(self, u):
        return math.dist(catmull(self.ea, u), catmull(self.eb, u))

    def u_at(self, s):
        lo, hi = 0, NS
        while lo < hi:
            m = (lo+hi)//2
            if self.arc[m] < s:
                lo = m+1
            else:
                hi = m
        return self.us[lo]

    def s_at(self, u):
        return self.arc[int(round(u/self.umax*NS))]

    def band(self, u0, u1, f0, f1, k=8):
        pts = [self.pt(u0 + (u1-u0)*i/k, f0) for i in range(k+1)]
        pts += [self.pt(u1 - (u1-u0)*i/k, f1) for i in range(k+1)]
        return pts


def frames(st, p, defs, out, name, ua, ub, ca, cb, gloss, flen, n, last_scale=1.0):
    """Exactly n frames that fill [ua, ub]; length and gap scale with the local width.
    Frames before the strip start (p['ufrom']) are cut off. Returns the (u0, u1) list."""
    def layout(k):
        qs, s = [], st.s_at(ua)
        for i in range(n):
            u0 = st.u_at(s)
            w = st.width(u0)
            length = flen*w*k*(last_scale if i == n-1 else 1.0)
            qs.append((u0, st.u_at(s+length)))
            s += length + 0.13*w*k
        return qs, s - 0.13*st.width(st.u_at(s))*k

    lo, hi = 0.2, 3.0
    for _ in range(40):
        k = (lo+hi)/2
        if layout(k)[1] > st.s_at(ub):
            hi = k
        else:
            lo = k
    quads = layout(lo)[0]
    for i, (u0, u1) in enumerate(quads):
        f = i/max(1, len(quads)-1)
        if u1 <= p['ufrom']:
            continue
        u0 = max(u0, p['ufrom'])
        gid = f"tt_{name}{i}"
        a, b = st.pt(u0, 0.5), st.pt(u1, 0.5)
        defs.append(f'<linearGradient id="{gid}" gradientUnits="userSpaceOnUse" x1="{a[0]:.1f}" y1="{a[1]:.1f}" '
                    f'x2="{b[0]:.1f}" y2="{b[1]:.1f}"><stop offset="0" stop-color="{col(ca[0], cb[0], f)}"/>'
                    f'<stop offset="1" stop-color="{col(ca[1], cb[1], f)}"/></linearGradient>')
        out.append(f'<polygon points="{poly(st.band(u0, u1, MARGIN, 1-MARGIN))}" fill="url(#{gid})" '
                   f'stroke="#1d2350" stroke-width="{p["fst"]}" stroke-linejoin="round"/>')
        out.append(f'<polygon points="{poly(st.band(u0, u0+(u1-u0)*0.55, MARGIN, 1-MARGIN))}" '
                   f'fill="#ffffff" fill-opacity="{gloss}"/>')
    return quads


def clips_to_masks(svg):
    """Turn every <clipPath> of Tux.svg into an equivalent white <mask>.

    Qt's SVG renderer (the icon engine Plasma uses; measured with Qt 6.11)
    ignores clip-path altogether but draws masks, and it skips
    feComponentTransfer (hence the feColorMatrix in tt_shadow). Each clipPath
    in Tux.svg holds <use> references to plain paths with their own fill, so
    the mask copies the path data with a white fill instead. librsvg draws the
    result as before (measured: RMSE 0.0001 against the clipped original).
    """
    def path_d(pid):
        return re.search(r'<path\s+id="%s"[^>]*?\sd="([^"]*)"' % re.escape(pid), svg, re.S).group(1)

    def mask(m):
        shapes = "".join(f'<path fill="#fff" d="{path_d(pid)}"/>'
                         for pid in re.findall(r'<use href="#([^"]+)"', m.group(2)))
        return (f'<mask id="{m.group(1)}" maskUnits="userSpaceOnUse" '
                f'x="-50" y="-50" width="316" height="356">{shapes}</mask>')

    svg = re.sub(r'<clipPath id="([^"]+)">(.*?)</clipPath>', mask, svg, flags=re.S)
    return svg.replace('clip-path="url(#', 'mask="url(#')


def draw_film(st, p, defs, tux_src):
    """Strip body, perforation, frames, outlines and Tux. Returns (svg parts, desc frame list)."""
    framed = []
    frames(st, p, defs, framed, "tail", 0.12, 1.80, ("#4a3a96", "#3a2c80"), ("#43348f", "#33267a"), 0.10, 0.70, 3)
    frames(st, p, defs, framed, "rise", 2.30, 4.80, ("#3f2f8e", "#2e2272"), ("#4a39a0", "#33267a"), 0.10, 0.62, 3)
    # the frames may fill up to this point, which fixes the size of the Tux frame
    u_last = st.u_at(st.s_at(st.umax) - p['room']*st.width(st.umax))
    quads = frames(st, p, defs, framed, "desc", 4.92, u_last, ("#4a3fa6", "#2b2f86"), ("#4f86ec", "#2f58c8"),
                   0.16, 0.60, p['ndesc'], p['lastk'])

    # the strip ends half a frame gap after the last frame
    gap = st.s_at(quads[-1][0]) - st.s_at(quads[-2][1])
    s_end = st.s_at(quads[-1][1]) + 0.5*gap
    u_end = st.u_at(s_end)
    uf = p['ufrom']

    # body: thin slices, colour blended from the back side to the front side over the top bend
    back_a, back_b = "#e8e1f7", "#b8aadb"     # back side: light lavender
    front_a, front_b = "#ffffff", "#d5d9e7"   # front side: white
    film = []
    sl = 260
    for i in range(sl):
        u0, u1 = uf + (u_end-uf)*i/sl, uf + (u_end-uf)*(i+1)/sl
        face = smooth((u0 - 4.84) / 0.3)                    # 0 = back, 1 = front (inside a frame gap)
        light = 0.5 + 0.5*math.sin(u0*1.3)                  # gentle shading along the strip
        bend = math.exp(-((u0-2.0)/0.35)**2) + math.exp(-((u0-5.0)/0.35)**2)   # darker inside the bends
        c_back = col(back_a, back_b, light*0.6 + bend*0.35)
        c_front = col(front_a, front_b, light*0.6 + bend*0.35)
        c = col(c_back, c_front, face)
        film.append(f'<polygon points="{poly(st.band(u0, u1 + 0.004, 0, 1, 2))}" fill="{c}" stroke="{c}" '
                    f'stroke-width="0.8"/>')

    # perforation: phase taken from the strip end, so the cut lies in the middle of a gap
    s = s_end - 0.0425*st.width(u_end) - 0.075*st.width(u_end)
    while p['perforation'] and s > st.s_at(uf) + 0.02*st.width(uf):
        u = st.u_at(s)
        w = st.width(u)
        u1 = st.u_at(s + 0.075*w)
        for f0, f1 in ((0.05, 0.12), (0.88, 0.95)):
            film.append(f'<polygon points="{poly(st.band(u, u1, f0, f1, 2))}" fill="#262c48" stroke="#262c48" '
                        f'stroke-width="1.2" stroke-linejoin="round"/>')
        s -= 0.16*st.width(st.u_at(s - 0.08*w))

    film.extend(framed)

    # continuous edge lines and the two cut ends
    for e in (0.0, 1.0):
        pts = [st.pt(uf + (u_end-uf)*i/400, e) for i in range(401)]
        film.append(f'<polyline points="{poly(pts)}" fill="none" stroke="#232a44" stroke-width="{p["edge"]}" '
                    f'stroke-linejoin="round" stroke-linecap="round"/>')
    for u in (uf, u_end):
        a, b = st.pt(u, 0), st.pt(u, 1)
        film.append(f'<line x1="{a[0]:.1f}" y1="{a[1]:.1f}" x2="{b[0]:.1f}" y2="{b[1]:.1f}" '
                    f'stroke="#232a44" stroke-width="{p["edge"]}" stroke-linecap="round"/>')

    # Tux in the last (largest) frame, unchanged from Tux.svg, clipped to the frame
    u0, u1 = quads[-1]
    q = st.band(u0, u1, MARGIN, 1-MARGIN)
    xs, ys = [pt[0] for pt in q], [pt[1] for pt in q]
    cx, cy = (min(xs)+max(xs))/2, (min(ys)+max(ys))/2
    th = min(st.s_at(u1) - st.s_at(u0), st.width((u0+u1)/2)*(1-2*MARGIN)) * 0.86
    tw = th*216/256
    # Tux.svg's root <svg> becomes a <g>: Qt's SVG renderer (the icon engine
    # Plasma uses) draws nothing inside a nested <svg>. The transform is what
    # a nested <svg x y width height viewBox="0 0 216 256"> with the default
    # preserveAspectRatio (xMidYMid meet) would apply.
    x, y, w, h = (float(f"{v:.1f}") for v in (cx-tw/2, cy-th/2+2, tw, th))
    s = min(w/216, h/256)
    tux = open(tux_src, encoding="utf-8").read()
    tux = re.sub(r'<\?xml[^>]*\?>', '', tux)
    tux = re.sub(r'<svg\b[^>]*>', f'<g transform="translate({x + (w-216*s)/2:.4f} {y + (h-256*s)/2:.4f}) '
                 f'scale({s:.6f})">', tux, count=1)
    tux = tux[:tux.rindex('</svg>')] + '</g>'
    tux = clips_to_masks(tux)
    defs.append(f'<mask id="tt_lastframe" maskUnits="userSpaceOnUse" x="-100" y="-100" width="800" height="800">'
                f'<polygon fill="#fff" points="{poly(q)}"/></mask>')
    film.append(f'<g id="tux" mask="url(#tt_lastframe)">{tux}</g>')
    return film, quads


class Scissors:
    """Scissors placed on the gap between the last two frames; the cut line bisects the blades."""

    def __init__(self, st, p, quads):
        self.p = p
        ug = (quads[-2][1] + quads[-1][0]) / 2
        pa, pb = st.pt(ug, 0), st.pt(ug, 1)
        length = math.dist(pa, pb)
        d = ((pa[0]-pb[0])/length, (pa[1]-pb[1])/length)
        self.piv = (pb[0] - d[0]*p['pdist'], pb[1] - d[1]*p['pdist'])
        back = (-d[0], -d[1])
        self.tip_front = self.at(rot(d, -p['open']), length + p['tipx'])
        self.tip_back = self.at(rot(d, p['open']), length + p['tipx'] + 8)
        self.ring_front = self.at(rot(back, -p['open']), p['rdist'])
        self.ring_back = self.at(rot(back, p['open']), p['rdist'])

    def at(self, v, dist):
        return self.piv[0] + v[0]*dist, self.piv[1] + v[1]*dist

    def blade(self, tip, side, steel_id):
        piv = self.piv
        dx, dy = tip[0]-piv[0], tip[1]-piv[1]
        l = math.hypot(dx, dy)
        ux, uy = dx/l, dy/l
        nx, ny = -uy*side, ux*side
        spine_w = edge = 13
        p_spine = (piv[0] + nx*spine_w, piv[1] + ny*spine_w)
        p_belly = (piv[0] + ux*l*0.45 + nx*spine_w*0.95, piv[1] + uy*l*0.45 + ny*spine_w*0.95)
        p_edge = (piv[0] - nx*edge, piv[1] - ny*edge)
        p_edge_mid = (piv[0] + ux*l*0.5 - nx*edge*0.25, piv[1] + uy*l*0.5 - ny*edge*0.25)
        d = (f'M{p_edge[0]:.1f},{p_edge[1]:.1f} L{p_spine[0]:.1f},{p_spine[1]:.1f} '
             f'Q{p_belly[0]:.1f},{p_belly[1]:.1f} {tip[0]:.1f},{tip[1]:.1f} '
             f'Q{p_edge_mid[0]:.1f},{p_edge_mid[1]:.1f} {p_edge[0]:.1f},{p_edge[1]:.1f}Z')
        hl = (f'M{p_spine[0]+ux*14:.1f},{p_spine[1]+uy*14:.1f} '
              f'Q{p_belly[0]-nx*0.25:.1f},{p_belly[1]-ny*0.25:.1f} {tip[0]-ux*16:.1f},{tip[1]-uy*16:.1f}')
        return (f'<path d="{d}" fill="url(#{steel_id})" stroke="#2b2240" stroke-width="{self.p["bst"]}" '
                f'stroke-linejoin="round"/>'
                f'<path d="{hl}" fill="none" stroke="#ffffff" stroke-opacity="0.75" stroke-width="3" '
                f'stroke-linecap="round"/>')

    def ring(self, ring_c):
        """The ring ellipse, oriented along its arm (pivot -> ring)."""
        ang = math.degrees(math.atan2(ring_c[1]-self.piv[1], ring_c[0]-self.piv[0]))
        return (f'<ellipse cx="{ring_c[0]:.1f}" cy="{ring_c[1]:.1f}" rx="{self.p["rx"]}" ry="{self.p["ry"]}" '
                f'transform="rotate({ang:.1f} {ring_c[0]:.1f} {ring_c[1]:.1f})"')

    def ring_strokes(self, ring_c):
        k, r = self.p['armk'], self.ring(ring_c)
        return (f'{r} fill="none" stroke="#2b2240" stroke-width="{29*k}"/>'
                f'{r} fill="none" stroke="url(#tt_lilac)" stroke-width="{23*k}"/>'
                f'{r} fill="none" stroke="#ffffff" stroke-opacity="0.45" stroke-width="5" '
                f'stroke-dasharray="70 400" stroke-dashoffset="-150" stroke-linecap="round"/>')

    def handle(self, ring_c):
        piv, k = self.piv, self.p['armk']
        dx, dy = ring_c[0]-piv[0], ring_c[1]-piv[1]
        l = math.hypot(dx, dy)
        ux, uy = dx/l, dy/l
        start = (piv[0] + ux*6, piv[1] + uy*6)
        neck = (ring_c[0] - ux*self.p['rx']*0.95, ring_c[1] - uy*self.p['rx']*0.95)
        arm = f'M{start[0]:.1f},{start[1]:.1f} L{neck[0]:.1f},{neck[1]:.1f}'
        r = self.ring(ring_c)
        return (f'<path d="{arm}" stroke="#2b2240" stroke-width="{31*k}" stroke-linecap="round" fill="none"/>'
                f'{r} fill="none" stroke="#2b2240" stroke-width="{29*k}"/>'
                f'<path d="{arm}" stroke="url(#tt_lilac)" stroke-width="{25*k}" stroke-linecap="round" fill="none"/>'
                f'{r} fill="none" stroke="url(#tt_lilac)" stroke-width="{23*k}"/>'
                f'{r} fill="none" stroke="#ffffff" stroke-opacity="0.45" stroke-width="5" '
                f'stroke-dasharray="70 400" stroke-dashoffset="-150" stroke-linecap="round"/>')

    def back_half(self):
        """Lower blade and upper ring: drawn behind the film."""
        return self.handle(self.ring_back) + self.blade(self.tip_back, -1, "tt_steel_back")

    def front_half(self):
        """Upper blade, lower ring and the pivot screw: drawn in front of the film."""
        piv = self.piv
        return (self.handle(self.ring_front) + self.blade(self.tip_front, 1, "tt_steel")
                + f'<circle cx="{piv[0]}" cy="{piv[1]}" r="15" fill="url(#tt_screw)" stroke="#2b2240" stroke-width="3"/>'
                f'<circle cx="{piv[0]-4}" cy="{piv[1]-5}" r="4.5" fill="#ffffff" fill-opacity="0.8"/>')


def build(p, tux_src, part="all"):
    st = Strip(p['wscale'])
    defs = []
    film, quads = draw_film(st, p, defs, tux_src)
    sc = Scissors(st, p, quads)
    if part == "film":
        content = f'<g id="film">{"".join(film)}</g>'
    elif part == "rings":
        content = sc.ring_strokes(sc.ring_back) + sc.ring_strokes(sc.ring_front)
    else:
        content = (f'<g id="scissors-back">{sc.back_half()}</g>\n'
                   f'  <g id="film">{"".join(film)}</g>\n'
                   f'  <g id="scissors-front">{sc.front_half()}</g>')
    shadow = ' filter="url(#tt_shadow)"' if p['shadow'] and part == "all" else ''
    return f'''<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="512" height="512" viewBox="{p['viewbox']}">
<title>TTCut-ng</title>
{METADATA}
<defs>
  <linearGradient id="tt_steel" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#f6f8fc"/><stop offset="0.55" stop-color="#c3c9d6"/><stop offset="1" stop-color="#8a92a6"/></linearGradient>
  <linearGradient id="tt_steel_back" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#d9dde7"/><stop offset="0.6" stop-color="#a2a9ba"/><stop offset="1" stop-color="#6d7488"/></linearGradient>
  <linearGradient id="tt_lilac" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#a98be0"/><stop offset="0.5" stop-color="#7a5ab8"/><stop offset="1" stop-color="#4f3488"/></linearGradient>
  <radialGradient id="tt_screw" cx="0.35" cy="0.3" r="0.8">
    <stop offset="0" stop-color="#e9e2f7"/><stop offset="1" stop-color="#6d54a0"/></radialGradient>
  <filter id="tt_shadow" x="-10%" y="-10%" width="125%" height="130%">
    <feGaussianBlur in="SourceAlpha" stdDeviation="6"/>
    <feOffset dx="3" dy="7" result="o"/>
    <feColorMatrix type="matrix" values="0 0 0 0 0  0 0 0 0 0  0 0 0 0 0  0 0 0 0.38 0"/>
    <feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge>
  </filter>
  {"".join(defs)}
</defs>
<g transform="translate({256 + p['dx']} 256) scale(0.87) translate(-268 -250)"><g{shadow}>
  {content}
</g></g>
</svg>
'''


def main():
    ap = argparse.ArgumentParser(description="Generate the TTCut-ng logo as SVG.")
    ap.add_argument("--profile", choices=("large", "small"), required=True)
    ap.add_argument("--size", type=int, choices=sorted(SMALL_SIZES), help="required for --profile small")
    ap.add_argument("--part", choices=("all", "film", "rings"), default="all")
    ap.add_argument("--tux", required=True, help="path to ui/pixmaps/Tux.svg")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    if a.profile == "small":
        if a.size is None:
            ap.error("--profile small needs --size")
        p = dict(SMALL, **SMALL_SIZES[a.size])
    else:
        if a.size is not None:
            ap.error("--size applies to --profile small only")
        p = dict(LARGE)
    with open(a.out, "w", encoding="utf-8") as f:
        f.write(build(p, a.tux, a.part))


if __name__ == "__main__":
    main()
