#define _POSIX_C_SOURCE 200809L
#include "record.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

int main(int argc, char *argv[])
{
    // Ignore SIGPIPE so write() returns EPIPE
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, NULL);

    const char *pattern = "";
    if (argc > 1) {
        pattern = argv[1];
    }

    fprintf(stderr, "[pf_filter] Filtering records containing '%s'...\n", pattern);

    char buffer[4096];
    int matched = 0;
    int total = 0;

    while (1) {
        pf_rec_status_t st = record_read_line(STDIN_FILENO, buffer, sizeof(buffer));
        if (st == PF_REC_EOF) {
            break;
        }
        if (st != PF_REC_OK) {
            fprintf(stderr, "[pf_filter] Read error\n");
            return 1;
        }

        total++;
        if (strlen(pattern) == 0 || strstr(buffer, pattern) != NULL) {
            matched++;
            pf_rec_status_t wst = record_write_line(STDOUT_FILENO, buffer);
            if (wst == PF_REC_EPIPE) {
                fprintf(stderr, "[pf_filter] Downstream stage closed pipe (EPIPE). Terminating gracefully.\n");
                return 0;
            } else if (wst != PF_REC_OK) {
                fprintf(stderr, "[pf_filter] Write error\n");
                return 1;
            }
        }
    }

    fprintf(stderr, "[pf_filter] Passed %d / %d records matching '%s'.\n", matched, total, pattern);
    return 0;
}
