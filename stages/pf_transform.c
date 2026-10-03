#define _POSIX_C_SOURCE 200809L
#include "record.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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

    const char *mode = "upper";
    if (argc > 1) {
        mode = argv[1];
    }

    fprintf(stderr, "[pf_transform] Running transform mode: %s\n", mode);

    char buffer[4096];
    int processed = 0;

    while (1) {
        pf_rec_status_t st = record_read_line(STDIN_FILENO, buffer, sizeof(buffer));
        if (st == PF_REC_EOF) {
            // Clean EOF from upstream
            break;
        }
        if (st != PF_REC_OK) {
            fprintf(stderr, "[pf_transform] Read error\n");
            return 1;
        }

        // Apply transformation
        size_t len = strlen(buffer);
        if (strcmp(mode, "upper") == 0) {
            for (size_t i = 0; i < len; i++) {
                buffer[i] = (char)toupper((unsigned char)buffer[i]);
            }
        } else if (strcmp(mode, "lower") == 0) {
            for (size_t i = 0; i < len; i++) {
                buffer[i] = (char)tolower((unsigned char)buffer[i]);
            }
        } else if (strcmp(mode, "reverse") == 0) {
            // Leave newline at end if present
            size_t end = (len > 0 && buffer[len - 1] == '\n') ? len - 2 : len - 1;
            size_t start = 0;
            while (start < end) {
                char tmp = buffer[start];
                buffer[start] = buffer[end];
                buffer[end] = tmp;
                start++;
                end--;
            }
        }

        // Write downstream
        pf_rec_status_t wst = record_write_line(STDOUT_FILENO, buffer);
        if (wst == PF_REC_EPIPE) {
            fprintf(stderr, "[pf_transform] Downstream stage closed pipe (EPIPE). Terminating gracefully.\n");
            return 0;
        } else if (wst != PF_REC_OK) {
            fprintf(stderr, "[pf_transform] Write error\n");
            return 1;
        }

        processed++;
    }

    fprintf(stderr, "[pf_transform] Processed %d records successfully.\n", processed);
    return 0;
}
