# Security Audit Report — TTCut-ng (2026-03-02)

Umfassende Sicherheitspruefung des gesamten TTCut-ng Projekts. Desktop-Applikation
ohne Netzwerk-Angriffsflaeche — Hauptrisiko sind Crashes/DoS durch manipulierte
Mediendateien.

## CRITICAL

### C1: readBits() ohne Bounds-Check

**Datei:** `avstream/ttnaluparser.cpp:1118` und `extern/ttessmartcut.cpp:1952`

`readBits()` liest Bits aus Byte-Arrays ohne jegliche Laengenvalidierung.
Ein manipuliertes NAL-Unit mit Exp-Golomb-Code (viele fuehrende Nullen) liest
ueber das Buffer-Ende hinaus. Betrifft alle Slice-Header- und SPS-Parser.

```cpp
// Aktuell — kein Bounds-Check:
uint32_t TTNaluParser::readBits(const uint8_t* data, int& bitPos, int numBits)
{
    for (int i = 0; i < numBits; i++) {
        int byteIndex = bitPos / 8;
        value |= (data[byteIndex] >> bitIndex) & 1;  // OOB wenn byteIndex >= bufferSize
    }
}
```

**Fix:** Buffer-Size-Parameter hinzufuegen:
```cpp
uint32_t readBits(const uint8_t* data, int dataSize, int& bitPos, int numBits)
{
    for (int i = 0; i < numBits; i++) {
        int byteIndex = bitPos / 8;
        if (byteIndex >= dataSize) return value;  // Bounds-Check
        // ...
    }
}
```

Betrifft auch `readExpGolombUE()` und `readExpGolombSE()`. Alle Aufrufer muessen
die Buffer-Groesse mitgeben.

### C2: readExpGolombUE() Undefined Behavior bei Shift-by-32

**Datei:** `avstream/ttnaluparser.cpp:1085`

`(1u << leadingZeros)` ist Undefined Behavior wenn `leadingZeros == 32`
(32-Bit Integer um 32 Positionen shiften). Maximum auf 31 begrenzen oder
`uint64_t` verwenden.

---

## HIGH

### H1: AC3 frmsizecod Array-OOB

**Datei:** `avstream/ttac3audiostream.cpp:131`

`frmsizecod` ist 6-Bit (0-63), aber `AC3FrameLength[4][38]` hat nur 38 Spalten.
Werte >= 38 lesen ueber das Array-Ende hinaus.

**Fix:**
```cpp
if (audio_header->frmsizecod >= 38 || audio_header->fscod >= 3) {
    audio_header->syncframe_words = 0;
    return;
}
```

### H2: MPEG Audio frame_length Integer-Underflow

**Datei:** `avstream/ttmpegaudiostream.cpp:261`

Bei `frame_length == 0` oder `< 4`: `seekRelative(frame_length - 4)` erzeugt
unsigned Underflow → massiver Forward-Seek oder Endlosschleife.

**Fix:** `frame_length >= 4` validieren nach `parseAudioHeader()`.

### H3: seekBackward() Underflow

**Datei:** `avstream/ttfilebuffer.cpp:271`

`readPos -= offset` kann negative Werte erzeugen wenn `offset > readPos`.
Cast zu `quint64` in `seekAbsolute()` erzeugt dann riesigen Seek-Wert.

**Fix:**
```cpp
void TTFileBuffer::seekBackward(quint64 offset)
{
    if ((qint64)offset > readPos) readPos = 0;
    else readPos -= offset;
    seekAbsolute(readPos);
}
```

### H4: MPEG-2 Picture Extension Endlosschleife

**Datei:** `avstream/ttmpeg2videoheader.cpp:328`

Start-Code-Suche nach Picture Coding Extension hat kein Iterationslimit.
Manipulierter MPEG-2-Stream ohne Extension → CPU-gebundener DoS.

**Fix:** Maximum 1024 Bytes suchen, dann abbrechen.

### H5: delete statt delete[] fuer new[]-Arrays

**Datei:** `data/ttframesearchtask.cpp:86-87`

```cpp
mpReferenceData = new quint8 [...];  // new[]
delete mpReferenceData;              // FALSCH — muss delete[] sein
```

Undefined Behavior, Heap-Corruption moeglich.

### H6: av_packet_alloc/av_frame_alloc ohne NULL-Check

**Dateien:** `extern/ttessmartcut.cpp` (Zeilen 1041, 1134, 1169, 1335, 1388, 1705, 1784),
`extern/ttffmpegwrapper.cpp:1564`, `extern/ttmkvmergeprovider.cpp` (459, 520, 560, 604)

Unter Speicherdruck crasht die Applikation bei NULL-Dereference.

---

## MEDIUM

### M1: vsprintf Stack-Overflow

**Datei:** `common/ttmessagelogger.cpp:209` (7 Instanzen)

`vsprintf(buf, msg, ap)` mit 1024-Byte Stack-Buffer ohne Laengenbegrenzung.

**Fix:** `vsnprintf(buf, sizeof(buf), msg, ap)`

### M2: Format-String Vulnerability

**Datei:** `common/ttmessagelogger.cpp:345`

`qDebug(logMsgStr.toUtf8().data())` interpretiert den String als Format-String.
Ein Dateiname mit `%s` verursacht undefiniertes Verhalten.

**Fix:** `qDebug("%s", logMsgStr.toUtf8().data())` oder `qDebug() << logMsgStr`

### M3: system() mit user-konfigurierbarem Temp-Pfad

**Dateien:**
- `avstream/ttmpeg2videostream.cpp:1032` — `system("rm ...")`
- `data/ttcutpreviewtask.cpp:237` — `system()` mit mplex/mv-Kommando

