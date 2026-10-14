#ifndef PIPEFLOW_STAGE_H
#define PIPEFLOW_STAGE_H

#include <sys/types.h>
#include <stdbool.h>

#define PF_MAX_STAGES 64
#define PF_MAX_EDGES  128

// Lifecycle state of a pipeline stage (process table tracking)
typedef enum {
    STAGE_STATE_CREATED,
    STAGE_STATE_READY,
    STAGE_STATE_RUNNING,
    STAGE_STATE_EXITED,
    STAGE_STATE_SIGNALED,
    STAGE_STATE_FAILED
} stage_state_t;

// A directed edge in the dataflow DAG between two stages
typedef struct edge {
    int from_stage;             // Source stage index
    int to_stage;               // Destination stage index
    int pipe_fd[2];             // Pipe file descriptors [0: read, 1: write]
    bool active;
} edge_t;

// A single stage representing a node in the computation graph / pipeline
typedef struct stage {
    int id;                     // 0-indexed stage order
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
    int restarts;               // Number of times stage has been restarted
    int in_degree;              // For topological sorting in DAG
    int out_degree;             // Outgoing edge count
    char log_file[256];         // Stderr redirection path (logs/<name>.log)
} stage_t;

// A pipeline holding stages, edges, and process group tracking
typedef struct pipeline {
    char name[128];             // Pipeline identifier
    stage_t **stages;           // Dynamic array of stage pointers
    int stage_count;            // Number of stages
    int stage_capacity;         // Capacity of stages array
    edge_t edges[PF_MAX_EDGES]; // Directed edges between stages
    int edge_count;             // Number of edges
    bool is_dag;                // True if configured as a non-linear DAG
    int topo_order[PF_MAX_STAGES]; // Dependency-ordered stage indices
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
int pipeline_find_stage_by_name(const pipeline_t *pipe, const char *name);
bool pipeline_add_edge(pipeline_t *pipe, int from_id, int to_id);
bool pipeline_topological_sort(pipeline_t *pipe);
void pipeline_free(pipeline_t *pipe);

// Print the process table of stages with states, PIDs, exit codes, and restarts
void pipeline_print_table(const pipeline_t *pipe);

#endif // PIPEFLOW_STAGE_H
