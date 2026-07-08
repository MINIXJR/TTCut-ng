# burst-analysis

Python-Werkzeug zur Vermessung der TTCut-ng Audio-Burst-Erkennung an realem
DVB-Material. Ergänzt `ttcut-burst-probe`:

| Werkzeug | Zweck |
|---|---|
| `ttcut-burst-probe` (C++) | Ruft `TTFFmpegWrapper::detectAudioBurst` direkt auf — die *echte* Entscheidung an *einer* Schnittgrenze. |
| `burst_analysis.py` | Bildet Fenster- und Chunk-Logik nach und scannt einen *ganzen Stream* nach Kandidatenstellen. Findet, was das C++-Tool dann verifiziert. |

Es ersetzt den Detektor nicht: `astats` liefert das RMS pro Audioframe, der Rest
(Fenstergrenzen, Kontext-Median, geprüfte Chunks) ist nachgebaut. Weichen die
Ergebnisse von `ttcut-burst-probe` ab, gilt das C++-Tool.

## Nachgebildete Logik

Fenster (`extern/ttffmpegwrapper.cpp`):

```
CutOut: [boundary - 0.200, boundary + frameDur/2)
CutIn : [boundary - frameDur/2, boundary + 0.200)
```

Ein Frame liegt im Fenster, wenn `frameTime + frameDur > windowStart && frameTime < windowEnd`
— ein Frame, der vor `windowStart` beginnt und hineinragt, **zählt mit**. Geprüft
werden die letzten zwei Chunks (CutOut) bzw. die ersten zwei (CutIn).

Grenzzeit (`data/ttavdata.cpp`): `cutOutTime = (cutOutIndex + 1 - extraFrames) / frameRate`.

## Verwendung

Chunk-Dump um Schnittgrenzen (Frame-Indizes, nicht Sekunden):

```bash
./burst_analysis.py dump recording_deu.ac3 27505 70788 119661
```

Ganzen Stream nach Kandidatenstellen für die Verifikations-Gates absuchen:

```bash
# Stellen, an denen die untere Reglerhaelfte (10..20 dB) greifen muss.
# Nur Kandidaten mit Peak ueber dem Gate werden gelistet -- darunter liefert
# der Detektor present=0 aus dem falschen Grund und der Test bewiese nichts.
./burst_analysis.py scan recording_deu.ac3 --gate-lo 10 --gate-hi 20

# Stellen, die das absolute Gate abweisen soll (relativ auffaellig, absolut unhoerbar)
./burst_analysis.py scan recording_deu.ac3 --gate-hi 20 --floor -40
```

`--floor` entspricht `kBurstAbsoluteFloorDb` im Detektor (Default −40 dB).
Kandidaten nahe dem Streambeginn meiden: Dort umfasst das Analysefenster unter
Umständen weniger als drei Chunks, und der Detektor liefert `false` wegen
`need >= 3` statt wegen der Schwelle.

Für MP2 statt AC3 die Framedauer angeben: `--frame-dur 0.024`.

## Hinweise

- `LC_ALL=C` wird intern gesetzt: libavfilter gibt Zahlen sonst mit Dezimalkomma aus.
- Ein Chunk mit `rms <= 0` meldet der Detektor als `-120 dB`; das Skript ebenso.

Siehe `docs/superpowers/specs/2026-07-08-burst-detection-unified-threshold-design.md`.
