#!/usr/bin/env python3
"""
burst_analysis.py — Vermessung der TTCut-ng Audio-Burst-Erkennung.

Bildet die Fenster- und Chunk-Logik von TTFFmpegWrapper::detectAudioBurst nach,
um Schwellwerte an realem Material zu belegen statt zu schaetzen. Ergaenzt
`ttcut-burst-probe`: Das C++-Tool prueft EINE Grenze mit dem echten Detektor,
dieses Skript scannt einen ganzen Stream und findet Kandidatenstellen.

Fensterdefinition (extern/ttffmpegwrapper.cpp):
    CutOut: [boundary - 0.200, boundary + frameDur/2)
    CutIn : [boundary - frameDur/2, boundary + 0.200)
Ein Frame liegt im Fenster, wenn
    frameTime + frameDur > windowStart  UND  frameTime < windowEnd
(ein Frame, der vor windowStart beginnt und hineinragt, zaehlt mit).

Grenzzeit (data/ttavdata.cpp):
    cutOutTime = (cutOutIndex + 1 - extraFrames) / frameRate

Modi:
    dump <audio> <cutIndex...>   Chunk-fuer-Chunk um Schnittgrenzen
    scan <audio>                 ganzer Stream, findet Kandidaten fuer Gate-Tests

Siehe docs/superpowers/specs/2026-07-08-burst-detection-unified-threshold-design.md
"""
import argparse
import os
import statistics
import subprocess
import sys

CTX_SEC = 0.200          # Kontextfenster im Detektor
RMS_FLOOR_DB = -120.0    # detectAudioBurst: rms <= 0 -> -120 dB


def read_rms(path, start=None, duration=None):
    """RMS pro Audio-Frame via ffmpeg astats. -> [(t_sec, rms_db), ...]

    astats' Overall.RMS_level ist 20*log10(sqrt(sum(v^2)/N)) ueber alle Kanaele
    in dBFS -- dieselbe Formel wie detectAudioBurst.
    """
    cmd = ["ffmpeg", "-v", "error"]
    if start is not None:
        cmd += ["-ss", f"{start:.6f}"]
    if duration is not None:
        cmd += ["-t", f"{duration:.6f}"]
    cmd += ["-i", path,
            "-af", "astats=metadata=1:reset=1,"
                   "ametadata=print:key=lavfi.astats.Overall.RMS_level:file=-",
            "-f", "null", "/dev/null"]

    # LC_ALL=C: libavfilter gibt Zahlen sonst mit Komma aus (deutsche Locale)
    p = subprocess.run(cmd, capture_output=True, text=True,
                       env=dict(os.environ, LC_ALL="C"))
    if p.returncode != 0:
        sys.exit(f"ffmpeg failed:\n{p.stderr[:400]}")

    out, t = [], None
    for line in p.stdout.splitlines():
        line = line.strip()
        if line.startswith("frame:"):
            for tok in line.split():
                if tok.startswith("pts_time:"):
                    t = float(tok.split(":", 1)[1])
        elif line.startswith("lavfi.astats.Overall.RMS_level=") and t is not None:
            v = line.split("=", 1)[1]
            rms = RMS_FLOOR_DB if v in ("-inf", "inf", "nan") else float(v)
            out.append(((start or 0.0) + t, rms))
            t = None

    # Bei -ss ist der erste Frame oft ein Teilframe -> verwerfen
    if start is not None and len(out) > 1:
        out = out[1:]
    return out


def window_bounds(boundary, frame_dur, is_cut_out):
    tail = frame_dur / 2.0
    if is_cut_out:
        return boundary - CTX_SEC, boundary + tail
    return boundary - tail, boundary + CTX_SEC


def chunks_in_window(chunks, lo, hi, frame_dur):
    """Detektor-Kriterium, nicht 'lo <= t < hi'."""
    return [(t, r) for (t, r) in chunks if t + frame_dur > lo and t < hi]


