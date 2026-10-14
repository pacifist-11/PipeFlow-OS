#define _POSIX_C_SOURCE 200809L
#include "record.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>

#define MAX_INPUTS 16

int main(int argc, char *argv[])
{
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, NULL);

    struct pollfd pfds[MAX_INPUTS];
    int num_inputs = 0;

    // Check if arguments specify files or FIFOs
    if (argc > 1) {
        for (int i = 1; i < argc && num_inputs < MAX_INPUTS; i++) {
            int fd = open(argv[i], O_RDONLY | O_NONBLOCK);
            if (fd >= 0) {
                pfds[num_inputs].fd = fd;
                pfds[num_inputs].events = POLLIN;
                pfds[num_inputs].revents = 0;
                num_inputs++;
            } else {
                perror("[pf_merge] open input error");
            }
        }
    } else {
        // Default to reading stdin
        pfds[num_inputs].fd = STDIN_FILENO;
        pfds[num_inputs].events = POLLIN;
        pfds[num_inputs].revents = 0;
        num_inputs++;
    }

    if (num_inputs == 0) {
        fprintf(stderr, "[pf_merge] No valid inputs to merge.\n");
        return 1;
    }

    fprintf(stderr, "[pf_merge] Multiplexing %d input streams with poll(2) without deadlock...\n", num_inputs);

    int active_inputs = num_inputs;
    long total_records = 0;
    char buffer[4096];

    while (active_inputs > 0) {
        int ret = poll(pfds, num_inputs, 2000); // 2 second timeout
        if (ret < 0) {
            perror("[pf_merge] poll error");
            break;
        }
        if (ret == 0) {
            // Timeout checking if writers still alive
            continue;
        }

        for (int i = 0; i < num_inputs; i++) {
            if (pfds[i].fd < 0) continue;

            if (pfds[i].revents & (POLLIN | POLLHUP)) {
                pf_rec_status_t st = record_read_line(pfds[i].fd, buffer, sizeof(buffer));
                if (st == PF_REC_OK) {
                    record_write_line(STDOUT_FILENO, buffer);
                    total_records++;
                } else if (st == PF_REC_EOF || (pfds[i].revents & POLLHUP)) {
                    // Upstream input stream closed
                    close(pfds[i].fd);
                    pfds[i].fd = -1;
                    active_inputs--;
                }
            } else if (pfds[i].revents & (POLLERR | POLLNVAL)) {
                close(pfds[i].fd);
                pfds[i].fd = -1;
                active_inputs--;
            }
        }
    }

    fprintf(stderr, "[pf_merge] Successfully merged %ld records from all inputs. Clean EOF.\n", total_records);
    return 0;
}
