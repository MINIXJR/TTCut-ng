#!/bin/bash
# gate_demux_slotplan.sh - the slot rule of ttcut-demux (video_slots +
# plan_audio_slots): every audio frame goes to the picture slot its PTS
# names; frames in a hole are dropped, empty slots get silence. Synthetic
# PTS lists, no material. 50 fps picture (20 ms), MP2 audio (24 ms).
# The checks run once per installed awk (gawk, mawk).
#   usage: tools/diag/gate_demux_slotplan.sh
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEMUX="$ROOT/tools/ttcut-demux/ttcut-demux"
WORK="${W:-/usr/local/src/CLAUDE_TMP/TTCut-ng/demux-slotplan-tests}"
mkdir -p "$WORK"
PASS=0; FAIL=0
ok()  { echo "PASS: $1"; PASS=$((PASS+1)); }
bad() { echo "FAIL: $1"; FAIL=$((FAIL+1)); }
extract_fn() { awk -v fn="$1" '$0 == fn"() {" {infn=1} infn {print} infn && /^}/ {exit}' "$DEMUX"; }
FN_FILE="$WORK/.fns.sh"
{ extract_fn pts_runs; extract_fn video_slots; extract_fn plan_audio_slots; } > "$FN_FILE"
grep -q "^plan_audio_slots() {" "$FN_FILE" || { echo "FAIL: plan_audio_slots not found in $DEMUX"; exit 1; }
# shellcheck source=/dev/null
source "$FN_FILE"

V="$WORK/v.pts"; A="$WORK/a.pkt"; S="$WORK/slots"; P="$WORK/plan"; ST="$WORK/stats"; H="$WORK/holes"; SI="$WORK/sil"
gen_video() {   # START N [SKIP_FROM SKIP_N] - N pictures at 20 ms from START, optionally a hole
    LC_ALL=C awk -v s="$1" -v n="$2" -v hf="${3:--1}" -v hn="${4:-0}" 'BEGIN { for (i = 0; i < n; i++) { if (hf >= 0 && i >= hf && i < hf + hn) continue; printf "%.6f\n", s + i * 0.02 } }'
}
gen_audio() {   # START N [SKIP_FROM SKIP_N] - N frames at 24 ms, size 576
    LC_ALL=C awk -v s="$1" -v n="$2" -v hf="${3:--1}" -v hn="${4:-0}" 'BEGIN { for (i = 0; i < n; i++) { if (hf >= 0 && i >= hf && i < hf + hn) continue; printf "%.6f 576\n", s + i * 0.024 } }'
}
stat_of() { grep -oP "(^| )$1=\K[^ ]+" "$ST"; }
count_op() { grep -c "^$1 " "$P"; }
mid_sil() { grep -c . "$SI"; }     # silence inserts inside the recording (head and tail excluded)
run_plan() { video_slots "$V" "${MODE:-h26x}" "$1" 0.02 "$S" "$H" && plan_audio_slots "$S" "$A" 0.02 0.024 "$P" "$ST" "$SI"; }

