#define _POSIX_C_SOURCE 200809L
#include "record.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int limit = 5;
    if (argc > 1) {
        limit = atoi(argv[1]);
    }

    fprintf(stderr, "[pf_head] Limiting stream to %d records, then exiting early...\n", limit);

    char buffer[4096];
    int count = 0;

    while (count < limit) {
        pf_rec_status_t st = record_read_line(STDIN_FILENO, buffer, sizeof(buffer));
        if (st == PF_REC_EOF) {
            fprintf(stderr, "[pf_head] Upstream EOF reached before limit.\n");
            break;
        }
        if (st != PF_REC_OK) {
            fprintf(stderr, "[pf_head] Read error\n");
            return 1;
        }

        record_write_line(STDOUT_FILENO, buffer);
        count++;
    }

    fprintf(stderr, "[pf_head] Limit (%d) reached. Exiting early to trigger upstream SIGPIPE test.\n", count);
    // Closing STDIN and STDOUT is performed automatically upon exit
    return 0;
}
