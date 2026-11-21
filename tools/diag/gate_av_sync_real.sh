#!/bin/bash
# gate_av_sync_real.sh - sound against picture on real recordings, through
# the whole chain: original TS -> ttcut-demux -> TTCut-ng --auto-cut -> MKV.
#
# Per recording:
#   1. demux the whole recording (every segment) with the repository's
#      ttcut-demux and ttcut-audiofix
#   2. av_track_audit.py over the demuxed set: every audio frame against the
#      picture at its PTS, bound +-1 audio frame, and no source frame lost or
#      ES frame inserted away from picture holes. The whole recording because
#      a defect can start late: the slot rule's one-tick-jitter drop first hit
#      at 246.5 s of Tatort "Schlangengrube", the former 250 MB head ended
#      near 150 s
#   3. project with every audio track, frames 500-2000 -> --auto-cut ->
#      av_chain_check.py against the head of the original (the first HEAD_MB
#      megabytes, default 250), bound +-1 frame (with the plain-remux
#      control). SKIP when TTCut-ng refuses the cut; the audit then stands
#      alone.
#
#   usage: gate_av_sync_real.sh <recording-dir | 00001.ts | file.ts> [more ...]
#
# Recordings under the NAS corpus are read from its local mirror, synced
# first (tools/diag/corpus.sh); corpus lists keep NAS paths.
#
# Not in the run-gates.sh table: it needs recordings and decodes them (minutes
# per recording). Exit 0 all PASS/SKIP, 1 a FAIL, 2 usage.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck source=tools/diag/corpus.sh
. "$ROOT/tools/diag/corpus.sh"
DEMUX="$ROOT/tools/ttcut-demux/ttcut-demux"
WORK="${W:-/usr/local/src/CLAUDE_TMP/TTCut-ng/av-sync-real}"
HEAD_MB="${HEAD_MB:-250}"
[ $# -ge 1 ] || { sed -n '2,20p' "$0"; exit 2; }
[ -x "$ROOT/build/ttcut-ng" ] || { echo "FAIL: build/ttcut-ng missing (cmake --build build)"; exit 1; }
[ -x "$ROOT/tools/ttcut-audiofix/ttcut-audiofix" ] || { echo "FAIL: ttcut-audiofix not built"; exit 1; }
mkdir -p "$WORK"
export XDG_CACHE_HOME="$WORK/xdg"
export PATH="$ROOT/tools/ttcut-audiofix:$ROOT/tools/ttcut-ac3fix:$PATH"
NPASS=0; NFAIL=0; NSKIP=0
corpus_sync

for arg in "$@"; do
    arg=$(corpus_path "$arg")
    if [ -d "$arg" ]; then
        first=$(find "$arg" -maxdepth 2 \( -name '00001.ts' -o -name '001.vdr' \) | sort | head -1)
        [ -n "$first" ] || first=$(find "$arg" -maxdepth 2 -name '*.ts' | sort | head -1)
    else
        first="$arg"
    fi
    [ -f "$first" ] || { echo "FAIL $arg: no TS file found"; NFAIL=$((NFAIL+1)); continue; }
    recdir=$(dirname "$first")
    # full name: corpus folders share long prefixes (a cut to 80 characters
    # put Bibi-Files and Tatort into one directory)
    tag=$(basename "$recdir" | tr -c 'A-Za-z0-9._-' '_')
    [ "$(basename "$first")" = "00001.ts" ] && tag=$(basename "$(dirname "$recdir")" | tr -c 'A-Za-z0-9._-' '_')
    D="$WORK/$tag"; rm -rf "$D"; mkdir -p "$D"
    mapfile -t segs < <(find "$recdir" -maxdepth 1 -name '[0-9][0-9][0-9][0-9][0-9].ts' | sort)
    [ "$(basename "$first")" = "00001.ts" ] || segs=("$first")

    # original for the chain check: the head of the recording, across the
    # segments (a VDR split is one continuous TS; the first file can be a
    # few seconds only)
    cat "${segs[@]}" 2>/dev/null | head -c $(( HEAD_MB * 1000000 / 188 * 188 )) > "$D/head.ts"
    src="$first"; scope="whole, ${#segs[@]} file(s)"

    rc=0; "$DEMUX" -e --no-subs -n t "$src" "$D/es/" > "$D/demux.log" 2>&1 < /dev/null || rc=$?
    if [ "$rc" != 0 ] || [ ! -s "$D/es/t.info" ]; then
        echo "FAIL $tag: ttcut-demux exit $rc: $(tail -1 "$D/demux.log")"; NFAIL=$((NFAIL+1)); continue
    fi
    fps=$(sed -n 's/^frame_rate=//p' "$D/es/t.info" | head -1 | awk -F/ '{ printf "%.6f", $1 / (($2 > 0) ? $2 : 1) }')
    vfile=$(sed -n '/^\[video\]/,/^\[/s/^file=//p' "$D/es/t.info" | head -1)

    verdict=PASS
    python3 "$ROOT/tools/diag/av_track_audit.py" "$fps" "$D/es" t "${segs[@]}" --bound-frames 1 > "$D/audit.txt" 2>&1; arc=$?
    [ "$arc" = 0 ] || verdict=FAIL
    audit_fig=$(grep 'sound against picture' "$D/audit.txt" | sed -E 's/^([^:]+): .*min ([-+0-9]+), max ([-+0-9]+) ms; .info start offset ([^ ]+) ms.*\[bound[^:]*: (.*)\]$/\1 \2..\3 (offset \4, \5)/' | tr '\n' ';')

    {   echo '<!DOCTYPE TTCut-Projectfile>'; echo '<TTCut-Projectfile>'; echo ' <Version>1.0</Version>'; echo ' <Video>'
        echo '  <Order>0</Order>'; echo "  <Name>$D/es/$vfile</Name>"
        n=0
        while af=$(sed -n "s/^audio_${n}_file=//p" "$D/es/t.info") && [ -n "$af" ]; do
            echo "  <Audio><Order>$n</Order><Name>$D/es/$af</Name></Audio>"; n=$((n+1))
        done
        echo '  <Cut><Order>0</Order><CutIn>500</CutIn><CutOut>2000</CutOut></Cut>'; echo ' </Video>'; echo '</TTCut-Projectfile>'
    } > "$D/p.ttcut"
    "$ROOT/tools/diag/acm-cut.sh" "$D/p.ttcut" "$D/cut.mkv" > "$D/cut.log" 2>&1
    if [ -s "$D/cut.mkv" ]; then
        python3 "$ROOT/tools/diag/av_chain_check.py" "$D/head.ts" "$D/cut.mkv" --bound-frames 1 --control > "$D/chain.txt" 2>&1; crc=$?
        [ "$crc" = 0 ] || verdict=FAIL
        chain_fig=$(grep '^audio' "$D/chain.txt" | sed -E 's/^audio +([^ ]+) *: /\1 /; s/ \(MKV[^)]*\)//g; s/  \[bound[^:]*: (.*)\]$/ (\1)/' | tr '\n' ';')
        [ "$crc" = 2 ] && chain_fig="measurement failed: $(tail -1 "$D/chain.txt")"
    else
        chain_fig="SKIPPED - TTCut-ng produced no MKV ($(grep -ai 'error\|refus\|misalign' "$D/xdg-cache/ttcut-ng/logfile.log" 2>/dev/null | tail -1 | cut -c1-120))"
        [ "$verdict" = PASS ] && verdict=SKIP
    fi
    case "$verdict" in PASS) NPASS=$((NPASS+1)) ;; FAIL) NFAIL=$((NFAIL+1)) ;; SKIP) NSKIP=$((NSKIP+1)) ;; esac
    echo "$verdict $tag ($scope)"
    echo "     audit: ${audit_fig:-$(tail -1 "$D/audit.txt")}"
    echo "     chain: $chain_fig"
    grep -h 'SEVERELY\|Material loss\|placement failed\|not written\|NOT aligned' "$D/demux.log" | sed 's/\x1b\[[0-9;]*m//g; s/^/     demux: /' | head -6
    [ "${KEEP:-0}" = 1 ] || rm -f "$D/head.ts" "$D/es/$vfile" "$D/cut.mkv"
done
echo "RESULT: $NPASS passed, $NFAIL failed, $NSKIP chain skipped  (details: $WORK/<name>/)"
[ "$NFAIL" = 0 ]
