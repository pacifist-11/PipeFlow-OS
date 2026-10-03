#define _POSIX_C_SOURCE 200809L
#include "record.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

int main(int argc, char *argv[])
{
    // Ignore SIGPIPE so write() returns EPIPE instead of terminating abruptly
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, NULL);

    int count = 10;
    int delay_ms = 0;
    const char *prefix = "DATA_RECORD";

    if (argc > 1) {
        count = atoi(argv[1]);
    }
    if (argc > 2) {
        delay_ms = atoi(argv[2]);
    }
    if (argc > 3) {
        prefix = argv[3];
    }

    fprintf(stderr, "[pf_source] Generating %d records (prefix='%s', delay=%dms)...\n",
            count, prefix, delay_ms);

    struct timespec ts;
    ts.tv_sec = delay_ms / 1000;
    ts.tv_nsec = (delay_ms % 1000) * 1000000L;

    for (int i = 1; i <= count; i++) {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "[%04d] %s-PAYLOAD-%d\n", i, prefix, i);

        // Write line record to stdout (piped downstream)
        pf_rec_status_t st = record_write_line(STDOUT_FILENO, buffer);
        if (st == PF_REC_EPIPE) {
            fprintf(stderr, "[pf_source] Downstream stage closed pipe (EPIPE). Terminating gracefully at record %d.\n", i);
            return 0; // Graceful termination
        } else if (st != PF_REC_OK) {
            fprintf(stderr, "[pf_source] Write error: %s\n", strerror(errno));
            return 1;
        }

        if (delay_ms > 0) {
            nanosleep(&ts, NULL);
        }
    }

    fprintf(stderr, "[pf_source] Finished generating %d records. Clean EOF sent.\n", count);
    return 0;
}
