#!/usr/bin/env python3
"""End-to-end A/V check: how far does each audio track of a cut MKV sit from
the picture, compared with the original recording?

Method: the pictures of the MKV are found in the original by the MD5 of the
decoded frame, the audio packets by the MD5 of their payload. Both give a
time shift MKV minus original; an audio packet's offset is its shift minus
the shift of the picture next to it. Positive = the sound comes later
against the picture than in the original. Only content with a unique MD5
counts (still pictures and silence drop out). At a discontinuity of the
original the picture on the packet's own side of the break is taken (see
picture_shift).

Control (--control): the original is remuxed with "ffmpeg -c copy" into a
temporary MKV and measured the same way; every track must read 0 within
1 ms (Matroska stores timestamps in whole milliseconds, a PTS of x.8 ms
reads +0.2), or the measurement itself is wrong.

Exit: 0 every section of at least 20 packets is within the bound,
      1 a section is outside, 2 the measurement failed (nothing matched).

usage: av_chain_check.py ORIGINAL_TS [MKV] [--bound-frames X | --bound-ms N] [--control]
"""
import argparse
import bisect
import collections
import os
import statistics
import subprocess
import sys
import tempfile

ENV = dict(os.environ, LC_ALL="C")
MIN_SECTION = 20        # packets; shorter sections are seam effects, not a reading
CONTROL_BOUND_MS = 1.0  # Matroska timestamps are whole milliseconds
SEAM_WINDOW_S = 1.0     # how far from a packet the picture on its side of a break is looked for


def run(*cmd):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=ENV).stdout.decode()


