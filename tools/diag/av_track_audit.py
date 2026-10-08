#!/usr/bin/env python3
"""Sound against picture along a whole demuxed recording, from timestamps
alone - no cut, no decoding.

Method: every frame of an audio ES is mapped onto the packet of the original
with the same payload (MD5) and so gets that packet's PTS. Its place on the
picture timeline is the display rank of the picture at that PTS times the
frame duration: the video ES holds the pictures in display order, frame 0 is
the first packet for H.264/H.265 (earlier-displayed leading pictures are
dropped) and the smallest PTS for MPEG-2. A sustained backward jump of the
PTS starts a new run; the two field packets of an H.264 PAFF frame are one
picture; a single stray PTS goes back to the place it left. The reading per frame is

    ES time (frame index x frame duration) - picture time of its PTS

in ms; negative = the frame plays before the picture it belongs to. Frames
without a unique match (inserted silence, repeated payloads) are skipped.
The .info start offset of the track is printed next to it: reading + offset
is what TTCut-ng cuts with.

The walk reads the ES by its own frame parser. TTCut-ng cuts with libav, so
the audit also checks that libav reads the same frames: as many packets, the
first at the same byte. (libav skips the first MP2 frame of a file when the
second frame's header differs in mode, copyright, original or emphasis -
every timestamp it reports is then one frame early.)

Exit: 0 every section of at least 20 frames is within the bound,
      1 a section is outside, 2 the measurement failed (no frame matched).

--check-offset additionally requires, per track, that the first section's
reading plus the .info start offset is 0 within 1 ms: the offset the demuxer
wrote is the one this independent walk finds.

usage: av_track_audit.py FPS ESDIR BASENAME SEGMENT.ts [SEGMENT.ts ...]
                         [--bound-frames X | --bound-ms N] [--check-offset]
"""
import argparse
import bisect
import collections
import hashlib
import os
import subprocess
import sys

ENV = dict(os.environ, LC_ALL="C")
MIN_SECTION = 20
AC3_KBPS = [32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 576, 640]
MP2_KBPS = [0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384]
MP2_RATE = [44100, 48000, 32000]
FRAME_S = {"mp2": 0.024, "ac3": 0.032}       # 48 kHz


def frame_size(d, p, codec):
    if p + 7 >= len(d):
        return 0
    if codec == "ac3":
        if d[p] != 0x0B or d[p + 1] != 0x77:
            return 0
        c = d[p + 4]
        if (c >> 6) != 0 or (c & 0x3F) >> 1 >= len(AC3_KBPS):      # 48 kHz only
            return 0
        return AC3_KBPS[(c & 0x3F) >> 1] * 4
    h = int.from_bytes(d[p:p + 4], "big")
    if (h >> 21) != 0x7FF or ((h >> 17) & 3) != 2:                 # sync word, layer II
        return 0
    bi, si = (h >> 12) & 15, (h >> 10) & 3
    if bi in (0, 15) or si == 3:
        return 0
    return 144 * MP2_KBPS[bi] * 1000 // MP2_RATE[si] + ((h >> 9) & 1)


def es_frames(d, codec):
    """(pos, size) of the valid frames, resyncing byte by byte over junk."""
    out, p, n = [], 0, len(d)
    while p < n:
        s = frame_size(d, p, codec)
        if s and (p + s >= n or frame_size(d, p + s, codec)):
            out.append((p, s))
            p += s
        else:
            p += 1
    return out


def libav_view(path):
    """(packet count, byte position of the first packet) as libav reads the ES."""
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "packet=pos",
                          "-of", "csv=p=0", path], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                         env=ENV).stdout.decode().split()
    pos = [int(x.rstrip(",")) for x in out if x.rstrip(",").lstrip("-").isdigit()]
    return len(pos), (pos[0] if pos else -1)


