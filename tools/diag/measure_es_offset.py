#!/usr/bin/env python3
"""Misst den Ton-Versatz zwischen zwei Audio-ES ueber den ganzen Verlauf.

Warum nicht die Gesamtdrift: das End-Padding von ttcut-demux normiert die
Dauer beider Spuren, deshalb sieht die Drift am Dateiende auch dann sauber
aus, wenn der Ton mitten im Stream um Sekunden verschoben ist (gemessen
2026-08-24 auf "03x15": duration_drift_ms=-14 bei 808 ms realem Versatz).

Verfahren: ein Byte-Muster aus dem Referenz-ES an mehreren Zeitpunkten im
Pruef-ES wiederfinden. CBR-Framegroesse macht Byte-Offset und Zeit direkt
umrechenbar - MP2@48k 576 B / 24 ms, AC3 1792 B / 32 ms.

Aufruf: measure_es_offset.py REF.mp2 PRUEF.mp2 [--codec mp2|ac3]
"""
import argparse, sys

GRID = {"mp2": (576, 24), "ac3": (1792, 32)}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ref"); ap.add_argument("test")
    ap.add_argument("--codec", default="mp2", choices=sorted(GRID))
    ap.add_argument("--points", type=int, default=9)
    args = ap.parse_args()

    fr, ms = GRID[args.codec]
    ref = open(args.ref, "rb").read()
    test = open(args.test, "rb").read()
    total_s = len(ref) // fr * ms / 1000.0

    print(f"{'t_ref [s]':>10} {'t_test [s]':>11} {'Delta [ms]':>12}  Treffer")
    deltas = []
    for k in range(args.points):
        t = total_s * (k + 0.5) / args.points
        off = (int(t * 1000) // ms) * fr
        pat = ref[off:off + 8192]
        if len(pat) < 8192:
            continue
        hits, s = [], 0
        while len(hits) <= 2:
            i = test.find(pat, s)
            if i < 0:
                break
            hits.append(i); s = i + 1
        if not hits:
            print(f"{t:10.1f} {'--':>11} {'kein Treffer':>12}")
            continue
        tt = hits[0] // fr * ms / 1000.0
        d = (tt - t) * 1000
        deltas.append(d)
        print(f"{t:10.1f} {tt:11.3f} {d:12.1f}  {len(hits)}")

    if not deltas:
        print("\nKein einziger Treffer - unterschiedliche Quellen?", file=sys.stderr)
        return 2
    spread = max(deltas) - min(deltas)
    print(f"\nSpanne der Deltas: {spread:.1f} ms")
    # Ein konstantes Delta ist ein reiner Versatz (z.B. anderer Trim). Ein
    # WACHSENDES Delta ist der Defekt, den dieses Werkzeug finden soll.
    print("Konstant = reiner Versatz; wachsend = Reparatur an falscher Stelle.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
