#define _POSIX_C_SOURCE 200809L
#include "record.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <output_filepath>\n", argv[0]);
        return 1;
    }

    const char *filepath = argv[1];
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("[pf_file_sink] Failed to open output file");
        return 1;
    }

    fprintf(stderr, "[pf_file_sink] Writing records to '%s'...\n", filepath);

    char buffer[4096];
    long count = 0;

    while (1) {
        pf_rec_status_t rst = record_read_line(STDIN_FILENO, buffer, sizeof(buffer));
        if (rst == PF_REC_EOF) break;
        if (rst != PF_REC_OK) {
            fprintf(stderr, "[pf_file_sink] Read error\n");
            close(fd);
            return 1;
        }

        pf_rec_status_t wst = record_write_line(fd, buffer);
        if (wst != PF_REC_OK) {
            fprintf(stderr, "[pf_file_sink] Write error\n");
            close(fd);
            return 1;
        }
        count++;
    }

    close(fd);
    fprintf(stderr, "[pf_file_sink] Saved %ld records to '%s'. Clean EOF.\n", count, filepath);
    return 0;
}
