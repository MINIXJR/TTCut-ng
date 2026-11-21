# shellcheck shell=bash
# Sourced by the gate scripts that read the recording corpus: a local mirror
# of the NAS corpus, synced one way (NAS -> mirror) when a gate uses it.
# The NAS is an HDD over NFS; demux of a 7.5 GB recording took 124.6 s from
# it and 29.9 s from a local copy (2026-11-21).
#
#   corpus_sync          sync once per shell (call it in the gate's own shell,
#                        not inside $(...), or the result is not kept)
#   corpus_path <path>   the mirror path for a path under CORPUS_NAS when the
#                        sync succeeded and the mirror has it, else <path>
#
# Corpus lists and gates keep NAS paths; only corpus_path maps them.
CORPUS_NAS="${CORPUS_NAS:-/net/nase2021/Video/Sonstiges/TTCut-ng-Test-Files}"
CORPUS_MIRROR="${CORPUS_MIRROR:-/media/Daten/Video_Tmp/TTCut-ng-Test-Files}"
_corpus_state=""   # "" not synced yet, "mirror" read the mirror, "nas" read the NAS

corpus_sync() {
  [ -n "$_corpus_state" ] && return 0
  local nas=${CORPUS_NAS%/} mirror=${CORPUS_MIRROR%/} parent plan out rc n
  parent=$(dirname "$mirror")
  # Without the mirror's parent (disk not mounted) rsync would create the
  # mirror on whatever filesystem holds that path.
  if [ ! -d "$parent" ]; then
    echo "corpus: $parent missing, reading the NAS" >&2
    _corpus_state=nas; return 0
  fi
  # An unmounted or empty source together with --delete would wipe the mirror.
  if [ -z "$(find "$nas/" -type f -print -quit 2>/dev/null)" ]; then
    if [ -d "$mirror" ]; then
      echo "corpus: NAS not reachable, mirror used as is" >&2; _corpus_state=mirror
    else
      echo "corpus: neither NAS nor mirror found" >&2; _corpus_state=nas
    fi
    return 0
  fi
  # One sync at a time: gates may run in parallel.
  exec {_corpus_lock}>"$mirror.lock"
  flock "$_corpus_lock"
  plan=$(rsync -rlt --delete -n -i "$nas/" "$mirror/" 2>&1); rc=$?
  if [ "$rc" = 0 ]; then
    if [ -z "$plan" ]; then
      echo "corpus: mirror in sync" >&2
    else
      # files to copy or delete; directory-only lines (times) are not counted
      n=$(printf '%s\n' "$plan" | grep -cE '^(\*deleting .*[^/]$|.f)')
      echo "corpus: syncing $n file(s) from the NAS ..." >&2
      out=$(rsync -rlt --delete "$nas/" "$mirror/" 2>&1); rc=$?
      [ "$rc" = 0 ] && echo "corpus: mirror synced, $n file(s) changed" >&2
    fi
  else
    out=$plan
  fi
  flock -u "$_corpus_lock"; exec {_corpus_lock}>&-
  if [ "$rc" = 0 ]; then
    _corpus_state=mirror
  else
    echo "corpus: sync failed (rsync exit $rc: $(printf '%s' "$out" | tail -1)), reading the NAS" >&2
    _corpus_state=nas
  fi
}

corpus_path() {
  local p=$1 nas=${CORPUS_NAS%/} rel
  corpus_sync
  case "$p" in
    "$nas"/*) rel=${p#"$nas"/} ;;
    *) printf '%s\n' "$p"; return 0 ;;
  esac
  if [ "$_corpus_state" = mirror ] && [ -e "${CORPUS_MIRROR%/}/$rel" ]; then
    printf '%s\n' "${CORPUS_MIRROR%/}/$rel"
  else
    printf '%s\n' "$p"
  fi
}
