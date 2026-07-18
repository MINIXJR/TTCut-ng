#!/usr/bin/env python3
"""Drop TS packets of one PID whose PES-PTS falls into given windows.
Deterministic damage injector for ttcut-demux gap-repair tests."""
import argparse, sys

def pts_of_packet(pkt):
    # returns PES-PTS in seconds or None (needs payload_unit_start + PES header)
    if len(pkt) != 188 or pkt[0] != 0x47: return None
    pusi = (pkt[1] >> 6) & 1
    if not pusi: return None
    afc = (pkt[3] >> 4) & 3
    off = 4 + (1 + pkt[4] if afc & 2 else 0)
    if off + 14 > 188: return None
    if pkt[off:off+3] != b'\x00\x00\x01': return None
    flags = pkt[off+7]
    if not (flags & 0x80): return None
    p = pkt[off+9:off+14]
    pts = ((p[0] >> 1) & 7) << 30 | p[1] << 22 | (p[2] >> 1) << 15 | p[3] << 7 | p[4] >> 1
    return pts / 90000.0

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inp'); ap.add_argument('out')
    ap.add_argument('--pid', type=lambda x: int(x, 0), required=True)
    ap.add_argument('--drop-at', type=float, action='append', required=True)
    ap.add_argument('--drop-ms', type=float, action='append', required=True)
    a = ap.parse_args()
    assert len(a.drop_at) == len(a.drop_ms)
    wins = None  # filled after first PTS seen (relative windows)
    first_pts = None
    dropping = False
    n_drop = 0
    with open(a.inp, 'rb') as fi, open(a.out, 'wb') as fo:
        while True:
            pkt = fi.read(188)
            if len(pkt) < 188: break
            pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
            if pid == a.pid:
                pts = pts_of_packet(pkt)
                if pts is not None:
                    if first_pts is None:
                        first_pts = pts
                        wins = [(first_pts + s, first_pts + s + ms/1000.0)
                                for s, ms in zip(a.drop_at, a.drop_ms)]
                    dropping = any(w0 <= pts < w1 for w0, w1 in wins)
                if dropping:
                    n_drop += 1
                    continue      # drop packet (incl. continuation w/o PTS)
            fo.write(pkt)
    print(f"dropped {n_drop} packets of pid 0x{a.pid:x}", file=sys.stderr)

if __name__ == '__main__':
    main()
