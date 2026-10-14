#ifndef PIPEFLOW_EXECUTOR_H
#define PIPEFLOW_EXECUTOR_H

#include "stage.h"
#include <stdbool.h>

// Execution options
typedef struct {
    int timeout_sec;            // Timeout in seconds (0 = unlimited)
    bool verbose;               // Print debug/wiring diagnostics & process table
    bool redirect_logs;         // Redirect per-stage stderr to logs/<stage>.log
    bool auto_restart;          // Restart failed stage without tearing down pipeline
    int max_retries;            // Maximum restart attempts per failed stage
} exec_options_t;

// Execute a pipeline from start to completion
// Returns 0 on success (all stages exited 0), or non-zero on error/failure
int executor_run_pipeline(pipeline_t *pipe, const exec_options_t *opts);

#endif // PIPEFLOW_EXECUTOR_H