def probe(segments):
    """Video PTS in stream order, {audio stream: [(md5, pts)]}, video codec."""
    video, audio, vcodec = [], collections.defaultdict(list), ""
    for seg in segments:
        kinds = {}
        for l in subprocess.run(["ffprobe", "-v", "error", "-show_entries", "stream=index,codec_type,codec_name",
                                 "-of", "csv=p=0", seg], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                env=ENV).stdout.decode().splitlines():
            f = l.strip().rstrip(",").split(",")
            if len(f) >= 3 and f[0].isdigit():
                kinds[int(f[0])] = (f[2], f[1])                   # ffprobe prints index,codec_name,codec_type
        vidx = min(i for i, (t, _) in kinds.items() if t == "video")
        vcodec = kinds[vidx][1]
        pr = subprocess.Popen(["ffprobe", "-v", "error", "-show_packets", "-show_data_hash", "md5", "-show_entries",
                               "packet=stream_index,pts_time,data_hash", "-of", "default=nw=1", seg],
                              stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, env=ENV)
        cur = {}
        for l in pr.stdout:
            k, _, v = l.rstrip("\n").partition("=")
            cur[k] = v
            if k == "data_hash":
                if cur.get("pts_time", "N/A") != "N/A":
                    i, p = int(cur["stream_index"]), float(cur["pts_time"])
                    if i == vidx:
                        video.append(p)
                    elif kinds.get(i, ("", ""))[0] == "audio":
                        audio[i].append((v.split(":")[-1].lower(), p))
                cur = {}
        pr.wait()
    return video, audio, vcodec


def split_runs(pts, back=0.5, sustain=10):
    runs = [[]]
    for i, p in enumerate(pts):
        if len(runs[-1]) >= 12:
            ref = sorted(runs[-1][-12:])[-3]
            if p < ref - back and all(q < ref - back for q in pts[i:i + sustain]):
                runs.append([])
        runs[-1].append(p)
    return runs


