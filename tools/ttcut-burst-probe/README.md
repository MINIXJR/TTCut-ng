# ttcut-burst-probe

Ruft `TTAudioCutter::detectBurst` direkt auf — die echte Entscheidung des
Detektors an *einer* Schnittgrenze, ohne GUI.

## Bauen

Nicht Teil des Anwendungs-Builds (`EXCLUDE_FROM_ALL`; es kompiliert `extern/ttaudiocutter.cpp` und drei weitere Quellen erneut):

```bash
cmake --build build --target ttcut-burst-probe
```

## Aufruf

```bash
./ttcut-burst-probe <audio.ac3> <boundarySec> [--cutin] [--min-delta dB]
```

`--min-delta` ist der Mindestsprung über dem Umgebungspegel (Vorgabe 20, wie die
Einstellung der Anwendung; 0 schaltet die Erkennung ab).

Grenzzeit wie im Code (`data/ttavdata.cpp`):
`cutOutTime = (cutOutIndex + 1 - extraFrames) / frameRate`, `extraFrames = 0` außer
bei MPEG-2-Field-Pictures.

Ausgabe: `present=1 burstDb=-26.18 contextDb=-82.49 delta=56.31` bzw. `present=0`.
Exit 0 = Burst, 1 = kein Burst, 2 = Nutzungsfehler.

Bei `present=0` werden keine Pegel ausgegeben: `detectBurst` weist
`burstRmsDb`/`contextRmsDb` nur bei positiver Erkennung zu.

## Verhältnis zu `tools/burst-analysis/`

| Werkzeug | Zweck |
|---|---|
| `burst-analysis/burst_analysis.py` | Scannt einen ganzen Stream, findet Kandidatengrenzen. Bildet die Detektorlogik nach. |
| `ttcut-burst-probe` | Beantwortet für eine Grenze, was der Detektor *wirklich* entscheidet. Maßgeblich bei Abweichung. |
