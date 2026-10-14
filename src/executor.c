#define _POSIX_C_SOURCE 200809L
#include "executor.h"
#include "signals.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

// Helper to spawn a single stage with given in/out fds
static pid_t spawn_stage(pipeline_t *pipeline, int stage_idx, int in_fd, int out_fd, const exec_options_t *opts)
{
    stage_t *stage = pipeline->stages[stage_idx];

    pid_t pid = fork();
    if (pid < 0) {
        perror("PipeFlow: fork() failed");
        stage->state = STAGE_STATE_FAILED;
        return -1;
    }

    if (pid == 0) {
        // === CHILD PROCESS ===
        // 1. Process group management
        if (pipeline->pgid == 0) {
            setpgid(0, 0); // Becomes group leader
        } else {
            setpgid(0, pipeline->pgid); // Join existing group leader
        }

        // 2. Log redirection if requested (Week 9 requirement)
        if (opts && opts->redirect_logs) {
            mkdir("logs", 0755);
            snprintf(stage->log_file, sizeof(stage->log_file), "logs/%s.log", stage->name);
            int log_fd = open(stage->log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (log_fd >= 0) {
                dup2(log_fd, STDERR_FILENO);
                close(log_fd);
            }
        }

        // 3. Duplicate descriptors onto STDIN and STDOUT
        if (in_fd != STDIN_FILENO && in_fd >= 0) {
            if (dup2(in_fd, STDIN_FILENO) < 0) {
                perror("PipeFlow child: dup2(in_fd) failed");
                _exit(1);
            }
        }
        if (out_fd != STDOUT_FILENO && out_fd >= 0) {
            if (dup2(out_fd, STDOUT_FILENO) < 0) {
                perror("PipeFlow child: dup2(out_fd) failed");
                _exit(1);
            }
        }

        // 4. Critical: Close ALL pipe descriptors across all edges in child
        for (int e = 0; e < pipeline->edge_count; e++) {
            if (pipeline->edges[e].pipe_fd[0] >= 0) close(pipeline->edges[e].pipe_fd[0]);
            if (pipeline->edges[e].pipe_fd[1] >= 0) close(pipeline->edges[e].pipe_fd[1]);
        }

        // 5. Reset signal dispositions to defaults
        signals_setup_child();

        // 6. Execute program via PATH resolution
        execvp(stage->command, stage->argv);

        // If execvp returns, execution failed
        fprintf(stderr, "PipeFlow: stage '%s' failed to execute '%s': %s\n",
                stage->name, stage->command, strerror(errno));
        _exit(127);
    }

    // === PARENT PROCESS ===
    stage->pid = pid;
    stage->state = STAGE_STATE_RUNNING;

    if (pipeline->pgid == 0) {
        pipeline->pgid = pid;
        setpgid(pid, pid);
    } else {
        setpgid(pid, pipeline->pgid);
    }

    return pid;
}

int executor_run_pipeline(pipeline_t *pipeline, const exec_options_t *opts)
{
    if (!pipeline || pipeline->stage_count == 0) {
        fprintf(stderr, "PipeFlow: empty pipeline\n");
        return -1;
    }

    // 1. Create pipes for all defined edges
    for (int e = 0; e < pipeline->edge_count; e++) {
        if (pipe(pipeline->edges[e].pipe_fd) < 0) {
            perror("PipeFlow: pipe() creation failed");
            // Close already opened pipes
            for (int k = 0; k < e; k++) {
                close(pipeline->edges[k].pipe_fd[0]);
                close(pipeline->edges[k].pipe_fd[1]);
            }
            return -1;
        }
    }

    // 2. Register active pipeline with signal subsystem
    signals_setup_parent(pipeline);
    pipeline->is_running = true;
    pipeline->pgid = 0;

    if (opts && opts->timeout_sec > 0) {
        signals_set_timeout(opts->timeout_sec);
    }

    // 3. Launch stages in topological dependency order (Kahn's DAG schedule)
    for (int step = 0; step < pipeline->stage_count; step++) {
        int idx = pipeline->topo_order[step];
        stage_t *stage = pipeline->stages[idx];

        // Determine input fd (from incoming edge)
        int in_fd = STDIN_FILENO;
        for (int e = 0; e < pipeline->edge_count; e++) {
            if (pipeline->edges[e].to_stage == idx) {
                in_fd = pipeline->edges[e].pipe_fd[0];
                break;
            }
        }

        // Determine output fd (from outgoing edge)
        int out_fd = STDOUT_FILENO;
        for (int e = 0; e < pipeline->edge_count; e++) {
            if (pipeline->edges[e].from_stage == idx) {
                out_fd = pipeline->edges[e].pipe_fd[1];
                break;
            }
        }

        stage->in_fd = in_fd;
        stage->out_fd = out_fd;

        if (spawn_stage(pipeline, idx, in_fd, out_fd, opts) < 0) {
            pipeline_teardown(pipeline);
            break;
        }
    }

    // 4. Critical: Close all pipe descriptors in parent process so EOF reaches readers
    for (int e = 0; e < pipeline->edge_count; e++) {
        if (pipeline->edges[e].pipe_fd[0] >= 0) {
            close(pipeline->edges[e].pipe_fd[0]);
            pipeline->edges[e].pipe_fd[0] = -1;
        }
        if (pipeline->edges[e].pipe_fd[1] >= 0) {
            close(pipeline->edges[e].pipe_fd[1]);
            pipeline->edges[e].pipe_fd[1] = -1;
        }
    }

    // 5. Wait for all stages to complete and collect exit statuses
    int overall_status = 0;
    for (int i = 0; i < pipeline->stage_count; i++) {
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
                i--;
                continue;
            }
            perror("PipeFlow: waitpid() error");
            stage->state = STAGE_STATE_FAILED;
            overall_status = -1;
            continue;
        }

        // Decode process exit status
        if (WIFEXITED(status)) {
            stage->exit_code = WEXITSTATUS(status);
            stage->state = STAGE_STATE_EXITED;

            // Dynamic Stage Restart (Stretch goal / Distinction bar)
            if (stage->exit_code != 0 && opts && opts->auto_restart &&
                stage->restarts < opts->max_retries) {
                stage->restarts++;
                fprintf(stderr, "[PipeFlow Supervisor] Stage '%s' (PID %d) exited with code %d. Restarting (attempt %d/%d)...\n",
                        stage->name, stage->pid, stage->exit_code, stage->restarts, opts->max_retries);

                // Re-spawn stage
                pid_t new_pid = spawn_stage(pipeline, stage->id, stage->in_fd, stage->out_fd, opts);
                if (new_pid > 0) {
                    i--; // Re-wait on this stage
                    continue;
                }
            }

            if (stage->exit_code != 0) {
                overall_status = stage->exit_code;
            }
        } else if (WIFSIGNALED(status)) {
            stage->term_sig = WTERMSIG(status);
            stage->state = STAGE_STATE_SIGNALED;

            if (opts && opts->auto_restart && stage->restarts < opts->max_retries && stage->term_sig != SIGTERM) {
                stage->restarts++;
                fprintf(stderr, "[PipeFlow Supervisor] Stage '%s' (PID %d) killed by signal %d. Restarting...\n",
                        stage->name, stage->pid, stage->term_sig);
                pid_t new_pid = spawn_stage(pipeline, stage->id, stage->in_fd, stage->out_fd, opts);
                if (new_pid > 0) {
                    i--;
                    continue;
                }
            }

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