def without_strays(run, dur):
    """Sorted PTS of one run. A stray PTS (one corrupt packet: more than 1 s
    from the median of up to 6 pictures before AND behind it in stream order)
    is no place on the timeline; the picture goes back into the free place
    nearest to its neighbours when there is one within 1 s."""
    def median(vals):
        return sorted(vals)[len(vals) // 2] if len(vals) >= 3 else None
    good, strays = [], []
    for i, p in enumerate(run):
        before, behind = median(run[max(0, i - 6):i]), median(run[i + 1:i + 7])
        far = [abs(p - m) > 1.0 for m in (before, behind) if m is not None]
        if far and all(far):
            sides = [m for m in (before, behind) if m is not None]
            strays.append(min(sides, key=lambda m: abs(p - m)))
        else:
            good.append(p)
    s = sorted(set(good))
    for level in strays:
        free = [s[i] + k * dur for i in range(len(s) - 1) if s[i + 1] - s[i] > 1.5 * dur
                for k in range(1, int(round((s[i + 1] - s[i]) / dur)))]
        free = [c for c in free if abs(c - level) < 1.0]
        if free:
            s = sorted(s + [min(free, key=lambda c: abs(c - level))])
    return s


class PictureTimeline:
    def __init__(self, video_pts, h26x, dur):
        self.dur, self.runs, self.base = dur, [], []
        n = 0
        for r in split_runs(video_pts):
            s = without_strays(r, dur)
            if h26x and not self.runs:
                s = [p for p in s if p >= r[0] - 1e-6]             # cold start: leading pictures dropped
            pics = []
            for p in s:                                            # PAFF: second field = same picture
                if not pics or p - pics[-1] >= 0.75 * dur:
                    pics.append(p)
            self.runs.append(pics)
            self.base.append(n)
            n += len(pics)
        self.count = n
        self.holes = [(s[i], round((s[i + 1] - s[i]) / dur) - 1)
                      for s in self.runs for i in range(len(s) - 1) if round((s[i + 1] - s[i]) / dur) != 1]

    def time_of(self, pts):
        r = next((j for j, s in enumerate(self.runs) if s and s[0] - 1 <= pts <= s[-1] + 1), 0)
        s = self.runs[r]
        i = bisect.bisect_right(s, pts + 1e-6) - 1
        if i < 0:
            return pts - s[0]
        return (self.base[r] + i) * self.dur + min(pts - s[i], self.dur)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("fps", type=float, help="frame rate of the video ES (frames, not fields)")
    ap.add_argument("esdir", help="directory with the demuxed ES set")
    ap.add_argument("basename", help="base name: <basename>.info lists the audio files")
    ap.add_argument("segments", nargs="+", help="the original recording, every segment in order")
    ap.add_argument("--bound-frames", type=float, default=1.0,
                    help="bound in audio frames of each track, plus 1 ms (default 1.0)")
    ap.add_argument("--bound-ms", type=float, default=None, help="bound in ms for every track")
    ap.add_argument("--check-offset", action="store_true",
                    help="require first reading + .info start offset = 0 within 1 ms")
    a = ap.parse_args()

    video, audio, vcodec = probe(a.segments)
    tl = PictureTimeline(video, vcodec in ("h264", "hevc"), 1.0 / a.fps)
    lost = sum(h[1] for h in tl.holes) * tl.dur * 1000
    print(f"picture: {len(video)} packets, {len(tl.runs)} run(s), {tl.count} pictures, "
          f"{len(tl.holes)} hole(s) = {lost:.0f} ms")
    info = dict(l.strip().split("=", 1) for l in open(os.path.join(a.esdir, a.basename + ".info"), encoding="utf-8")
                if "=" in l and not l.startswith("#"))
    rc, measured, n = 0, 0, 0
    while f"audio_{n}_file" in info:
        name, codec = info[f"audio_{n}_file"], info[f"audio_{n}_codec"]
        offset = info.get(f"audio_{n}_start_offset_ms", "not written")
        n += 1
        if codec not in FRAME_S:
            print(f"{name}: codec {codec} not checked")
            continue
        fd = FRAME_S[codec]
        data = open(os.path.join(a.esdir, name), "rb").read()
        frames = es_frames(data, codec)
        hashes = [hashlib.md5(data[p:p + s]).hexdigest() for p, s in frames]
        n_libav, first_libav = libav_view(os.path.join(a.esdir, name))
        libav_note = ""
        if frames and (n_libav != len(frames) or first_libav != frames[0][0]):
            libav_note = (f", LIBAV READS {n_libav} PACKETS FROM BYTE {first_libav}"
                          f" (frame walk: {len(frames)} from byte {frames[0][0]})")
        best = None
        for idx, packets in audio.items():                         # the source track with the most frames in common
            table = collections.defaultdict(list)
            for h, p in packets:
                table[h].append(p)
            hits = sum(1 for h in hashes[::50] if h in table)
            if best is None or hits > best[0]:
                best = (hits, table)
        table = best[1] if best else {}
        sections, matched = [], 0
        for k, h in enumerate(hashes):
            pts = table.get(h)
            if not pts or len(pts) != 1:
                continue
            matched += 1
            err = (k * fd - tl.time_of(pts[0])) * 1000
            if sections and abs(err - sections[-1][0]) < 1.0:
                sections[-1][2] = k * fd
                sections[-1][3] += 1
            else:
                sections.append([err, k * fd, k * fd, 1])
        big = [s for s in sections if s[3] >= MIN_SECTION]
        if not big:
            print(f"{name}: {len(hashes)} frames, {matched} matched - no section of {MIN_SECTION} frames")
            rc = max(rc, 2)
            continue
        measured += 1
        lo, hi = min(s[0] for s in big), max(s[0] for s in big)
        bound = a.bound_ms if a.bound_ms is not None else a.bound_frames * fd * 1000 + 1.0
        verdict = ("ok" if max(abs(lo), abs(hi)) <= bound else "OUTSIDE") + libav_note
        if a.check_offset:
            try:
                if abs(big[0][0] + int(offset)) > 1.0:
                    verdict += f", START OFFSET WRONG (first reading {big[0][0]:+.1f} ms)"
            except ValueError:
                verdict += ", START OFFSET MISSING"
        if verdict != "ok":
            rc = max(rc, 1)
        print(f"{name}: {len(hashes)} frames, {matched} matched, {len(big)} section(s); sound against picture "
              f"min {lo:+.0f}, max {hi:+.0f} ms; .info start offset {offset} ms  [bound +-{bound:.0f} ms: {verdict}]")
        print("   course: " + " | ".join(f"{s[0]:+.0f} ms from {s[1]:.0f} s" for s in big[:40])
              + (" | ..." if len(big) > 40 else ""))
    return rc if measured or rc else 2


if __name__ == "__main__":
    sys.exit(main())
