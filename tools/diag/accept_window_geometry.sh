#!/bin/bash
# Half-automatic acceptance for the plain-text window geometry (Task 5 of
# docs/superpowers/plans/2026-07-31-window-geometry-plaintext.md).
#
# Not a gate_*.sh: window placement cannot be judged without a screen. The
# script owns everything that IS objective — putting the config into a known
# starting state and checking the file afterwards — and tells you what to look
# at while the application is up.
#
# usage:
#   accept_window_geometry.sh list          show the cases
#   accept_window_geometry.sh run <n>       set up case n, start TTCut, check on exit
#   accept_window_geometry.sh setup <n>     only set up case n
#   accept_window_geometry.sh verify <n>    only check case n's outcome
#   accept_window_geometry.sh all           run every case in order
#   accept_window_geometry.sh restore       put the original config back
#
# The original config is saved on first use and never overwritten after that.

set -u

CONF="$HOME/.config/TTCut-ng/TTCut-ng.conf"
STRAY="$HOME/.config/Unknown Organization/TTCut-ng.conf"
WORK="/usr/local/src/CLAUDE_TMP/TTCut-ng"
BACKUP="$WORK/acceptance-original.conf"
BLOB_SRC="$WORK/backup.conf"          # a config still holding the legacy blob
APP="$(cd "$(dirname "$0")/../.." && pwd)/ttcut-ng"

# Wayland clients cannot place their own window: setGeometry's position is
# dropped when the surface is mapped (measured 2026-08-01 on this system —
# xcb keeps 320,180, wayland reports 0,0). So x/y are only meaningful under
# xcb, and any case that depends on the position proves nothing under wayland.
on_wayland() {
  [ "${QT_QPA_PLATFORM:-}" = "xcb" ] && return 1
  [ "${XDG_SESSION_TYPE:-}" = "wayland" ]
}

pass=0; fail=0
ok()   { echo -e "  \033[32mPASS\033[0m  $*"; pass=$((pass+1)); }
bad()  { echo -e "  \033[31mFAIL\033[0m  $*"; fail=$((fail+1)); }
note() { echo -e "  \033[33m>>\033[0m    $*"; }

need_backup() {
  mkdir -p "$WORK"
  if [ ! -f "$BACKUP" ]; then
    if [ -f "$CONF" ]; then
      cp "$CONF" "$BACKUP"
      echo "original config saved to $BACKUP"
    else
      echo "no config yet — nothing to save"
    fi
  fi
}

# --- helpers on the config file ---------------------------------------------

get() {  # get <group> <key>  -> value, empty when absent
  [ -f "$CONF" ] || return 0
  sed -n "/^\[$1\]/,/^\[/p" "$CONF" | sed -n "s/^$2=//p" | tr -d '\r'
}

drop_group() {  # remove a whole [group] block
  [ -f "$CONF" ] || return 0
  python3 - "$CONF" "$1" <<'PY'
import sys, re
path, group = sys.argv[1], sys.argv[2]
out, skip = [], False
for line in open(path):
    if line.startswith('['):
        skip = (line.strip() == '[%s]' % group)
    if not skip:
        out.append(line)
open(path, 'w').writelines(out)
PY
}

set_key() {  # set_key <group> <key> <value>, creating the group when needed
  python3 - "$CONF" "$1" "$2" "$3" <<'PY'
import sys
path, group, key, value = sys.argv[1:5]
try:
    lines = open(path).readlines()
except FileNotFoundError:
    lines = []
head = '[%s]\n' % group
if head not in lines:
    lines = [head, '%s=%s\n' % (key, value), '\n'] + lines
else:
    i = lines.index(head) + 1
    while i < len(lines) and not lines[i].startswith('['):
        if lines[i].startswith(key + '='):
            lines[i] = '%s=%s\n' % (key, value)
            break
        i += 1
    else:
        lines.insert(i, '%s=%s\n' % (key, value))
open(path, 'w').writelines(lines)
PY
}

mainwindow_plain_keys_present() {
  for k in x y width height maximized; do
    [ -n "$(get MainWindow $k)" ] || return 1
  done
  return 0
}

# --- the cases ---------------------------------------------------------------

case_title() {
  case $1 in
    1) echo "fresh config -> 80% of the primary screen, centred" ;;
    2) echo "legacy blob  -> migrated to plain keys on start" ;;
    3) echo "hand-edited  -> window opens at the size you typed (position: xcb only)" ;;
    4) echo "maximised    -> stage 1: maximise and close" ;;
    4b) echo "maximised    -> stage 2: comes back maximised, normal size preserved" ;;
    5) echo "off-screen   -> falls back to 80% centred (xcb only)" ;;
    6) echo "oversized    -> clamped to the screen" ;;
    7) echo "QuickJump    -> dialog size survives, no stray file" ;;
    8) echo "settings     -> no @Variant left after a normal close" ;;
    *) echo "unknown case" ;;
  esac
}