def video_frames(path):
    """(md5, pts) of every decoded picture."""
    md5 = [l.split(",")[-1].strip()
           for l in run("ffmpeg", "-v", "error", "-i", path, "-map", "0:v:0", "-fps_mode", "passthrough",
                        "-f", "framemd5", "-").splitlines() if l and not l.startswith("#")]
    pts = []
    for l in run("ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                 "frame=best_effort_timestamp_time", "-of", "default=nw=1:nk=1", path).split():
        try:
            pts.append(float(l))
        except ValueError:
            pass
    if len(md5) != len(pts):
        sys.exit(f"{path}: {len(md5)} picture MD5 against {len(pts)} timestamps - measurement setup broken")
    return list(zip(md5, pts))


def audio_packets(path):
    """{stream index: [(md5, pts), ...]} of every audio packet with a PTS."""
    out = collections.defaultdict(list)
    cur = {}
    for l in run("ffprobe", "-v", "error", "-select_streams", "a", "-show_packets", "-show_data_hash", "md5",
                 "-show_entries", "packet=stream_index,pts_time,data_hash", "-of", "default=nw=1", path).splitlines():
        k, _, v = l.partition("=")
        cur[k] = v
        if k == "data_hash":
            if cur.get("pts_time", "N/A") != "N/A":
                out[int(cur["stream_index"])].append((v, float(cur["pts_time"])))
            cur = {}
    return out


def unique(pairs):
    pairs = list(pairs)
    count = collections.Counter(h for h, _ in pairs)
    return {h: p for h, p in pairs if count[h] == 1}


def frame_ms(pairs):
    """Frame duration of a track in ms: the median PTS step of its packets."""
    pts = sorted(p for _, p in pairs)
    steps = [b - a for a, b in zip(pts, pts[1:]) if b > a]
    return statistics.median(steps) * 1000 if steps else 0.0


def picture_shift(shifts, keys, at, audio_shift):
    """Shift of the picture next to MKV time `at`. Where the original has a
    discontinuity (a seam between files, a signal loss), pictures before and
    behind it have different shifts and sound and picture do not break at
    the same packet: of the pictures within SEAM_WINDOW_S the one whose
    shift is closest to the audio packet's own is on its side of the break."""
    lo = bisect.bisect_left(keys, at - SEAM_WINDOW_S)
    hi = bisect.bisect_right(keys, at + SEAM_WINDOW_S)
    if lo >= hi:
        return shifts[min(max(bisect.bisect_left(keys, at), 0), len(shifts) - 1)][1]
    return min((shifts[i][1] for i in range(lo, hi)), key=lambda v: abs(audio_shift - v))


def measure(ts_video, ts_audio, names, mkv, bound_frames, bound_ms, label):
    """Prints one line per audio track of mkv; returns 0 / 1 / 2 like the exit code."""
    mkv_pics = video_frames(mkv)
    tv = unique(ts_video)
    mv = unique(mkv_pics)
    shifts = sorted((mv[h], mv[h] - tv[h]) for h in mv if h in tv)
    if not shifts:
        print(f"{label}no picture of the MKV found in the original")
        return 2
    top = collections.Counter(round(s * 1000, 1) for _, s in shifts).most_common(3)
    print(f"{label}picture: {len(shifts)} of {len(mkv_pics)} MKV pictures found in the original; shift {dict(top)}")
    keys = [x[0] for x in shifts]
    rc = 0
    measured = 0
    for idx, pairs in sorted(audio_packets(mkv).items()):
        ma = unique(pairs)
        best = None
        for j, t in ts_audio.items():
            n = sum(1 for h in ma if h in t)
            if n and (best is None or n > best[1]):
                best = (j, n)
        if not best:
            print(f"{label}audio MKV track {idx}: no packet found in the original")
            rc = max(rc, 2)
            continue
        t = ts_audio[best[0]]
        fd = frame_ms(pairs)
        bound = bound_ms if bound_ms is not None else bound_frames * fd + 1.0
        sections = []
        for h, pm in sorted(ma.items(), key=lambda x: x[1]):
            if h not in t:
                continue
            off = round((pm - t[h] - picture_shift(shifts, keys, pm, pm - t[h])) * 1000, 1)
            if sections and sections[-1][0] == off:
                sections[-1][2] = pm
                sections[-1][3] += 1
            else:
                sections.append([off, pm, pm, 1])
        sections = [s for s in sections if s[3] >= MIN_SECTION]
        if not sections:
            print(f"{label}audio {names.get(best[0], best[0])}: no section of {MIN_SECTION} packets")
            rc = max(rc, 2)
            continue
        measured += 1
        worst = max(abs(s[0]) for s in sections)
        verdict = "ok" if worst <= bound else "OUTSIDE"
        if worst > bound:
            rc = max(rc, 1)
        print(f"{label}audio {names.get(best[0], best[0]):9s}: "
              + "; ".join(f"{s[0]:+.1f} ms (MKV {s[1]:.1f}-{s[2]:.1f} s, {s[3]} packets)" for s in sections)
              + f"  [bound +-{bound:.0f} ms: {verdict}]")
    return rc if measured or rc else 2


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("ts", help="original recording (one TS file)")
    ap.add_argument("mkv", nargs="?", help="cut MKV to check")
    ap.add_argument("--bound-frames", type=float, default=0.5,
                    help="bound in audio frames of each track, plus 1 ms (default 0.5)")
    ap.add_argument("--bound-ms", type=float, default=None, help="bound in ms for every track (overrides --bound-frames)")
    ap.add_argument("--control", action="store_true", help="measure a plain remux of the original first (must be 0.0)")
    a = ap.parse_args()
    if not a.mkv and not a.control:
        ap.error("give an MKV, --control, or both")
    ts_video = video_frames(a.ts)
    ts_audio = {i: unique(p) for i, p in audio_packets(a.ts).items()}
    names = {}
    for l in run("ffprobe", "-v", "error", "-select_streams", "a", "-show_entries",
                 "stream=index,codec_name:stream_tags=language", "-of", "csv=p=0", a.ts).splitlines():
        f = l.strip().rstrip(",").split(",")
        if len(f) >= 2 and f[0].isdigit():
            names[int(f[0])] = ",".join(f[1:])
    rc = 0
    if a.control:
        # next to the MKV, else in the working directory (never next to the
        # recording: that may be a read-only share)
        tmp_parent = os.path.dirname(os.path.abspath(a.mkv)) if a.mkv else os.getcwd()
        with tempfile.TemporaryDirectory(prefix="av-chain-", dir=tmp_parent) as tmp:
            remux = os.path.join(tmp, "control.mkv")
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", a.ts, "-map", "0:v:0", "-map", "0:a", "-c", "copy", remux],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=ENV)
            crc = measure(ts_video, ts_audio, names, remux, 0.0, CONTROL_BOUND_MS, "control: ")
        if crc != 0:
            print(f"control: the plain remux is not within {CONTROL_BOUND_MS:.0f} ms - the measurement is not usable")
            return 2
        print(f"control: within {CONTROL_BOUND_MS:.0f} ms on all tracks")
    if a.mkv:
        rc = measure(ts_video, ts_audio, names, a.mkv, a.bound_frames, a.bound_ms, "")
    return rc


if __name__ == "__main__":
    sys.exit(main())
