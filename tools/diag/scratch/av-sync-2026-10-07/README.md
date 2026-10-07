# A/V sync investigation 2026-10-07 — throwaway tools

Measurement and simulation scripts behind the spec
`docs/superpowers/specs/2026-10-07-av-sync-timestamp-rule-design.md` and the
TODO entries "Ton gegen Bild in der fertigen MKV" / "ttcut-demux: Ton
120–136 ms zu spät im ES". Kept as written (paths under
`/usr/local/src/CLAUDE_TMP/TTCut-ng/demux-shift/` are hard-coded); they are
the templates for `tools/diag/av_chain_check.py` and `av_track_audit.py`
(plan task 6), not maintained tools.

- `chain.py ORIGINAL_TS MKV` — sound against picture in a cut MKV, by decoded-picture MD5 and audio payload MD5; control: a plain `ffmpeg -c copy` remux gives 0.0 ms.
- `prep.sh`, `mid.sh`, `multi.sh` — head/mid-stream/multi-file runs of `ttcut-demux` variants (`variants/cur|k1|k3|k4|k5`) and the headless cut.
- `probe_packets.py` — packet lists (video PTS, audio MD5+PTS) of a recording into a pickle.
- `zoneaudit.py FPS ESDIR BASENAME PICKLE` — sound against picture over a whole ES set, no cut needed (same picture model as the rule).
- `zonesim.py PICKLE FPS [PLANDIR]` — the slot rule computed on packet lists; `applyrule.py` builds the ES from a plan (`eswalk.py`: resyncing frame walker).
- `tracksync.py`, `spot.py`, `gapaudit.py`, `holes.py`, `esdelta.py`, `escmp.py`, `headcut_probe.py`, `es_start.py`, `survey.py`, `order.py` — the smaller probes named in the TODO entries.
- `*-result.txt`, `probe-all.txt`, `survey-all.txt` — the measurements as printed.
