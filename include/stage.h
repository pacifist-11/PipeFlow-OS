#ifndef PIPEFLOW_STAGE_H
#define PIPEFLOW_STAGE_H

#include <sys/types.h>
#include <stdbool.h>

// Lifecycle state of a pipeline stage (process table tracking)
typedef enum {
    STAGE_STATE_CREATED,
    STAGE_STATE_READY,
    STAGE_STATE_RUNNING,
    STAGE_STATE_EXITED,
    STAGE_STATE_SIGNALED,
    STAGE_STATE_FAILED
} stage_state_t;

// A single stage representing a node in the computation graph / pipeline
typedef struct stage {
    int id;                     // 0-indexed stage order in the linear chain
    char name[64];              // Human-readable stage name
    char command[256];          // Program name or executable path (e.g. pf_source or grep)
    char **argv;                // NULL-terminated argv vector
    int argc;                   // Argument count
    pid_t pid;                  // OS Process ID once forked
    stage_state_t state;        // Current lifecycle state
    int exit_code;              // Exit status code (if WIFEXITED)
    int term_sig;               // Terminating signal (if WIFSIGNALED)
    int in_fd;                  // Stdin redirection source fd
    int out_fd;                 // Stdout redirection destination fd
} stage_t;

// A pipeline holding stages and process group tracking
typedef struct pipeline {
    char name[128];             // Pipeline identifier
    stage_t **stages;           // Dynamic array of stage pointers
    int stage_count;            // Number of stages
    int stage_capacity;         // Capacity of stages array
    pid_t pgid;                 // Process Group ID (for group signals)
    bool is_running;            // True while execution is active
} pipeline_t;

// Stage lifecycle functions
stage_t *stage_create(int id, const char *name, const char *cmd, char **argv, int argc);
void stage_free(stage_t *stage);
const char *stage_state_to_string(stage_state_t state);

// Pipeline lifecycle functions
pipeline_t *pipeline_create(const char *name);
bool pipeline_add_stage(pipeline_t *pipe, stage_t *stage);
void pipeline_free(pipeline_t *pipe);

// Print the process table of stages with states, PIDs, exit codes
void pipeline_print_table(const pipeline_t *pipe);

#endif // PIPEFLOW_STAGE_H
