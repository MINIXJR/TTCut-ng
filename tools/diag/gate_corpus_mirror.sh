#!/bin/bash
# gate_corpus_mirror.sh - tools/diag/corpus.sh, the one-way sync of the NAS
# corpus into its local mirror, on throwaway directories (no NAS needed):
# first sync, no change, a changed and a removed file, path mapping; the
# guards: an empty or missing source leaves the mirror untouched (with
# --delete it would wipe it), a missing mirror disk creates nothing, a failed
# rsync reads the NAS; two syncs at once are serialized.
#   usage: tools/diag/gate_corpus_mirror.sh [workdir]
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
H="$ROOT/tools/diag/corpus.sh"
T="${1:-${W:-/usr/local/src/CLAUDE_TMP/TTCut-ng/corpus-mirror-gate}}/t"
P=0; F=0
ok()  { echo "PASS: $1"; P=$((P+1)); }
bad() { echo "FAIL: $1"; F=$((F+1)); }
fresh() { chmod -R u+w "${T:?}" 2>/dev/null; rm -rf "${T:?}"; mkdir -p "$T/nas/HD/rec" "$T/disk"; echo a > "$T/nas/HD/rec/00001.ts"; echo b > "$T/nas/x.txt"; }
run() { # $1 = commands after sourcing; prints stderr+stdout
  ( CORPUS_NAS="$NAS" CORPUS_MIRROR="$MIR"; . "$H"; eval "$1" ) 2>&1; }

fresh; NAS=$T/nas MIR=$T/disk/m
o=$(run 'corpus_sync; corpus_sync'); [ "$o" = "corpus: syncing 2 file(s) from the NAS ...
corpus: mirror synced, 2 file(s) changed" ] && cmp -s "$T/nas/x.txt" "$MIR/x.txt" && ok "first sync copies, second call silent" || bad "first sync: $o"
o=$(run 'corpus_sync'); [ "$o" = "corpus: mirror in sync" ] && ok "unchanged -> in sync" || bad "unchanged: $o"
echo changed > "$T/nas/x.txt"; touch -d '2020-01-01' "$T/nas/x.txt"
o=$(run 'corpus_sync'); [ "$(cat "$MIR/x.txt")" = changed ] && [[ "$o" == *"mirror synced"* ]] && ok "changed file synced" || bad "changed: $o"
rm "$T/nas/x.txt"; o=$(run 'corpus_sync'); [ ! -e "$MIR/x.txt" ] && [[ "$o" == *"mirror synced, 1 file(s)"* ]] && ok "file removed on NAS removed in mirror" || bad "delete: $o"
o=$(run 'corpus_path "$CORPUS_NAS/HD/rec/00001.ts"; corpus_path "$CORPUS_NAS/HD/missing.ts"; corpus_path /elsewhere/f.ts')
[ "$o" = "corpus: mirror in sync
$MIR/HD/rec/00001.ts
$NAS/HD/missing.ts
/elsewhere/f.ts" ] && ok "corpus_path mapping" || bad "mapping: $o"
o=$(run 'corpus_path "$CORPUS_NAS/HD/rec/"'); [[ "$o" == *"$MIR/HD/rec/" ]] && ok "directory path with slash mapped" || bad "dir: $o"

# empty source: mirror must stay
mv "$T/nas" "$T/nas.away"; mkdir "$T/nas"
o=$(run 'corpus_path "$CORPUS_NAS/HD/rec/00001.ts"'); [ -s "$MIR/HD/rec/00001.ts" ] && [ "$o" = "corpus: NAS not reachable, mirror used as is
$MIR/HD/rec/00001.ts" ] && ok "empty source: mirror untouched and used" || bad "empty source: $o"
rmdir "$T/nas"
o=$(run 'corpus_sync'); [ -s "$MIR/HD/rec/00001.ts" ] && [ "$o" = "corpus: NAS not reachable, mirror used as is" ] && ok "missing source: mirror untouched" || bad "missing source: $o"
mv "$T/nas.away" "$T/nas"

# mirror parent missing: no sync, NAS path, nothing created
MIR=$T/nodisk/m
o=$(run 'corpus_path "$CORPUS_NAS/HD/rec/00001.ts"'); [ ! -e "$T/nodisk" ] && [ "$o" = "corpus: $T/nodisk missing, reading the NAS
$NAS/HD/rec/00001.ts" ] && ok "missing parent: NAS path, nothing created" || bad "missing parent: $o"

# neither NAS nor mirror
MIR=$T/disk/none; mv "$T/nas" "$T/nas.away"
o=$(run 'corpus_path "$CORPUS_NAS/HD/rec/00001.ts"'); [ "$o" = "corpus: neither NAS nor mirror found
$NAS/HD/rec/00001.ts" ] && ok "neither: NAS path" || bad "neither: $o"
mv "$T/nas.away" "$T/nas"

# rsync fails (a source file unreadable, exit 23): NAS path
MIR=$T/disk/m; echo new > "$T/nas/new.txt"; chmod 000 "$T/nas/new.txt"
o=$(run 'corpus_path "$CORPUS_NAS/HD/rec/00001.ts"'); chmod 644 "$T/nas/new.txt"
[[ "$o" == *"sync failed (rsync exit "* ]] && [[ "$o" == *"
$NAS/HD/rec/00001.ts" ]] && ok "rsync failure: NAS path" || bad "rsync failure: $o"

# parallel: two syncs at once both succeed, mirror complete
dd if=/dev/urandom of="$T/nas/big.bin" bs=1M count=100 status=none
( run 'corpus_sync' > "$T/p1" ) & ( run 'corpus_sync' > "$T/p2" ) & wait
cmp -s "$T/nas/big.bin" "$MIR/big.bin" && ! grep -q failed "$T/p1" "$T/p2" && grep -q "in sync" "$T/p1" "$T/p2" && ok "parallel syncs serialized" || bad "parallel: $(cat "$T/p1" "$T/p2")"

chmod -R u+w "${T:?}"; rm -rf "${T:?}"
echo "RESULT: $P passed, $F failed"; [ "$F" = 0 ]