setup_case() {
  need_backup
  case $1 in
    1) drop_group MainWindow
       note "watch: window opens centred, roughly 4/5 of the screen" ;;
    2) if [ -f "$BLOB_SRC" ] && grep -q "@ByteArray" "$BLOB_SRC"; then
         cp "$BLOB_SRC" "$CONF"
         note "config replaced with the pre-migration one (still has the blob)"
         note "watch: window opens at 1616x973 in the top-left corner"
       else
         echo "  SKIP: no blob-carrying config at $BLOB_SRC"
         return 2
       fi ;;
    3) set_key MainWindow x 320;  set_key MainWindow y 180
       set_key MainWindow width 1920; set_key MainWindow height 1080
       set_key MainWindow maximized false
       note "watch: window is 1920x1080 with its top-left corner at 320,180" ;;
    4) drop_group MainWindow
       note "DO: maximise the window, then close it. Run 'verify 4' after."
       note "then: run 'run 4b' to check it comes back maximised" ;;
    4b) note "watch: window comes up MAXIMISED"
        note "DO: un-maximise it — it must return to the size from before" ;;
    5) set_key MainWindow x 9000; set_key MainWindow y 9000
       set_key MainWindow width 1280; set_key MainWindow height 720
       set_key MainWindow maximized false
       note "watch: window does NOT open off-screen; falls back to 80% centred" ;;
    6) set_key MainWindow x 0; set_key MainWindow y 0
       set_key MainWindow width 9999; set_key MainWindow height 9999
       set_key MainWindow maximized false
       note "watch: window fits on the screen instead of overflowing it" ;;
    7) drop_group QuickJumpDialog
       note "DO: LOAD A VIDEO FIRST — the frame-jump dialog needs one."
       note "     Then open it, resize it noticeably, close it, close TTCut." ;;
    8) note "DO: just close TTCut normally (the settings are rewritten on close)" ;;
    *) echo "no such case"; return 2 ;;
  esac
}

verify_case() {
  echo "verify case $1: $(case_title "$1")"
  case $1 in
    1|4)
       mainwindow_plain_keys_present \
         && ok "all five plain keys written" \
         || bad "plain keys missing: x=$(get MainWindow x) y=$(get MainWindow y) w=$(get MainWindow width) h=$(get MainWindow height) max=$(get MainWindow maximized)"
       grep -q "@ByteArray" "$CONF" && bad "a byte array is back in the file" || ok "no byte array"
       if [ "$1" = 4 ]; then
         [ "$(get MainWindow maximized)" = "true" ] \
           && ok "maximized=true recorded" \
           || bad "maximized is '$(get MainWindow maximized)', expected true"
         local w h
         w=$(get MainWindow width); h=$(get MainWindow height)
         note "normal size recorded as ${w}x${h} — must NOT be the full screen size"
       fi ;;
    4b)
       # After stage 2 the window was un-maximised by hand, so the flag must
       # have flipped back and the normal size must be the one from stage 1.
       [ "$(get MainWindow maximized)" = "false" ] \
         && ok "maximized=false after un-maximising" \
         || note "maximized is '$(get MainWindow maximized)' — expected false IF you un-maximised before closing"
       note "normal size now ${w:-$(get MainWindow width)}x$(get MainWindow height)"
       note "compare against stage 1: it must be the SAME size, not the screen size" ;;
    2) grep -q "@ByteArray" "$CONF" && bad "blob still present" || ok "blob removed"
       mainwindow_plain_keys_present && ok "plain keys written" || bad "plain keys missing"
       [ "$(get MainWindow width)" = "1616" ] \
         && ok "width carried over from the blob (1616)" \
         || bad "width is '$(get MainWindow width)', expected 1616 from the blob"
       [ "$(get MainWindow height)" = "973" ] \
         && ok "height carried over from the blob (973)" \
         || bad "height is '$(get MainWindow height)', expected 973 from the blob" ;;
    3) [ "$(get MainWindow width)" = "1920" ] && [ "$(get MainWindow height)" = "1080" ] \
         && ok "size kept at 1920x1080" \
         || bad "size became $(get MainWindow width)x$(get MainWindow height)"
       if on_wayland; then
         note "position: $(get MainWindow x),$(get MainWindow y) — NOT checked."
         note "  A wayland client cannot place itself; x/y only take effect under"
         note "  QT_QPA_PLATFORM=xcb. Re-run this case that way to test the position."
       else
         [ "$(get MainWindow x)" = "320" ] && [ "$(get MainWindow y)" = "180" ] \
           && ok "position kept at 320,180" \
           || bad "position became $(get MainWindow x),$(get MainWindow y)"
       fi
       note "this only proves the value survived; that the window LOOKED right is your call" ;;
    5|6)
       local x y w h
       x=$(get MainWindow x); y=$(get MainWindow y)
       w=$(get MainWindow width); h=$(get MainWindow height)
       note "recorded: ${w}x${h} at ${x},${y}"
       if [ "$1" = 5 ]; then
         if on_wayland; then
           note "SKIPPED under wayland: the window never lands off-screen there,"
           note "  because the position is ignored on mapping. A pass here would"
           note "  mean nothing. Re-run with QT_QPA_PLATFORM=xcb to test it."
         else
           [ "$x" -lt 9000 ] 2>/dev/null \
             && ok "no longer at the off-screen x" \
             || bad "x is still $x — the fallback did not engage"
         fi
       else
         [ "$w" -lt 9999 ] 2>/dev/null && [ "$h" -lt 9999 ] 2>/dev/null \
           && ok "clamped below the nonsense size" \
           || bad "size is still ${w}x${h} — the clamp did not engage"
       fi ;;
    7) [ -n "$(get QuickJumpDialog width)" ] && [ -n "$(get QuickJumpDialog height)" ] \
         && ok "dialog size recorded as $(get QuickJumpDialog width)x$(get QuickJumpDialog height)" \
         || bad "no QuickJumpDialog size in the config"
       grep -q "@Size" "$CONF" && bad "still stored as @Size" || ok "stored as plain numbers"
       [ -f "$STRAY" ] \
         && bad "the stray file is back: $STRAY" \
         || ok "no stray file under 'Unknown Organization'"
       note "the directory itself is shared with other applications and stays" ;;
    8) local n
       n=$(grep -c "@Variant" "$CONF")
       [ "$n" = "0" ] && ok "no @Variant left" || bad "$n @Variant entries remain"
       for k in BlackThreshold SceneThreshold LogoThreshold; do
         local v; v=$(sed -n "s/^Navigation\\\\$k=//p" "$CONF")
         case "$v" in
           *[0-9].[0-9] | *[0-9].[0-9][0-9] | *[0-9].[0-9][0-9][0-9] | [0-9])
              ok "$k = $v (short decimal)" ;;
           "")        bad "$k missing" ;;
           *@Variant*|*'"@Variant'*)
              bad "$k is still binary — TTCut has not been closed normally since the change" ;;
           *)  bad "$k = $v — long decimal tail, the rounding did not engage" ;;
         esac
       done ;;
    *) echo "no such case"; return 2 ;;
  esac
}

