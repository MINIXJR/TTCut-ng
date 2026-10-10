# Measurements behind the donor fill

Scripts that produced the numbers the audio repair "fill a hole from a second
track" rests on (`extern/ttdonorfill.cpp`; `docs/completed-work.md`,
"Tonanomalie: Loch aus zweiter Tonspur füllen"). They are evidence, not part
of the program and not run by any gate. Each takes its files as arguments;
its header says how. Needs `python3` with `numpy` and `ffmpeg`.

The fill:

| Script | What it measures |
|---|---|
| `guard.py` | how well each MP2 track of a recording matches the AC3 track next to a pretended 17 ms hole (correlation, offset), one spot every 20 s |
| `guard_inhole.py` | the same, and how much closer the fill comes to the true sound inside the hole than silence does — the basis of the 0.80 hint threshold |
| `hole_env.py` | the level per channel in 1 ms steps around a real hole marker |
| `hole_rule.py` | the rule for locating a hole (thresholds 20, 30 and 40 dB) and the donor fit on real holes — the reference values `tools/diag/gate_donorfill_real.sh` compares the program with |
| `fixture_check.py` | hole rule, fit and fill on the files of `tools/diag/make_donorfill_sample.sh` — where the thresholds of `test_donorfill` come from |

Where a track sits against the original recording (written on 2026-10-07,
while the offset between the audio tracks was traced back to the demux):

| Script | What it measures |
|---|---|
| `widelag.py` | offset between AC3 centre and MP2 mid of a recording, searched over ±20 s |
| `tslag.py` | offset between the AC3 and an MP2 stream in the original transport stream, on the PTS axis |
| `esmap.py` | where a frame of the original transport stream sits in the demuxed ES |
| `where0.py` | which original frame is frame 0 of an extracted ES |
| `pktmap.py` | packets of a remuxed TS mapped back to the frames of the original by their bytes |

Results of the runs are not kept here; the figures taken from them are in
`docs/completed-work.md`.
