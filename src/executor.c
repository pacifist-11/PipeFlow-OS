#define _POSIX_C_SOURCE 200809L
#include "executor.h"
#include "signals.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

int executor_run_pipeline(pipeline_t *pipeline, const exec_options_t *opts)
{
    if (!pipeline || pipeline->stage_count == 0) {
        fprintf(stderr, "PipeFlow: empty pipeline\n");
        return -1;
    }

    int n = pipeline->stage_count;
    int (*pipes)[2] = NULL;

    if (n > 1) {
        pipes = malloc((n - 1) * sizeof(int[2]));
        if (!pipes) {
            perror("PipeFlow: malloc pipes failed");
            return -1;
        }

        for (int i = 0; i < n - 1; i++) {
            if (pipe(pipes[i]) < 0) {
                perror("PipeFlow: pipe() creation failed");
                // Close previously created pipes
                for (int j = 0; j < i; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
                free(pipes);
                return -1;
            }
        }
    }

    // Register active pipeline with signal subsystem
    signals_setup_parent(pipeline);
    pipeline->is_running = true;
    pipeline->pgid = 0;

    if (opts && opts->timeout_sec > 0) {
        signals_set_timeout(opts->timeout_sec);
    }

    // Fork and wire all stages
    for (int i = 0; i < n; i++) {
        stage_t *stage = pipeline->stages[i];

        pid_t pid = fork();
        if (pid < 0) {
            perror("PipeFlow: fork() failed");
            stage->state = STAGE_STATE_FAILED;
            // Clean up all running children
            pipeline_teardown(pipeline);
            break;
        }

        if (pid == 0) {
            // === CHILD PROCESS ===
            // 1. Process group management
            if (i == 0) {
                setpgid(0, 0); // Becomes group leader
            } else {
                setpgid(0, pipeline->pgid); // Join group leader
            }

            // 2. Determine input and output file descriptors
            int in_fd = (i == 0) ? STDIN_FILENO : pipes[i - 1][0];
            int out_fd = (i == n - 1) ? STDOUT_FILENO : pipes[i][1];

            // 3. Duplicate descriptors onto STDIN and STDOUT
            if (in_fd != STDIN_FILENO) {
                if (dup2(in_fd, STDIN_FILENO) < 0) {
                    perror("PipeFlow child: dup2(in_fd) failed");
                    _exit(1);
                }
            }
            if (out_fd != STDOUT_FILENO) {
                if (dup2(out_fd, STDOUT_FILENO) < 0) {
                    perror("PipeFlow child: dup2(out_fd) failed");
                    _exit(1);
                }
            }

            // 4. Critical: Close ALL pipe descriptors in child to ensure EOF propagation
            if (n > 1) {
                for (int k = 0; k < n - 1; k++) {
                    close(pipes[k][0]);
                    close(pipes[k][1]);
                }
            }

            // 5. Reset signal dispositions to defaults
            signals_setup_child();

            // 6. Execute program via PATH resolution
            execvp(stage->command, stage->argv);

            // If execvp returns, an error occurred
            fprintf(stderr, "PipeFlow: stage '%s' failed to execute '%s': %s\n",
                    stage->name, stage->command, strerror(errno));
            _exit(127);
        }

        // === PARENT PROCESS ===
        stage->pid = pid;
        stage->state = STAGE_STATE_RUNNING;

        if (i == 0) {
            pipeline->pgid = pid; // First child is process group leader
            setpgid(pid, pid);
        } else {
            setpgid(pid, pipeline->pgid);
        }
    }

    // Critical: Close all pipe descriptors in parent process so writer closure reaches readers
    if (n > 1 && pipes) {
        for (int i = 0; i < n - 1; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
        free(pipes);
        pipes = NULL;
    }

    // Wait for all stages to complete and collect exit statuses
    int overall_status = 0;
    for (int i = 0; i < n; i++) {
        stage_t *stage = pipeline->stages[i];
        if (stage->pid <= 0) continue;

        int status = 0;
        pid_t wpid = waitpid(stage->pid, &status, 0);

        if (wpid < 0) {
            if (errno == EINTR) {
                if (g_timed_out) {
                    fprintf(stderr, "\nPipeFlow: Pipeline '%s' timed out! Tearing down...\n", pipeline->name);
                    pipeline_teardown(pipeline);
                } else if (g_interrupted) {
                    fprintf(stderr, "\nPipeFlow: Interrupt received on '%s'! Tearing down...\n", pipeline->name);
                    pipeline_teardown(pipeline);
                }
                // Retry waitpid
                i--;
                continue;
            }
            perror("PipeFlow: waitpid() error");
            stage->state = STAGE_STATE_FAILED;
            overall_status = -1;
            continue;
        }

        // Decode exit status (W5 milestone)
        if (WIFEXITED(status)) {
            stage->exit_code = WEXITSTATUS(status);
            stage->state = STAGE_STATE_EXITED;
            if (stage->exit_code != 0) {
                overall_status = stage->exit_code;
            }
        } else if (WIFSIGNALED(status)) {
            stage->term_sig = WTERMSIG(status);
            stage->state = STAGE_STATE_SIGNALED;
            overall_status = 128 + stage->term_sig;
        }
    }

    signals_clear_timeout();
    pipeline->is_running = false;

    if (opts && opts->verbose) {
        pipeline_print_table(pipeline);
    }

    return overall_status;
}
