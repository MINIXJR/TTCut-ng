#!/usr/bin/env python3
"""A/V-Sync-Messung: ES-Audio (AC3/MP2) gegen Original-TS an Sample-Punkten.

Methode:
  - sig: 4s Audio aus dem ES an ES-Zeit T (byte-genau via CBR-Framegrenzen, dd-artig).
  - ref: 14s Audio aus dem Original-TS um die erwartete Source-PTS
         (ffmpeg -copyts + atrim, sample-genau an absoluter PTS).
  - Normalisierte Kreuzkorrelation: Lag e = gefundene Position - erwartete Position.
    e > 0: Ton laeuft dem Bild VORAUS; e < 0: Ton HINTERHER.

Referenz-Zeitbasis:
  ES-Videoframe 0 = Source-PTS FIRST_VIDEO_PTS (90342.042989).
  Hinter der Video-Luecke (ES-Frame >= 828) fehlen 62 Frames -> Source-Zeit +1.24s.
"""
import subprocess, sys, os
import numpy as np

TS   = "/media/Daten/Video_Tmp/temp/Serien/Tatort/1974x03_-_Gerber_-_02_-_Playback_oder_die_Show_geht_weiter_TV/2026-08-05.23.15.55-0.rec/00001.ts"
BASE = "/media/Daten/Video_Tmp/ProjectX_Temp/1974x03_-_Gerber_-_02_-_Playback_oder_die_Show_geht_weiter_TV"

FIRST_VIDEO_PTS = 90342.042989
FMT_START       = 90341.310989
GAP_ES_TIME     = 828 / 50.0      # ab hier fehlen 62 Frames im Video-ES
GAP_SECONDS     = 62 / 50.0       # 1.24 s

SR = 8000  # Analyse-Samplerate

TRACKS = {
    # name: (es_file, ts_stream_index, frame_bytes, frame_dur_s)
    "ac3_deu": (BASE + "_deu.ac3", 4, 1792, 0.032),
    "mp2_deu": (BASE + "_deu.mp2", 1, 576, 0.024),
}

def run(cmd):
    r = subprocess.run(cmd, capture_output=True)
    if r.returncode != 0:
        raise RuntimeError("cmd failed: %s\n%s" % (" ".join(cmd), r.stderr.decode()[-800:]))
    return r.stdout

def decode_es_chunk(es_file, frame_bytes, frame_dur, t_start, dur):
    """Byte-genaue Extraktion aus CBR-ES, dann decode zu f32 mono 8k.
    Rueckgabe: (samples, tatsaechliche Startzeit des ersten extrahierten Frames)."""
    frame0 = int(t_start / frame_dur)
    nframes = int(dur / frame_dur) + 1
    with open(es_file, "rb") as f:
        f.seek(frame0 * frame_bytes)
        raw = f.read(nframes * frame_bytes)
    p = subprocess.run(
        ["ffmpeg", "-v", "error", "-f", "ac3" if frame_bytes == 1792 else "mp3",
         "-i", "pipe:0", "-f", "f32le", "-ac", "1", "-ar", str(SR), "pipe:1"],
        input=raw, capture_output=True)
    if p.returncode != 0:
        raise RuntimeError("es decode failed: " + p.stderr.decode()[-400:])
    return np.frombuffer(p.stdout, dtype=np.float32), frame0 * frame_dur

def decode_ts_chunk(stream_idx, abs_start, abs_end):
    """Sample-genau aus dem TS an absoluter PTS via -copyts + atrim."""
    seek = abs_start - FMT_START - 3.0
    out = run(["ffmpeg", "-v", "error", "-ss", "%.3f" % seek, "-copyts", "-i", TS,
               "-map", "0:%d" % stream_idx,
               "-af", "atrim=start=%.6f:end=%.6f" % (abs_start, abs_end),
               "-f", "f32le", "-ac", "1", "-ar", str(SR), "pipe:1"])
    return np.frombuffer(out, dtype=np.float32)

def xcorr_offset(sig, ref):
    """Position von sig in ref via normalisierter Kreuzkorrelation (FFT).
    Rueckgabe: (offset_seconds, peak_quality)."""
    s = sig - sig.mean(); r = ref - ref.mean()
    n = len(r) + len(s)
    S = np.fft.rfft(s[::-1], n); R = np.fft.rfft(r, n)
    corr = np.fft.irfft(R * S, n)[len(s)-1:len(r)]
    # Normierung: lokale Energie des ref-Fensters
    e = np.cumsum(r*r); win = len(s)
    loc = e[win-1:] - np.concatenate(([0.0], e[:-win]))
    denom = np.sqrt(loc * (s*s).sum()) + 1e-9
    ncc = corr[:len(loc)] / denom
    k = int(np.argmax(ncc))
    return k / SR, float(ncc[k])

def main():
    points = [10, 60, 300, 900, 1800, 3600, 5400, 6600, 7100]
    print("%-8s %-8s %10s %10s %8s" % ("track", "T_es[s]", "err[ms]", "peakNCC", "note"))
    for name, (es_file, ts_idx, fb, fd) in TRACKS.items():
        for T in points:
            gap = GAP_SECONDS if T >= GAP_ES_TIME else 0.0
            p_abs = FIRST_VIDEO_PTS + T + gap        # erwartete Source-PTS des Contents bei ES-Zeit T
            try:
                sig, sig_t0 = decode_es_chunk(es_file, fb, fd, T, 4.0)
                ref = decode_ts_chunk(ts_idx, p_abs - 5.0, p_abs + 9.0)
                if len(ref) < len(sig) + SR:
                    print("%-8s %-8d %10s %10s %8s" % (name, T, "-", "-", "ref_kurz")); continue
                off, q = xcorr_offset(sig[:4*SR], ref)
                # sig beginnt tatsaechlich bei ES-Zeit sig_t0; erwartete Position in ref:
                expected = 5.0 + (sig_t0 - T)
                err_ms = (off - expected) * 1000.0
                note = "" if q > 0.5 else "schwach"
                print("%-8s %-8d %10.1f %10.3f %8s" % (name, T, err_ms, q, note))
            except Exception as ex:
                print("%-8s %-8d  FEHLER: %s" % (name, T, str(ex)[:120]))
        print()

if __name__ == "__main__":
    main()
