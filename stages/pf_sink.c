#define _POSIX_C_SOURCE 200809L
#include "record.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

int main(int argc, char *argv[])
{
    bool quiet = false;
    int delay_ms = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            quiet = true;
        } else if ((strcmp(argv[i], "--delay-ms") == 0 || strcmp(argv[i], "-d") == 0) && i + 1 < argc) {
            delay_ms = atoi(argv[++i]);
        }
    }

    fprintf(stderr, "[pf_sink] Ready. Reading records from upstream until EOF (delay=%dms)...\n", delay_ms);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    struct timespec ts_delay;
    ts_delay.tv_sec = delay_ms / 1000;
    ts_delay.tv_nsec = (delay_ms % 1000) * 1000000L;

    char buffer[4096];
    long total_records = 0;
    long total_bytes = 0;
    double total_io_blocked_sec = 0.0;

    while (1) {
        struct timespec t_r0, t_r1;
        clock_gettime(CLOCK_MONOTONIC, &t_r0);

        pf_rec_status_t st = record_read_line(STDIN_FILENO, buffer, sizeof(buffer));

        clock_gettime(CLOCK_MONOTONIC, &t_r1);
        double dt = (t_r1.tv_sec - t_r0.tv_sec) + (t_r1.tv_nsec - t_r0.tv_nsec) / 1e9;
        total_io_blocked_sec += dt;

        if (st == PF_REC_EOF) {
            break;
        }
        if (st != PF_REC_OK) {
            fprintf(stderr, "[pf_sink] Read error\n");
            return 1;
        }

        total_records++;
        total_bytes += (long)strlen(buffer);

        if (!quiet) {
            printf("%s", buffer);
        }

        if (delay_ms > 0) {
            nanosleep(&ts_delay, NULL);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed_sec = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double rate = elapsed_sec > 0 ? (total_records / elapsed_sec) : total_records;

    fprintf(stderr, "\n==================== SINK METRICS ====================\n");
    fprintf(stderr, " Total Records Processed : %ld\n", total_records);
    fprintf(stderr, " Total Bytes Processed   : %ld bytes\n", total_bytes);
    fprintf(stderr, " Total I/O Wait (Read)   : %.6f seconds\n", total_io_blocked_sec);
    fprintf(stderr, " Total Run Duration      : %.6f seconds\n", elapsed_sec);
    fprintf(stderr, " Throughput Rate         : %.2f records/sec\n", rate);
    fprintf(stderr, " Clean Upstream EOF      : YES\n");
    fprintf(stderr, "======================================================\n");

    return 0;
}
