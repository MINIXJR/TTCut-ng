# Is ES B equal to ES A apart from a shift at the head? usage: escmp.py A B
import sys
a = open(sys.argv[1], "rb").read(); b = open(sys.argv[2], "rb").read()
def find(x, y):                      # position of y's head inside x's first 2 MB
    return x.find(y[:4096], 0, 2_000_000)
p = find(a, b); q = find(b, a)
if p < 0 and q < 0: print(f"   kein gemeinsamer Anfang gefunden (A {len(a)} Bytes, B {len(b)} Bytes)"); sys.exit()
if p >= 0: x, y, txt = a[p:], b, f"B beginnt bei A-Byte {p}"
else:      x, y, txt = a, b[q:], f"A beginnt bei B-Byte {q}"
m = min(len(x), len(y))
if x[:m] == y[:m]: print(f"   {txt}; gemeinsamer Teil ({m} Bytes) gleich; Rest A {len(x)-m}, Rest B {len(y)-m} Bytes")
else:
    i = next(k for k in range(0, m, 4096) if x[k:k+4096] != y[k:k+4096])
    print(f"   {txt}; erster Unterschied nach {i} von {m} Bytes ({100.0*i/m:.2f} %)")