Shell-Metazeichen im Temp-Pfad (User-Settings) koennten injiziert werden.

**Fix:** `system()` durch Qt-Operationen ersetzen (`QDir::entryInfoList` + `QFile::remove`,
`QProcess::start` fuer mplex).

### M4: Shell-Script-Generierung mit unzureichendem Quoting

**Datei:** `extern/ttmplexprovider.cpp:101`

Mux-Script wrpt Dateinamen in Double-Quotes, aber `$`, Backticks und `"` werden
nicht escaped.

**Fix:** Single-Quote-Escaping: `'` + `s.replace("'", "'\\''")` + `'`

### M5: rewriteGOP() Buffer-Index ohne Validierung

**Datei:** `avstream/ttmpeg2videostream.cpp:937`

`(int)(gop->headerOffset()-abs_pos)` — Cast von quint64 auf int kann bei
korrupten Header-Daten Ueberlauf erzeugen. Kein Check gegen Buffer-Groesse.

### M6: 256KB Stack-Allokation

**Datei:** `avstream/ttmpeg2videostream.cpp:696`

`quint8 buffer[262144]` auf dem Stack. Stack-Overflow bei begrenztem Stack.

**Fix:** `QByteArray buffer(262144, '\0')` (Heap).

### M7: SRT Text-Akkumulation ohne Limit

**Datei:** `avstream/ttsrtsubtitlestream.cpp:183`

SRT-Datei ohne Leerzeile → unbegrenzte String-Akkumulation → Memory Exhaustion.

**Fix:** Maximum 64KB pro Untertitel-Eintrag.

### M8: SRT streamLengthTime() NULL-Deref

**Datei:** `avstream/ttsrtsubtitlestream.cpp:88`

Bei leerer SRT-Datei: `header_list->at(count()-1)` mit count=0 → Crash.

**Fix:** Guard `if (header_list == 0 || header_list->count() == 0) return QTime(0,0,0,0);`

### M9: IDD-Datei untrusted Offsets

**Datei:** `avstream/ttmpeg2videostream.cpp:433`

IDD-Cache-Datei enthaelt File-Offsets die direkt fuer Seeks verwendet werden.
Manipulierte IDD → Garbage-Daten als Header interpretiert.

**Fix:** `offset < stream_buffer->size()` validieren.

### M10: SPS-Patching ohne Write-Bounds-Check

**Datei:** `extern/ttessmartcut.cpp:2195`

`spsWriteBits()`/`spsWriteUE()` schreiben ohne Buffer-Groessen-Check.
Bei korrupten SPS-Daten kann `bsrFlagPos` falsch berechnet sein.

### M11: NAL Parser liest nur 32 Bytes fuer Slice-Header

**Datei:** `avstream/ttnaluparser.cpp:351`

Bei 4K-Streams kann `first_mb_in_slice` ueber 32 Bytes hinaus kodiert sein.
Compound-Problem mit C1 (readBits ohne Bounds).

**Fix:** 128 Bytes lesen + Buffer-Groesse an readBits() uebergeben.

---

## LOW

### L1: Ring-Buffer bufferMask ohne Power-of-2 Validierung

**Datei:** `avstream/ttfilebuffer.cpp:86`

`bufferMask = bufferSize - 1` funktioniert nur bei Power-of-2.

**Fix:** `Q_ASSERT((bufSize & (bufSize-1)) == 0)`

### L2: .info Audio-Track-Count unbegrenzt

**Datei:** `avstream/ttesinfo.cpp:163`

`count` aus .info-Datei nicht validiert → Memory Exhaustion bei count=999999999.

**Fix:** Maximum 32 Audio-Tracks.

### L3: XML-Projektdatei ohne Schema-Validierung

**Datei:** `data/ttcutprojectdata.cpp:389`

Float-Versionsvergleich (`ver != 1.0`) und fehlende Child-Count-Checks.

### L4: MPEG-2 Feld-Validierung

**Datei:** `avstream/ttmpeg2videoheader.cpp:118`

Geparste Werte (frame_rate_code, aspect_ratio) ohne Range-Check. Graceful Defaults
vorhanden, aber inkonsistente Fehlerbehandlung.

### L5: ttcut-demux Unquoted Shell-Variablen

**Datei:** `tools/ttcut-demux:462,712`

`$FFMPEG_OPTS` und `$ENCODER` sind intentional unquoted fuer Word-Splitting.
Werte stammen aus ffprobe-Output (validiert via Regex).

**Defense-in-Depth:** Bash-Arrays statt Word-Splitting verwenden.

---

## Priorisierte Fix-Reihenfolge

| Prio | Findings | Aufwand | Impact |
|------|----------|---------|--------|
| 1 | C1+C2+M11 | Mittel | readBits() Bounds-Check behebt 3 Findings gleichzeitig |
| 2 | H1 | Klein | AC3 frmsizecod Validierung (2 Zeilen) |
| 3 | H5 | Klein | delete → delete[] (1 Zeile) |
| 4 | M1+M2 | Klein | vsprintf → vsnprintf + qDebug Fix |
| 5 | H3+H2 | Klein | seekBackward + frame_length Underflow-Checks |
| 6 | H4 | Klein | Iterationslimit fuer Start-Code-Suche |
| 7 | H6 | Mittel | NULL-Checks fuer alle av_*_alloc() |
| 8 | M3+M4 | Mittel | system() ersetzen, Shell-Quoting |
| 9 | M7+M8 | Klein | SRT Laengenlimit + NULL-Guard |
| 10 | Rest | Klein | Low-Priority Hardening |
