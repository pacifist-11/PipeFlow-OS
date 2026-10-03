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
    if (argc > 1 && strcmp(argv[1], "--quiet") == 0) {
        quiet = true;
    }

    fprintf(stderr, "[pf_sink] Ready. Reading records from upstream until EOF...\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    char buffer[4096];
    long total_records = 0;
    long total_bytes = 0;

    while (1) {
        pf_rec_status_t st = record_read_line(STDIN_FILENO, buffer, sizeof(buffer));
        if (st == PF_REC_EOF) {
            // Clean EOF from upstream
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
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed_sec = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double rate = elapsed_sec > 0 ? (total_records / elapsed_sec) : total_records;

    fprintf(stderr, "\n==================== SINK METRICS ====================\n");
    fprintf(stderr, " Total Records Processed : %ld\n", total_records);
    fprintf(stderr, " Total Bytes Processed   : %ld bytes\n", total_bytes);
    fprintf(stderr, " Elapsed Duration        : %.4f seconds\n", elapsed_sec);
    fprintf(stderr, " Throughput Rate         : %.2f records/sec\n", rate);
    fprintf(stderr, " Clean Upstream EOF      : YES\n");
    fprintf(stderr, "======================================================\n");

    return 0;
}
