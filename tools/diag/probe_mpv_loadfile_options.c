/* probe_mpv_loadfile_options.c — how does libmpv parse the fourth positional
 * argument of `loadfile` (the per-file options list)? Built while chasing the
 * lost subtitles of a recording with a comma in its name (2026-09-05): the
 * list is comma-separated and split BEFORE the values are parsed, so a path
 * with a comma ends there. Measured with this probe (argv exactly as
 * TTMpvLibBackend::command builds it):
 *   sub-file=/x/a,b.srt,pause=yes     -> "Error parsing option b.srt,pause"
 *   sub-file="/x/a,b.srt",pause=yes   -> works, but breaks on a " in the path
 *   sub-file=[/x/a,b.srt],pause=yes   -> works, but breaks on a ] in the path
 *   sub-file=%12%/x/a,b.srt,pause=yes -> works for any value; n = UTF-8 bytes
 *                                        (character count fails on umlauts)
 * The %n% form is what ttMpvLoadfileCommand() emits; the regression gate is
 * tools/diag/test_mpv_loadfile_args. This file is not part of the build:
 *   gcc -o probe_mpv_loadfile_options probe_mpv_loadfile_options.c $(pkg-config --cflags --libs mpv)
 *   ./probe_mpv_loadfile_options <video> '<options string>'
 * prints loadfile's rc, mpv's error lines and, once the file is loaded,
 * track-list/count, sid and pause. */
#include <mpv/client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    mpv_handle *m = mpv_create();
    mpv_set_option_string(m, "vo", "null");
    mpv_set_option_string(m, "ao", "null");
    mpv_set_option_string(m, "idle", "yes");
    mpv_set_option_string(m, "terminal", "no");
    mpv_request_log_messages(m, "error");
    if (mpv_initialize(m) < 0) return 3;
    const char *cmd[] = {"loadfile", argv[1], "replace", "0", argv[2], NULL};
    int rc = mpv_command(m, cmd);
    printf("loadfile rc=%d (%s)\n", rc, mpv_error_string(rc));
    for (int i = 0; i < 200; i++) {
        mpv_event *ev = mpv_wait_event(m, 0.1);
        if (ev->event_id == MPV_EVENT_LOG_MESSAGE) {
            mpv_event_log_message *lm = ev->data; printf("log[%s]: %s", lm->prefix, lm->text);
        } else if (ev->event_id == MPV_EVENT_FILE_LOADED) {
            char *n = mpv_get_property_string(m, "track-list/count");
            char *sid = mpv_get_property_string(m, "sid");
            char *pause = mpv_get_property_string(m, "pause");
            printf("FILE_LOADED tracks=%s sid=%s pause=%s\n", n ? n : "?", sid ? sid : "?", pause ? pause : "?");
            mpv_free(n); mpv_free(sid); mpv_free(pause);
            break;
        } else if (ev->event_id == MPV_EVENT_END_FILE) { printf("END_FILE (load failed)\n"); break; }
    }
    mpv_terminate_destroy(m);
    return 0;
}
