#!/bin/bash
# mid.sh TAG FRAME_IN : cut 1500 frames from FRAME_IN out of the full-run ES of TAG
# and compare against the matching part of the original TS.
D=/usr/local/src/CLAUDE_TMP/TTCut-ng/demux-shift/chain; R=/usr/local/src/TTCut-ng
tag=$1; fin=$2; W="$D/$tag"; cd "$W" || exit 1
src=$(readlink -f head.ts); size=$(stat -c %s "$src")
dur=$(sed -n 's/^video_duration_ms=//p' out-cur/t.info | head -1)
fps=$(awk -v f="$fin" 'BEGIN{print 50}')
off=$(LC_ALL=C awk -v s="$size" -v d="$dur" -v f="$fin" 'BEGIN{t=f/50-25; if(t<0)t=0; o=int(s*t*1000/d/188)*188; print o}')
tail -c +$((off+1)) "$src" | head -c 200000000 > "mid-$fin.ts"
sed "s|<CutIn>500</CutIn><CutOut>2000</CutOut>|<CutIn>$fin</CutIn><CutOut>$((fin+1500))</CutOut>|" p-cur.ttcut > "p-mid-$fin.ttcut"
"$R/tools/diag/acm-cut.sh" "p-mid-$fin.ttcut" "$W/cut-mid-$fin.mkv" 2>&1 | sed -n '2p'
echo "== $tag, ganze Aufnahme, heutiges Skript, Bilder $fin-$((fin+1500))"
LC_ALL=C python3 "$D/chain.py" "mid-$fin.ts" "cut-mid-$fin.mkv"
rm -f "mid-$fin.ts"
