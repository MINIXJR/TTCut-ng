#!/bin/bash
# Run the three script variants on the first 80 MB of every recording.
D=/usr/local/src/CLAUDE_TMP/TTCut-ng/demux-shift
export XDG_CACHE_HOME=/usr/local/src/CLAUDE_TMP/TTCut-ng/xdg-cache
n=0
find /media/Daten/Video_Tmp/temp -name 00001.ts | sort | while IFS= read -r f; do
    n=$((n+1)); W="$D/w$n"; rm -rf "$W"; mkdir -p "$W"; cd "$W" || exit 1
    echo "== ${f#/media/Daten/Video_Tmp/temp/}"
    head -c 80000000 "$f" > head.ts
    for v in cur k1 k3; do
        mkdir -p "out-$v"
        "$D/variants/$v" -e --no-subs -n t head.ts "out-$v/" > "run-$v.log" 2>&1 </dev/null
        echo "  $v (Exit $?): $(LC_ALL=C python3 "$D/es_start.py" head.ts "out-$v" t 2>&1 | tail -1)"
    done
    for v in k1 k3; do
        same=0; tot=0
        for a in out-cur/t.*264 out-cur/t.*265 out-cur/t.m2v; do [ -f "$a" ] || continue; tot=$((tot+1)); cmp -s "$a" "out-$v/$(basename "$a")" && same=$((same+1)); done
        echo "  Video-ES $v gegen cur: $same von $tot gleich; Tonlängen cur/$v: $(for a in out-cur/t_*; do printf '%s %s/%s  ' "$(basename "$a" | sed 's/^t_//')" "$(stat -c %s "$a")" "$(stat -c %s "out-$v/$(basename "$a")" 2>/dev/null)"; done)"
    done
    cd "$D" && rm -rf "$W"
done
