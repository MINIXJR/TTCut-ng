#!/bin/bash
# Gate for TTThreadTaskPool::startNested().
#
# Builds tools/diag/test_pool_crossthread with ThreadSanitizer and runs it in
# both shapes:
#   queued - the pre-fix call, start(task, true) from a worker thread
#   nested - startNested(), which leaves mTaskQueue to the pool's own thread
#
# The run is judged on findings that actually concern the queue: both stacks
# have to reach it through a TTThreadTaskPool frame AND touch
# QList<TTThreadTask*>. Everything else libQt5Core produces is noise here - it
# is not built with ThreadSanitizer, so its own synchronisation is invisible and
# every task that is created in one thread and run in another shows up as an
# apparent race. Those counts stay the same in both shapes; only the queue
# findings differ.
#
# PASS: queued reports queue findings, nested reports none.
#
# usage: gate_pool_crossthread.sh [innerCount] [mainCount]
set -u
cd "$(dirname "$0")"

INNER=${1:-200}
MAIN=${2:-200}
LOGDIR=$(mktemp -d)
trap 'rm -rf "$LOGDIR"' EXIT

MOC="$(pkg-config --variable=host_bins Qt5Core)/moc"
MOCDIR=$(mktemp -d)
trap 'rm -rf "$LOGDIR" "$MOCDIR"' EXIT   # extends the existing LOGDIR trap
for h in ttthreadtask ttthreadtaskpool ttsettings istatusreporter; do
  "$MOC" ../../common/$h.h -o "$MOCDIR/moc_$h.cpp" || exit 1
done

# QT_NO_DEBUG, not NDEBUG: Q_ASSERT keys off Qt's own macro, and start() now
# asserts on the calling thread - without this the "queued" run aborts at the
# assertion before it ever reaches the race.
echo "building (ThreadSanitizer)..."
g++ -g -O1 -fsanitize=thread -fno-omit-frame-pointer -fPIC -std=gnu++17 \
    -DQT_NO_DEBUG -I../.. \
    $(pkg-config --cflags Qt5Core Qt5Widgets) \
    -o test_pool_crossthread test_pool_crossthread.cpp \
    ../../common/ttthreadtask.cpp ../../common/ttthreadtaskpool.cpp \
    ../../common/ttmessagelogger.cpp ../../common/ttexception.cpp \
    ../../common/ttsettings.cpp ../../common/istatusreporter.cpp \
    "$MOCDIR"/moc_*.cpp \
    $(pkg-config --libs Qt5Core) -lpthread || exit 1

for mode in queued nested; do
  TSAN_OPTIONS="halt_on_error=0" ./test_pool_crossthread "$mode" "$INNER" "$MAIN" \
      > "$LOGDIR/$mode.log" 2>&1
done

python3 - "$LOGDIR" <<'EOF'
import re, sys, os
logdir = sys.argv[1]
counts = {}
for mode in ("queued", "nested"):
    txt = open(os.path.join(logdir, mode + ".log")).read()
    total = hits = 0
    kinds = {}
    for b in re.split(r'^={10,}$', txt, flags=re.M):
        if 'ThreadSanitizer' not in b:
            continue
        total += 1
        pool  = len(re.findall(r'TTThreadTaskPool::(start|startNested|runningTaskCount'
                               r'|overallTime|cleanUpQueue|onThreadTask)', b))
        qlist = re.search(r'QList<TTThreadTask\*>|QQueue<TTThreadTask\*>'
                          r'|indexOf<TTThreadTask', b)
        if pool >= 2 and qlist:
            hits += 1
            s = re.search(r'SUMMARY: ThreadSanitizer: ([^(]*)', b)
            k = s.group(1).strip() if s else '?'
            kinds[k] = kinds.get(k, 0) + 1
    counts[mode] = hits
    crash = 'yes' if ('DEADLYSIGNAL' in txt or 'SEGV' in txt) else 'no'
    print(f"{mode}: {total} report(s) total, {hits} concerning mTaskQueue, crash={crash}")
    for k, v in sorted(kinds.items(), key=lambda x: -x[1]):
        print(f"    {v}x {k}")

if counts["queued"] == 0:
    print("INCONCLUSIVE: the pre-fix shape produced no queue finding in this run - "
          "raise the counts and try again")
    sys.exit(2)
if counts["nested"] > 0:
    print("FAIL: startNested() still reaches mTaskQueue from a worker thread")
    sys.exit(1)
print("PASS: queue findings only in the pre-fix shape")
EOF
