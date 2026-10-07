#!/bin/bash
# multi.sh TAG REC_DIR : whole multi-file recording through cur and k3, then compare the ES
D=/usr/local/src/CLAUDE_TMP/TTCut-ng/demux-shift
export XDG_CACHE_HOME=/usr/local/src/CLAUDE_TMP/TTCut-ng/xdg-cache
tag=$1; rec=$2; W="$D/chain/$tag"; mkdir -p "$W"; cd "$W" || exit 1
for v in cur k3; do
    rm -rf "out-$v"; mkdir -p "out-$v"
    s=$(date +%s)
    "$D/variants/$v" -e --no-subs -n t "$rec/00001.ts" "out-$v/" > "run-$v.log" 2>&1 </dev/null
    echo "$tag/$v: Exit $?, $(( $(date +%s) - s )) s"
done
for f in out-cur/t.* out-cur/t_*; do
    b=$(basename "$f"); [ "$b" = t.info ] && continue
    if cmp -s "$f" "out-k3/$b"; then echo "   $b: gleich ($(stat -c %s "$f") Bytes)"; else echo "   $b: verschieden"; LC_ALL=C python3 "$D/chain/escmp.py" "$f" "out-k3/$b"; fi
done
diff <(grep -v '^#\|^created\|^date' out-cur/t.info) <(grep -v '^#\|^created\|^date' out-k3/t.info) > info.diff && echo "   t.info: gleich" || { echo "   t.info: Unterschiede:"; head -20 info.diff; }
