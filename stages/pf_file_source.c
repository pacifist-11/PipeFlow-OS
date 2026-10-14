#define _POSIX_C_SOURCE 200809L
#include "record.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

int main(int argc, char *argv[])
{
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, NULL);

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_filepath>\n", argv[0]);
        return 1;
    }

    const char *filepath = argv[1];
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        perror("[pf_file_source] Failed to open input file");
        return 1;
    }

    fprintf(stderr, "[pf_file_source] Reading records from '%s'...\n", filepath);

    char buffer[4096];
    long count = 0;

    while (1) {
        pf_rec_status_t rst = record_read_line(fd, buffer, sizeof(buffer));
        if (rst == PF_REC_EOF) break;
        if (rst != PF_REC_OK) {
            fprintf(stderr, "[pf_file_source] Read error\n");
            close(fd);
            return 1;
        }

        pf_rec_status_t wst = record_write_line(STDOUT_FILENO, buffer);
        if (wst == PF_REC_EPIPE) {
            fprintf(stderr, "[pf_file_source] Downstream stage closed pipe (EPIPE). Exiting cleanly.\n");
            close(fd);
            return 0;
        } else if (wst != PF_REC_OK) {
            fprintf(stderr, "[pf_file_source] Write error\n");
            close(fd);
            return 1;
        }
        count++;
    }

    close(fd);
    fprintf(stderr, "[pf_file_source] Streamed %ld records from '%s'. Clean EOF.\n", count, filepath);
    return 0;
}
