# ttcut-burst-probe

Ruft `TTFFmpegWrapper::detectAudioBurst` direkt auf — die echte Entscheidung des
Detektors an *einer* Schnittgrenze, ohne GUI.

## Bauen

Nicht Teil des Anwendungs-Builds (es kompiliert `ttffmpegwrapper.cpp` erneut):

```bash
cd tools/ttcut-burst-probe && qmake && make
```

## Aufruf

```bash
./ttcut-burst-probe <audio.ac3> <boundarySec> [--cutin]
```

Grenzzeit wie im Code (`data/ttavdata.cpp`):
`cutOutTime = (cutOutIndex + 1 - extraFrames) / frameRate`, `extraFrames = 0` außer
bei MPEG-2-Field-Pictures.

Ausgabe: `present=1 burstDb=-26.18 contextDb=-82.49 delta=56.31` bzw. `present=0`.
Exit 0 = Burst, 1 = kein Burst, 2 = Nutzungsfehler.

Bei `present=0` werden keine Pegel ausgegeben: `detectAudioBurst` weist
`burstRmsDb`/`contextRmsDb` nur bei positiver Erkennung zu.

## Verhältnis zu `tools/burst-analysis/`

| Werkzeug | Zweck |
|---|---|
| `burst-analysis/burst_analysis.py` | Scannt einen ganzen Stream, findet Kandidatengrenzen. Bildet die Detektorlogik nach. |
| `ttcut-burst-probe` | Beantwortet für eine Grenze, was der Detektor *wirklich* entscheidet. Maßgeblich bei Abweichung. |
