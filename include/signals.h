#ifndef PIPEFLOW_SIGNALS_H
#define PIPEFLOW_SIGNALS_H

#include "stage.h"
#include <signal.h>

// Global pointer to currently running pipeline for asynchronous signal handlers
extern volatile sig_atomic_t g_interrupted;
extern volatile sig_atomic_t g_timed_out;

// Setup signal handling in the parent engine process (SIGINT, SIGTERM, SIGALRM, SIGPIPE)
void signals_setup_parent(pipeline_t *active_pipeline);

// Reset signals in child stage processes before execvp
void signals_setup_child(void);

// Cleanly send a signal to the whole pipeline process group
void pipeline_kill_group(pipeline_t *pipe, int sig);

// Graceful pipeline teardown: sends SIGTERM to the process group, reaps processes, or escalates to SIGKILL if needed
void pipeline_teardown(pipeline_t *pipe);

// Setup a timer interrupt/alarm for pipeline timeout
void signals_set_timeout(int timeout_seconds);
void signals_clear_timeout(void);

#endif // PIPEFLOW_SIGNALS_H
