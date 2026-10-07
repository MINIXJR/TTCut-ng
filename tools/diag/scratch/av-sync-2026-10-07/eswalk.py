# Frame boundaries of an MP2/AC3 ES, resyncing on the sync word so junk bytes
# between frames are skipped (what ttcut-audiofix does). Returns the list of
# (pos, size) of valid frames.
KB = [32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640]
BR = [0,32,48,56,64,80,96,112,128,160,192,224,256,320,384]
SR = [44100, 48000, 32000]
def _size(d, p, fmt):
    if p + 7 >= len(d): return 0
    if fmt == "ac3":
        if d[p] != 0x0B or d[p+1] != 0x77: return 0
        c = d[p+4]
        if (c >> 6) != 0 or (c & 0x3F) >> 1 >= len(KB): return 0     # 48 kHz only
        return KB[(c & 0x3F) >> 1] * 4
    h = int.from_bytes(d[p:p+4], "big")
    if (h >> 21) != 0x7FF or ((h >> 17) & 3) != 2: return 0          # sync, layer II
    bi = (h >> 12) & 15; si = (h >> 10) & 3
    if bi == 0 or bi == 15 or si == 3: return 0
    return 144 * BR[bi] * 1000 // SR[si] + ((h >> 9) & 1)
def walk(d, fmt):
    out = []; p = 0; n = len(d)
    while p < n:
        s = _size(d, p, fmt)
        if s and (p + s >= n or _size(d, p + s, fmt)):
            out.append((p, s)); p += s
        else:
            p += 1                                                   # junk: resync byte by byte
    return out
