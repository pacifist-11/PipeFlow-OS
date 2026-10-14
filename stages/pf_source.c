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
    // Ignore SIGPIPE so write() returns EPIPE instead of crashing abruptly
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

    struct timespec start_total, end_total;
    clock_gettime(CLOCK_MONOTONIC, &start_total);

    double total_io_blocked_sec = 0.0;
    int records_sent = 0;
    bool caught_epipe = false;

    for (int i = 1; i <= count; i++) {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "[%04d] %s-PAYLOAD-%d\n", i, prefix, i);

        // Measure time spent blocked on write() I/O (backpressure metric)
        struct timespec t_w0, t_w1;
        clock_gettime(CLOCK_MONOTONIC, &t_w0);

        pf_rec_status_t st = record_write_line(STDOUT_FILENO, buffer);

        clock_gettime(CLOCK_MONOTONIC, &t_w1);
        double dt = (t_w1.tv_sec - t_w0.tv_sec) + (t_w1.tv_nsec - t_w0.tv_nsec) / 1e9;
        total_io_blocked_sec += dt;

        if (st == PF_REC_EPIPE) {
            fprintf(stderr, "[pf_source] Downstream stage closed pipe (EPIPE). Terminating gracefully at record %d.\n", i);
            caught_epipe = true;
            break;
        } else if (st != PF_REC_OK) {
            fprintf(stderr, "[pf_source] Write error: %s\n", strerror(errno));
            return 1;
        }

        records_sent++;

        if (delay_ms > 0) {
            nanosleep(&ts, NULL);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end_total);
    double elapsed_sec = (end_total.tv_sec - start_total.tv_sec) + (end_total.tv_nsec - start_total.tv_nsec) / 1e9;
    double rate = elapsed_sec > 0 ? (records_sent / elapsed_sec) : records_sent;

    fprintf(stderr, "\n==================== SOURCE METRICS ====================\n");
    fprintf(stderr, " Records Sent            : %d\n", records_sent);
    fprintf(stderr, " Total I/O Blocked Time  : %.6f seconds (Backpressure)\n", total_io_blocked_sec);
    fprintf(stderr, " Total Run Duration      : %.6f seconds\n", elapsed_sec);
    fprintf(stderr, " Throughput Rate         : %.2f records/sec\n", rate);
    fprintf(stderr, " EPIPE Caught Gracefully : %s\n", caught_epipe ? "YES" : "NO");
    fprintf(stderr, "========================================================\n");

    return 0;
}