run_case() {
  echo
  echo "=== case $1: $(case_title "$1") ==="
  setup_case "$1" || return
  echo

  # The application's output goes to a log, not /dev/null: a start that fails
  # must not look like a start that worked. And the run itself is checked -
  # verifying the config file proves nothing if TTCut never came up.
  local log started rc elapsed conf_before conf_after
  log="$WORK/last-run-case$1.log"
  conf_before=$(stat -c %Y "$CONF" 2>/dev/null || echo 0)
  started=$(date +%s)

  echo "  starting $APP — close it when you are done looking"
  "$APP" > "$log" 2>&1
  rc=$?
  elapsed=$(( $(date +%s) - started ))
  conf_after=$(stat -c %Y "$CONF" 2>/dev/null || echo 0)

  echo
  if [ "$elapsed" -lt 3 ]; then
    bad "TTCut exited after ${elapsed}s (rc=$rc) — it never really ran"
    note "last lines of $log:"
    tail -5 "$log" | sed 's/^/      /'
    note "everything below is meaningless while this fails"
  else
    ok "TTCut ran for ${elapsed}s and exited with rc=$rc"
  fi
  if [ "$rc" != 0 ]; then
    bad "non-zero exit code $rc — see $log"
  fi
  if [ "$conf_after" = "$conf_before" ]; then
    bad "the config file was not touched — closeEvent did not run"
    note "did you close the window, or kill the process?"
  fi
  if [ -s "$log" ]; then
    note "the application wrote output; first lines:"
    head -3 "$log" | sed 's/^/      /'
  fi

  echo
  verify_case "$1"
}

case "${1:-}" in
  list)
    for n in 1 2 3 4 4b 5 6 7 8; do printf "  %-3s %s\n" "$n" "$(case_title $n)"; done ;;
  setup)  need_backup; setup_case "${2:?case number}" ;;
  verify) verify_case "${2:?case number}" ;;
  run)    run_case "${2:?case number}" ;;
  all)
    for n in 1 2 3 5 6 7 8; do run_case "$n"; done
    echo
    echo "case 4 (maximised) is interactive in two stages — run it by hand:"
    echo "  $0 run 4     (maximise, then close)"
    echo "  $0 run 4b    (check it returns maximised)" ;;
  restore)
    if [ -f "$BACKUP" ]; then
      cp "$BACKUP" "$CONF"; echo "restored $CONF from $BACKUP"
    else
      echo "no backup at $BACKUP"; exit 1
    fi ;;
  *)
    sed -n '2,20p' "$0" | sed 's/^# \?//'
    exit 1 ;;
esac

echo
[ $((pass+fail)) -gt 0 ] && echo "passed $pass, failed $fail"
exit $(( fail > 0 ? 1 : 0 ))
