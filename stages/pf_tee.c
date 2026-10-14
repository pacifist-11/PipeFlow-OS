#define _POSIX_C_SOURCE 200809L
#include "record.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

int main(int argc, char *argv[])
{
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, NULL);

    int out_fds[16];
    int out_count = 0;

    // Always mirror to stdout as output 0
    out_fds[out_count++] = STDOUT_FILENO;

    // Additional target files or FIFOs
    for (int i = 1; i < argc && out_count < 16; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            out_fds[out_count++] = fd;
        } else {
            perror("[pf_tee] Failed to open target file/fifo");
        }
    }

    fprintf(stderr, "[pf_tee] Fanning out stream from stdin to %d destinations...\n", out_count);

    char buffer[4096];
    long count = 0;

    while (1) {
        pf_rec_status_t rst = record_read_line(STDIN_FILENO, buffer, sizeof(buffer));
        if (rst == PF_REC_EOF) break;
        if (rst != PF_REC_OK) break;

        for (int i = 0; i < out_count; i++) {
            if (out_fds[i] >= 0) {
                pf_rec_status_t wst = record_write_line(out_fds[i], buffer);
                if (wst == PF_REC_EPIPE) {
                    close(out_fds[i]);
                    out_fds[i] = -1; // Detach closed branch
                }
            }
        }
        count++;
    }

    for (int i = 1; i < out_count; i++) {
        if (out_fds[i] >= 0) close(out_fds[i]);
    }

    fprintf(stderr, "[pf_tee] Duplicated %ld records to %d destinations cleanly.\n", count, out_count);
    return 0;
}