def cmd_dump(args):
    frame_dur = args.frame_dur
    for idx in args.cut_index:
        boundary = (idx + 1) / args.fps          # wie ttavdata.cpp: index + 1
        lo, hi = window_bounds(boundary, frame_dur, not args.cutin)
        kind = "CutIn" if args.cutin else "CutOut"

        chunks = read_rms(args.audio, lo - 0.40, (hi + 0.15) - (lo - 0.40))
        win = chunks_in_window(chunks, lo, hi, frame_dur)

        print(f"=== {kind}-Index {idx} ===")
        print(f"  boundaryTime = (index+1)/fps = {boundary:.3f} s")
        print(f"  Fenster [{lo:.3f}, {hi:.3f})")
        if len(win) < 3:
            print(f"  nur {len(win)} chunks -> Detektor liefert false (need >=3)\n")
            continue

        median = statistics.median([r for _, r in win])
        tested = win[-2:] if not args.cutin else win[:2]
        peak = max(r for _, r in tested)
        print(f"  Kontext-Median {median:.2f} dB ueber {len(win)} chunks")
        print(f"  geprueft: {len(tested)} chunks, Peak {peak:.2f} dB, "
              f"Delta {peak - median:.2f} dB\n")

        print("     t [s]     rms [dB]  delta [dB]  Fenster  geprueft")
        for t, r in chunks:
            in_win = (t, r) in win
            is_tested = (t, r) in tested
            d = r - median
            print(f"  {t:9.3f}  {r:9.2f}  {d:9.2f}"
                  f"  {'  [W]  ' if in_win else '       '}"
                  f"  {'[P]' if is_tested else ''}")
        print()


def cmd_scan(args):
    """Findet Kandidatenstellen fuer die Verifikations-Gates der Spec."""
    frame_dur = args.frame_dur
    chunks = read_rms(args.audio)
    n_ctx = max(3, round(CTX_SEC / frame_dur))
    print(f"{len(chunks)} chunks ({chunks[-1][0]:.1f} s), Kontext = {n_ctx} chunks\n")

    mid_delta, below_floor = [], []
    for i in range(n_ctx, len(chunks) - 1):
        median = statistics.median([r for _, r in chunks[i - n_ctx:i + 1]])
        peak = max(chunks[i][1], chunks[i + 1][1])   # zwei geprueften Chunks
        delta = peak - median

        # Kandidat fuer 'untere Reglerhaelfte' nur, wenn der Peak das absolute Gate
        # ueberhaupt passiert -- sonst liefert der Detektor present=0 aus dem falschen
        # Grund und der Test beweist nichts.
        if args.gate_lo <= delta < args.gate_hi and peak > args.floor:
            mid_delta.append((chunks[i][0], peak, median, delta))

        if delta >= args.gate_hi and peak < args.floor:
            below_floor.append((chunks[i][0], peak, median, delta))

    print(f"Gate 'untere Reglerhaelfte' — Stellen mit {args.gate_lo} <= Delta < "
          f"{args.gate_hi} dB UND Peak > {args.floor} dB: {len(mid_delta)}")
    for t, p, m, d in mid_delta[:args.limit]:
        print(f"  boundary~{t:9.3f}s  peak={p:7.2f}  ctx={m:7.2f}  delta={d:6.2f}")

    print(f"\nGate 'Absolut-Gate wirkt' — Delta >= {args.gate_hi} dB, aber "
          f"Peak < {args.floor} dB: {len(below_floor)}")
    for t, p, m, d in below_floor[:args.limit]:
        print(f"  boundary~{t:9.3f}s  peak={p:7.2f}  ctx={m:7.2f}  delta={d:6.2f}")

    if not below_floor:
        print("  (keine — das Absolut-Gate weist auf diesem Material nichts ab)")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--frame-dur", type=float, default=1536 / 48000.0,
                    help="Audio-Framedauer in s (AC3@48k=0.032, MP2@48k=0.024)")
    sub = ap.add_subparsers(dest="mode", required=True)

    d = sub.add_parser("dump", help="Chunk-Dump um Schnittgrenzen")
    d.add_argument("audio")
    d.add_argument("cut_index", type=int, nargs="+")
    d.add_argument("--fps", type=float, default=25.0)
    d.add_argument("--cutin", action="store_true", help="CutIn- statt CutOut-Semantik")
    d.set_defaults(func=cmd_dump)

    s = sub.add_parser("scan", help="ganzen Stream nach Gate-Kandidaten absuchen")
    s.add_argument("audio")
    s.add_argument("--gate-lo", type=float, default=10.0,
                   help="untere Delta-Grenze fuer Kandidatensuche")
    s.add_argument("--gate-hi", type=float, default=20.0,
                   help="obere Delta-Grenze; zugleich Schwelle fuer den Gate-Test")
    s.add_argument("--floor", type=float, default=-40.0,
                   help="absolutes Gate des Detektors (kBurstAbsoluteFloorDb)")
    s.add_argument("--limit", type=int, default=8)
    s.set_defaults(func=cmd_scan)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
