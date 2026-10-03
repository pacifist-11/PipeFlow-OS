#define _POSIX_C_SOURCE 200809L
#include "signals.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <time.h>

volatile sig_atomic_t g_interrupted = 0;
volatile sig_atomic_t g_timed_out = 0;
static pipeline_t *s_active_pipeline = NULL;

static void handle_sigint_sigterm(int sig)
{
    (void)sig;
    g_interrupted = 1;
    if (s_active_pipeline && s_active_pipeline->pgid > 0) {
        kill(-s_active_pipeline->pgid, SIGTERM);
    }
}

static void handle_sigalrm(int sig)
{
    (void)sig;
    g_timed_out = 1;
    if (s_active_pipeline && s_active_pipeline->pgid > 0) {
        kill(-s_active_pipeline->pgid, SIGTERM);
    }
}

void signals_setup_parent(pipeline_t *active_pipeline)
{
    s_active_pipeline = active_pipeline;
    g_interrupted = 0;
    g_timed_out = 0;

    struct sigaction sa;

    // 1. Ignore SIGPIPE in parent so write to broken pipe returns EPIPE
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, NULL);

    // 2. Handle SIGINT and SIGTERM for clean pipeline teardown
    sa.sa_handler = handle_sigint_sigterm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // Note: do not set SA_RESTART so waitpid unblocks with EINTR
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // 3. Handle SIGALRM for pipeline timeout
    sa.sa_handler = handle_sigalrm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);
}

void signals_setup_child(void)
{
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    // Restore default behaviors for child stages
    sa.sa_handler = SIG_DFL;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGALRM, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);
}

void pipeline_kill_group(pipeline_t *pipe, int sig)
{
    if (pipe && pipe->pgid > 0) {
        kill(-pipe->pgid, sig);
    }
}

void pipeline_teardown(pipeline_t *pipe)
{
    if (!pipe || pipe->pgid <= 0) return;

    // Send SIGTERM to the entire process group
    kill(-pipe->pgid, SIGTERM);

    // Brief grace period for stages to exit cleanly
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 50000000L }; // 50 ms
    nanosleep(&ts, NULL);

    // Reaping loop: verify all children exit cleanly, escalate to SIGKILL if necessary
    for (int i = 0; i < pipe->stage_count; i++) {
        stage_t *s = pipe->stages[i];
        if (s->pid > 0 && (s->state == STAGE_STATE_RUNNING || s->state == STAGE_STATE_CREATED)) {
            int status = 0;
            pid_t res = waitpid(s->pid, &status, WNOHANG);
            if (res == 0) {
                // Still running: escalate to SIGKILL
                kill(s->pid, SIGKILL);
                waitpid(s->pid, &status, 0);
                s->state = STAGE_STATE_SIGNALED;
                s->term_sig = SIGKILL;
            } else if (res > 0) {
                if (WIFEXITED(status)) {
                    s->state = STAGE_STATE_EXITED;
                    s->exit_code = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    s->state = STAGE_STATE_SIGNALED;
                    s->term_sig = WTERMSIG(status);
                }
            }
        }
    }
    pipe->is_running = false;
}

void signals_set_timeout(int timeout_seconds)
{
    if (timeout_seconds > 0) {
        alarm(timeout_seconds);
    }
}

void signals_clear_timeout(void)
{
    alarm(0);
}