run_checks() {
    # 1. healthy: 500 pictures (10 s), audio from 0.5 s before to the end (frame 437
    #    starts at 109.988, inside the last slot) -> head drop, no mid edits.
    #    "d" ops in the plan include the head, so mid-stream drops are read from the stats.
    gen_video 100.0 500 > "$V"; gen_audio 99.5 438 > "$A"; run_plan 100.0
    [ "$(stat_of dropped)" = 0 ] && [ "$(count_op s)" = 0 ] && [ "$(count_op k)" = 417 ] && ok "$IMPL: healthy: no mid-stream edits, 417 kept" || bad "$IMPL: healthy: dropped=$(stat_of dropped) s=$(count_op s) k=$(count_op k)"
    [ "$(stat_of head_dropped)" = 21 ] && ok "$IMPL: healthy: 21 head frames dropped (0.5 s = 20.83 frames, frame 21 at 100.004 is the first in slot 0)" || bad "$IMPL: healthy: head_dropped=$(stat_of head_dropped)"
    [ "$(stat_of start_offset_ms)" = 4 ] && ok "$IMPL: healthy: start_offset_ms=4" || bad "$IMPL: healthy: start_offset_ms=$(stat_of start_offset_ms)"
    [ "$(stat_of tail_silence)" = 0 ] && [ "$(stat_of tail_dropped)" = 0 ] && [ ! -s "$H" ] && [ ! -s "$SI" ] && ok "$IMPL: healthy: no holes, no silence inserts, no tail edits" || bad "$IMPL: healthy: holes/silence written, tail_silence=$(stat_of tail_silence) tail_dropped=$(stat_of tail_dropped)"

    # 1b. audio runs 0.54 s past the last picture: those frames are dropped as tail, not as holes
    gen_video 100.0 500 > "$V"; gen_audio 99.5 460 > "$A"; run_plan 100.0
    [ "$(stat_of tail_dropped)" = 22 ] && [ "$(stat_of dropped)" = 0 ] && ok "$IMPL: long track: 22 tail frames dropped" || bad "$IMPL: long track: tail_dropped=$(stat_of tail_dropped) dropped=$(stat_of dropped)"

    # 1c. audio ends 1 s before the last picture: tail silence
    gen_video 100.0 500 > "$V"; gen_audio 99.5 396 > "$A"; run_plan 100.0
    TS=$(stat_of tail_silence); [ "$TS" -ge 41 ] && [ "$TS" -le 42 ] && [ "$(tail -1 "$P")" = "s $TS" ] && [ ! -s "$SI" ] && ok "$IMPL: short track: $TS tail silence frames" || bad "$IMPL: short track: tail_silence=$TS last op $(tail -1 "$P")"

    # 2. hole in video only (pictures 200-249 missing = 1 s): audio of that second dropped
    gen_video 100.0 500 200 50 > "$V"; gen_audio 99.5 438 > "$A"; run_plan 100.0
    D=$(stat_of dropped); [ "$D" -ge 41 ] && [ "$D" -le 42 ] && ok "$IMPL: video hole: $D frames dropped (~1 s / 24 ms)" || bad "$IMPL: video hole: dropped $D"
    [ "$(count_op s)" = 0 ] && ok "$IMPL: video hole: no silence" || bad "$IMPL: video hole: silence inserted"
    read -r hs he hms < "$H"; [ "$hms" = 1000 ] && ok "$IMPL: holes file: 1000 ms at $hs-$he" || bad "$IMPL: holes file: $(cat "$H")"

    # 3. hole in audio only (frames 200-241 missing = 1.008 s): silence for those slots
    gen_video 100.0 500 > "$V"; gen_audio 99.5 438 200 42 > "$A"; run_plan 100.0
    SIL=$(stat_of silence); [ "$SIL" -ge 41 ] && [ "$SIL" -le 43 ] && ok "$IMPL: audio hole: $SIL silence frames" || bad "$IMPL: audio hole: silence=$SIL"
    [ "$(stat_of dropped)" = 0 ] && ok "$IMPL: audio hole: nothing dropped" || bad "$IMPL: audio hole: dropped $(stat_of dropped)"
    [ "$(wc -l < "$SI")" = 1 ] && ok "$IMPL: silence positions: one insert" || bad "$IMPL: silence positions: $(cat "$SI")"

    # 4. both lost the same second (pictures 104.0-105.0, audio frames 188-229 = 104.012-105.020): nothing to do
    gen_video 100.0 500 200 50 > "$V"; gen_audio 99.5 438 188 42 > "$A"; run_plan 100.0
    [ "$(count_op s)" = 0 ] && [ "$(stat_of dropped)" -le 1 ] && ok "$IMPL: A+V hole: no silence, at most 1 edge frame dropped" || bad "$IMPL: A+V hole: dropped=$(stat_of dropped) s=$(count_op s)"

    # 5. exact half-slot start (audio 36 ms = 1.5 frames after V0): no single-frame flapping
    gen_video 100.0 500 > "$V"; gen_audio 100.036 400 > "$A"; run_plan 100.0
    [ "$(count_op d)" = 0 ] && [ "$(mid_sil)" = 0 ] && [ "$(head -1 "$P")" = "s 1" ] && [ "$(stat_of start_offset_ms)" = 12 ] && ok "$IMPL: half-slot start: no flapping (tie goes to the earlier slot: 1 leading silence frame, offset +12 ms)" || bad "$IMPL: half-slot: d=$(count_op d) mid silence=$(mid_sil) first op $(head -1 "$P")"

    # 6. late track (audio starts 0.1 s after the picture): 4 leading silence
    #    frames (96 ms) and the remainder of 4 ms as the start offset
    gen_video 100.0 500 > "$V"; gen_audio 100.1 400 > "$A"; run_plan 100.0
    [ "$(head -1 "$P")" = "s 4" ] && [ "$(count_op d)" = 0 ] && ok "$IMPL: late track: 4 leading silence frames, nothing dropped" || bad "$IMPL: late track: first op $(head -1 "$P"), d=$(count_op d)"
    [ "$(stat_of start_offset_ms)" = 4 ] && ok "$IMPL: late track: start_offset_ms=4 (the remainder, not the 100 ms)" || bad "$IMPL: late track: start_offset_ms=$(stat_of start_offset_ms)"

    # 7. a stray PTS in the video (one corrupt packet) is no run split, and the
    #    picture goes back to the place it left: 500 slots, no hole, no audio
    #    edit - whether the stray lies 6 s ahead or 50 s behind (a stray low
    #    PTS must not become slot 0)
    for CASE in "h26x 111.000000" "h26x 50.000000" "mpeg2 50.000000"; do
        MODE=${CASE% *}; STRAY=${CASE#* }
        gen_video 100.0 500 > "$V"; sed -i "300s/.*/$STRAY/" "$V"; gen_audio 99.5 438 > "$A"; run_plan 100.0
        [ "$(cut -d' ' -f1 "$S" | sort -u | wc -l)" = 1 ] && ok "$IMPL: stray PTS $STRAY ($MODE): one run" || bad "$IMPL: stray PTS $STRAY ($MODE): $(cut -d' ' -f1 "$S" | sort -u | wc -l) runs"
        [ "$(wc -l < "$S")" = 500 ] && [ "$(awk 'NR == 1 { print $3 }' "$S")" = "100.000000" ] && [ ! -s "$H" ] \
            && [ "$(stat_of dropped)" = 0 ] && [ "$(stat_of head_dropped)" = 21 ] && [ "$(count_op s)" = 0 ] \
            && ok "$IMPL: stray PTS $STRAY ($MODE): picture back in its place, 500 slots, no hole, no edit" \
            || bad "$IMPL: stray PTS $STRAY ($MODE): $(wc -l < "$S") slots from $(awk 'NR == 1 { print $3 }' "$S"), holes '$(head -1 "$H")', dropped=$(stat_of dropped) head=$(stat_of head_dropped) s=$(count_op s)"
    done
    MODE=h26x
    # 7b. a stray that left no place (an extra packet with a wild PTS) gets no slot
    gen_video 100.0 500 > "$V"; sed -i '300a 70.000000' "$V"; gen_audio 99.5 438 > "$A"; MODE=mpeg2 run_plan 100.0
    [ "$(wc -l < "$S")" = 500 ] && [ ! -s "$H" ] && [ "$(stat_of dropped)" = 0 ] \
        && ok "$IMPL: stray PTS without a place: no slot, no hole" || bad "$IMPL: stray without a place: $(wc -l < "$S") slots, holes '$(head -1 "$H")'"
    # 7c. the pictures on both sides of a real hole are no strays
    { gen_video 100.0 250; gen_video 110.0 250; } > "$V"; gen_audio 99.5 800 > "$A"; MODE=mpeg2 run_plan 100.0
    [ "$(wc -l < "$S")" = 500 ] && grep -q '^105.000000 110.000000 5000$' "$H" \
        && ok "$IMPL: a 5 s hole stays a hole, its edges keep their slots" || bad "$IMPL: 5 s hole: $(wc -l < "$S") slots, holes '$(head -1 "$H")'"

    # 8. PTS wrap: second half restarts at 5.0; audio wraps at the same place
    { gen_video 95440.0 250; gen_video 5.0 250; } > "$V"; { gen_audio 95439.5 229; gen_audio 5.004 208; } > "$A"; run_plan 95440.0
    [ "$(cut -d' ' -f1 "$S" | sort -u | wc -l)" = 2 ] && ok "$IMPL: wrap: two runs" || bad "$IMPL: wrap: runs $(cut -d' ' -f1 "$S" | sort -u | wc -l)"
    [ "$(stat_of dropped)" -le 1 ] && [ "$(mid_sil)" -le 1 ] && [ "$(stat_of head_dropped)" = 21 ] && ok "$IMPL: wrap: no edits beyond one edge frame" || bad "$IMPL: wrap: dropped=$(stat_of dropped) mid silence=$(mid_sil) head=$(stat_of head_dropped)"

    # 9. N/A continues: a packet without PTS keeps its bytes in place
    gen_video 100.0 500 > "$V"; gen_audio 99.5 438 > "$A"; sed -i '100s/.*/N\/A 576/' "$A"; run_plan 100.0
    [ "$(stat_of dropped)" = 0 ] && [ "$(count_op s)" = 0 ] && [ "$(count_op k)" = 417 ] && ok "$IMPL: N/A continues: 417 kept, nothing edited" || bad "$IMPL: N/A: k=$(count_op k) dropped=$(stat_of dropped) s=$(count_op s)"

    # 10. mpeg2 mode: frame 0 is the smallest PTS (leading B pictures shown); h26x drops them
    { echo 100.04; echo 100.00; echo 100.02; gen_video 100.06 497; } > "$V"; gen_audio 99.5 438 > "$A"
    MODE=mpeg2 run_plan 100.00; [ "$(wc -l < "$S")" = 500 ] && ok "$IMPL: mpeg2: 500 slots, leading B kept" || bad "$IMPL: mpeg2: $(wc -l < "$S") slots"
    MODE=h26x run_plan 100.04; [ "$(wc -l < "$S")" = 498 ] && ok "$IMPL: h26x: 498 slots, two leading pictures dropped" || bad "$IMPL: h26x: $(wc -l < "$S") slots"

    # 11. field pictures (H.264 PAFF: two packets per frame): 250 frames at 25 fps
    #     (40 ms), the second field 20 ms later / with the same PTS / without one
    #     -> one slot per FRAME in all three cases, and the audio stays unedited
    fields() { LC_ALL=C awk -v m="$1" 'BEGIN { for (i = 0; i < 250; i++) { printf "%.6f\n", 100 + i * 0.04; if (m == "half") printf "%.6f\n", 100.02 + i * 0.04; else if (m == "same") printf "%.6f\n", 100 + i * 0.04; else print "N/A" } }'; }
    for FM in half same none; do
        fields "$FM" > "$V"; gen_audio 99.5 438 > "$A"
        video_slots "$V" h26x 100.0 0.04 "$S" "$H" && plan_audio_slots "$S" "$A" 0.04 0.024 "$P" "$ST" "$SI"
        [ "$(wc -l < "$S")" = 250 ] && [ "$(stat_of dropped)" = 0 ] && [ "$(count_op s)" = 0 ] && [ ! -s "$H" ] \
            && ok "$IMPL: field pairs ($FM): 250 slots, no edits" || bad "$IMPL: field pairs ($FM): $(wc -l < "$S") slots, dropped=$(stat_of dropped) s=$(count_op s)"
    done

    # 12. two runs with a hole in the second: the hole position is on the slot
    #     timeline (frame 0 PTS + slot time + what was lost before), not the wrapped PTS
    { gen_video 95440.0 250; gen_video 5.0 250 100 50; } > "$V"; { gen_audio 95439.5 229; gen_audio 5.004 208; } > "$A"; run_plan 95440.0
    read -r hs he hms < "$H"; [ "$hs" = "95447.000000" ] && [ "$he" = "95448.000000" ] && [ "$hms" = 1000 ] \
        && ok "$IMPL: hole in run 2 at slot 350 = 95447.0 s on the slot timeline" || bad "$IMPL: hole in run 2: $(cat "$H")"

    # 14. a track 12 ms into the picture after the head drop (Das Erste HD: MP2
    #     starts 732 ms before frame 0 = 30.5 frames): the tie takes slot 0,
    #     no silence frame in front of the first real one
    gen_video 100.0 500 > "$V"; gen_audio 99.268 438 > "$A"; run_plan 100.0
    [ "$(grep -m1 -v '^d ' "$P" | cut -d' ' -f1)" = k ] && [ "$(stat_of head_dropped)" = 31 ] && [ "$(stat_of start_offset_ms)" = 12 ] \
        && ok "$IMPL: half-frame head: frame 31 on slot 0, offset +12 ms, no silence in front" || bad "$IMPL: half-frame head: first op after the drops '$(grep -m1 -v '^d ' "$P")' head=$(stat_of head_dropped) off=$(stat_of start_offset_ms)"

    # 15. the recording ends inside a GOP: the pictures not yet transmitted
    #     leave holes among the last 16 slots. They are holes for the audio,
    #     but no reported damage; a hole further in is reported.
    gen_video 100.0 500 > "$V"; sed -i -e '496d;498d' "$V"; gen_audio 99.5 438 > "$A"; run_plan 100.0
    [ ! -s "$H" ] && [ "$(wc -l < "$S")" = 498 ] && ok "$IMPL: truncated last GOP: 498 slots, no hole reported" || bad "$IMPL: truncated last GOP: $(wc -l < "$S") slots, holes: $(cat "$H")"
    gen_video 100.0 500 > "$V"; sed -i -e '401d;496d' "$V"; run_plan 100.0
    [ "$(wc -l < "$H")" = 1 ] && grep -q '^108.000000 108.020000 20$' "$H" && ok "$IMPL: hole at slot 400 reported, the one in the last GOP not" || bad "$IMPL: holes: $(cat "$H")"

    # 16. durations as fractions: 29.97 fps and 44.1 kHz are no finite
    #     decimals. One hour each; a duration rounded to 6 decimals drifts by
    #     more than an audio frame and would insert silence mid-stream.
    #     (One hour of 29.97 fps video; 30 min of 44.1 kHz audio.)
    LC_ALL=C awk 'BEGIN { for (i = 0; i < 107892; i++) printf "%.6f\n", 100 + i * 1001 / 30000 }' > "$V"
    LC_ALL=C awk 'BEGIN { for (i = 0; i < 150030; i++) printf "%.6f 576\n", 99.5 + i * 0.024 }' > "$A"
    video_slots "$V" h26x 100.0 1001/30000 "$S" "$H" && plan_audio_slots "$S" "$A" 1001/30000 1152/48000 "$P" "$ST" "$SI"
    [ "$(wc -l < "$S")" = 107892 ] && [ ! -s "$H" ] && [ ! -s "$SI" ] && [ "$(stat_of dropped)" = 0 ] && [ "$(stat_of tail_silence)" = 0 ] && [ "$(stat_of tail_dropped)" -lt 20 ] \
        && ok "$IMPL: 29.97 fps, one hour: no hole, no mid-stream edit" || bad "$IMPL: 29.97 fps: slots $(wc -l < "$S"), holes '$(head -1 "$H")', silence inserts '$(head -2 "$SI" | tr '\n' ' ')', dropped=$(stat_of dropped) tail_silence=$(stat_of tail_silence)"
    gen_video 100.0 90000 > "$V"
    LC_ALL=C awk 'BEGIN { for (i = 0; i < 68940; i++) printf "%.6f 626\n", 99.5 + i * 1152 / 44100 }' > "$A"
    video_slots "$V" h26x 100.0 1/50 "$S" "$H" && plan_audio_slots "$S" "$A" 1/50 1152/44100 "$P" "$ST" "$SI"
    [ ! -s "$SI" ] && [ "$(stat_of dropped)" = 0 ] && [ "$(stat_of tail_silence)" = 0 ] && [ "$(stat_of tail_dropped)" -lt 80 ] \
        && ok "$IMPL: 44.1 kHz audio, 30 min: no mid-stream edit" || bad "$IMPL: 44.1 kHz: silence inserts '$(head -2 "$SI" | tr '\n' ' ')', dropped=$(stat_of dropped) tail_dropped=$(stat_of tail_dropped) tail_silence=$(stat_of tail_silence)"

    # 13. start offset never prints "-0"
    gen_video 100.0 500 > "$V"; gen_audio 99.9998 438 > "$A"; run_plan 100.0
    [ "$(stat_of start_offset_ms)" = 0 ] && ok "$IMPL: start offset -0.2 ms prints 0" || bad "$IMPL: start_offset_ms=$(stat_of start_offset_ms)"
}

# Every awk on this machine: the script must not depend on gawk, and mawk
# follows LC_NUMERIC (the checks run in the caller's locale on purpose).
RAN=0
for IMPL in gawk mawk; do
    command -v "$IMPL" > /dev/null || { echo "note: $IMPL not installed, skipped"; continue; }
    SHIM="$WORK/bin-$IMPL"; mkdir -p "$SHIM"; ln -sf "$(command -v "$IMPL")" "$SHIM/awk"
    ( PATH="$SHIM:$PATH"; PASS=0; FAIL=0; run_checks; echo "$PASS $FAIL" > "$WORK/.count" )
    read -r p f < "$WORK/.count"; PASS=$((PASS + p)); FAIL=$((FAIL + f)); RAN=$((RAN + 1))
done
[ "$RAN" -gt 0 ] || { echo "FAIL: neither gawk nor mawk found"; exit 1; }

echo "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
