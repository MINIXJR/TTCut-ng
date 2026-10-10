# Offset between the AC3 and an MP2 stream in the ORIGINAL transport stream,
# on the PTS axis (what a player presents): cut a 6 s clip of both streams as
# they are (stream copy, timestamps kept), read each stream's first packet PTS
# and packet spacing, decode both and find where 3 s of the AC3 centre (or L)
# sit in the MP2 mid (or L).
# usage: tslag.py TS AC3_INDEX MP2_INDEX FIRST_VIDEO_PTS offset_s [offset_s ...]
import subprocess, sys, os, numpy as np
TS, IA, IM, V0 = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]); OFFS = sys.argv[5:]
FS = 48000
def run(*a): return subprocess.run(a, stdout=subprocess.PIPE, check=True).stdout
def decode(path, ch, fmt):
    pre = ["-drc_scale", "0", "-f", "ac3"] if fmt == "ac3" else ["-f", "mp3"]
    return np.frombuffer(run("ffmpeg", "-v", "error", *pre, "-i", path, "-f", "f32le", "-"), dtype="<f4").reshape(-1, ch).astype(np.float64)
for off in OFFS:
    run("ffmpeg", "-v", "error", "-y", "-ss", off, "-t", "6", "-copyts", "-i", TS, "-map", f"0:{IA}", "-map", f"0:{IM}", "-c", "copy", "-f", "nut", "clip.nut")
    pk = {0: [], 1: []}
    for l in run("ffprobe", "-v", "error", "-show_entries", "packet=stream_index,pts_time", "-of", "csv=p=0", "clip.nut").decode().split():
        s, p = l.split(",")[:2]; pk[int(s)].append(float(p))
    run("ffmpeg", "-v", "error", "-y", "-i", "clip.nut", "-map", "0:0", "-c", "copy", "-f", "ac3", "clip.ac3")
    run("ffmpeg", "-v", "error", "-y", "-i", "clip.nut", "-map", "0:1", "-c", "copy", "-f", "mp2", "clip.mp2")
    hdr = open("clip.ac3", "rb").read(8); ch = 6 if (hdr[6] >> 5) == 7 else 2
    x = decode("clip.ac3", ch, "ac3"); y = decode("clip.mp2", 2, "mp2")
    spa = max(abs(d - 0.032) for d in np.diff(pk[0])) * 1000; spm = max(abs(d - 0.024) for d in np.diff(pk[1])) * 1000
    assert len(x) == len(pk[0]) * 1536 and len(y) == len(pk[1]) * 1152, (len(x), len(pk[0]), len(y), len(pk[1]))
    c = x[:, 2] if ch == 6 else x[:, 0]; d = y.sum(axis=1) / 2 if ch == 6 else y[:, 0]
    s0 = FS; seg = c[s0:s0 + 3 * FS]
    n = 1 << int(np.ceil(np.log2(len(d) + len(seg))))
    cross = np.fft.irfft(np.fft.rfft(d, n) * np.conj(np.fft.rfft(seg, n)), n)[:len(d) - len(seg) + 1]
    cs = np.concatenate(([0.0], np.cumsum(d ** 2))); e = cs[len(seg):] - cs[:len(cs) - len(seg)]
    cc = cross / (np.sqrt(e * np.dot(seg, seg)) + 1e-30); k = int(np.argmax(cc))
    lag = (pk[1][0] + k / FS) - (pk[0][0] + s0 / FS)
    print(f"  ab {off} s (ES-Zeit {pk[0][0] + 1 - V0:8.2f} s, {ch} Kanaele): erstes AC3-Paket PTS {pk[0][0]:.6f}, erstes MP2-Paket PTS {pk[1][0]:.6f}; "
          f"Paketabstand weicht hoechstens {spa:.3f} / {spm:.3f} ms ab; Versatz MP2 gegen AC3 {lag * 1000:+.1f} ms, Korrelation {cc[k]:.3f}")
for f in ("clip.nut", "clip.ac3", "clip.mp2"): os.remove(f)
