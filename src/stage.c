#define _POSIX_C_SOURCE 200809L
#include "stage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *stage_state_to_string(stage_state_t state)
{
    switch (state) {
        case STAGE_STATE_CREATED:   return "CREATED";
        case STAGE_STATE_READY:     return "READY";
        case STAGE_STATE_RUNNING:   return "RUNNING";
        case STAGE_STATE_EXITED:    return "EXITED";
        case STAGE_STATE_SIGNALED:  return "SIGNALED";
        case STAGE_STATE_FAILED:    return "FAILED";
        default:                    return "UNKNOWN";
    }
}

stage_t *stage_create(int id, const char *name, const char *cmd, char **argv, int argc)
{
    stage_t *s = (stage_t *)calloc(1, sizeof(stage_t));
    if (!s) return NULL;

    s->id = id;
    if (name) {
        strncpy(s->name, name, sizeof(s->name) - 1);
    } else {
        snprintf(s->name, sizeof(s->name), "stage_%d", id);
    }

    if (cmd) {
        strncpy(s->command, cmd, sizeof(s->command) - 1);
    }

    s->argc = argc;
    if (argv && argc > 0) {
        s->argv = (char **)calloc(argc + 1, sizeof(char *));
        for (int i = 0; i < argc; i++) {
            s->argv[i] = strdup(argv[i]);
        }
        s->argv[argc] = NULL;
    } else if (cmd) {
        s->argv = (char **)calloc(2, sizeof(char *));
        s->argv[0] = strdup(cmd);
        s->argv[1] = NULL;
        s->argc = 1;
    }

    s->pid = -1;
    s->state = STAGE_STATE_CREATED;
    s->exit_code = -1;
    s->term_sig = 0;
    s->in_fd = STDIN_FILENO;
    s->out_fd = STDOUT_FILENO;
    s->restarts = 0;
    s->in_degree = 0;
    s->out_degree = 0;
    s->log_file[0] = '\0';

    return s;
}

void stage_free(stage_t *stage)
{
    if (!stage) return;
    if (stage->argv) {
        for (int i = 0; i < stage->argc; i++) {
            free(stage->argv[i]);
        }
        free(stage->argv);
    }
    free(stage);
}

pipeline_t *pipeline_create(const char *name)
{
    pipeline_t *p = (pipeline_t *)calloc(1, sizeof(pipeline_t));
    if (!p) return NULL;

    if (name) {
        strncpy(p->name, name, sizeof(p->name) - 1);
    } else {
        strncpy(p->name, "pipeline", sizeof(p->name) - 1);
    }

    p->stage_capacity = 8;
    p->stages = (stage_t **)calloc(p->stage_capacity, sizeof(stage_t *));
    p->stage_count = 0;
    p->edge_count = 0;
    p->is_dag = false;
    p->pgid = 0;
    p->is_running = false;

    return p;
}

bool pipeline_add_stage(pipeline_t *pipe, stage_t *stage)
{
    if (!pipe || !stage) return false;

    if (pipe->stage_count >= pipe->stage_capacity) {
        int new_cap = pipe->stage_capacity * 2;
        stage_t **new_stages = (stage_t **)realloc(pipe->stages, new_cap * sizeof(stage_t *));
        if (!new_stages) return false;
        pipe->stages = new_stages;
        pipe->stage_capacity = new_cap;
    }

    stage->id = pipe->stage_count;
    pipe->stages[pipe->stage_count++] = stage;
    return true;
}

int pipeline_find_stage_by_name(const pipeline_t *pipe, const char *name)
{
    if (!pipe || !name) return -1;
    for (int i = 0; i < pipe->stage_count; i++) {
        if (strcmp(pipe->stages[i]->name, name) == 0) {
            return i;
        }
    }
    return -1;
}

bool pipeline_add_edge(pipeline_t *pipe, int from_id, int to_id)
{
    if (!pipe || from_id < 0 || to_id < 0 ||
        from_id >= pipe->stage_count || to_id >= pipe->stage_count) {
        return false;
    }
    if (pipe->edge_count >= PF_MAX_EDGES) return false;

    // Check duplicate
    for (int i = 0; i < pipe->edge_count; i++) {
        if (pipe->edges[i].from_stage == from_id && pipe->edges[i].to_stage == to_id) {
            return true;
        }
    }

    edge_t *e = &pipe->edges[pipe->edge_count++];
    e->from_stage = from_id;
    e->to_stage = to_id;
    e->pipe_fd[0] = -1;
    e->pipe_fd[1] = -1;
    e->active = true;

    pipe->stages[from_id]->out_degree++;
    pipe->stages[to_id]->in_degree++;
    return true;
}

// Kahn's algorithm for topological sorting of stages in DAG
bool pipeline_topological_sort(pipeline_t *pipe)
{
    if (!pipe || pipe->stage_count == 0) return false;

    int in_deg[PF_MAX_STAGES];
    for (int i = 0; i < pipe->stage_count; i++) {
        in_deg[i] = 0;
    }

    for (int i = 0; i < pipe->edge_count; i++) {
        int v = pipe->edges[i].to_stage;
        if (v >= 0 && v < pipe->stage_count) {
            in_deg[v]++;
        }
    }

    int queue[PF_MAX_STAGES];
    int front = 0, rear = 0;

    for (int i = 0; i < pipe->stage_count; i++) {
        if (in_deg[i] == 0) {
            queue[rear++] = i;
        }
    }

    int visited_count = 0;
    while (front < rear) {
        int u = queue[front++];
        pipe->topo_order[visited_count++] = u;

        for (int i = 0; i < pipe->edge_count; i++) {
            if (pipe->edges[i].from_stage == u) {
                int v = pipe->edges[i].to_stage;
                in_deg[v]--;
                if (in_deg[v] == 0) {
                    queue[rear++] = v;
                }
            }
        }
    }

    // If visited_count != stage_count, graph has a cycle!
    return (visited_count == pipe->stage_count);
}

void pipeline_free(pipeline_t *pipe)
{
    if (!pipe) return;
    for (int i = 0; i < pipe->stage_count; i++) {
        stage_free(pipe->stages[i]);
    }
    free(pipe->stages);
    free(pipe);
}

void pipeline_print_table(const pipeline_t *pipe)
{
    if (!pipe) return;

    printf("\n====================================================================================================\n");
    printf(" PIPELINE PROCESS TABLE: %s (Stages: %d, Edges: %d, PGID: %d)\n",
           pipe->name, pipe->stage_count, pipe->edge_count, pipe->pgid);
    printf("----------------------------------------------------------------------------------------------------\n");
    printf(" %-4s | %-14s | %-7s | %-10s | %-10s | %-8s | %-32s\n",
           "ID", "STAGE NAME", "PID", "STATE", "STATUS", "RESTARTS", "COMMAND");
    printf("----------------------------------------------------------------------------------------------------\n");

    for (int i = 0; i < pipe->stage_count; i++) {
        stage_t *s = pipe->stages[i];
        char status_buf[32] = "-";

        if (s->state == STAGE_STATE_EXITED) {
            snprintf(status_buf, sizeof(status_buf), "exit(%d)", s->exit_code);
        } else if (s->state == STAGE_STATE_SIGNALED) {
            snprintf(status_buf, sizeof(status_buf), "sig(%d)", s->term_sig);
        } else if (s->state == STAGE_STATE_FAILED) {
            snprintf(status_buf, sizeof(status_buf), "failed");
        } else if (s->state == STAGE_STATE_RUNNING) {
            snprintf(status_buf, sizeof(status_buf), "running");
        }

        char cmd_buf[128] = "";
        int offset = 0;
        for (int a = 0; a < s->argc && offset < 120; a++) {
            offset += snprintf(cmd_buf + offset, sizeof(cmd_buf) - offset, "%s%s",
                               s->argv[a], (a + 1 < s->argc) ? " " : "");
        }

        printf(" %-4d | %-14s | %-7d | %-10s | %-10s | %-8d | %-32s\n",
               s->id,
               s->name,
               s->pid > 0 ? s->pid : 0,
               stage_state_to_string(s->state),
               status_buf,
               s->restarts,
               cmd_buf);
    }
    printf("====================================================================================================\n\n");
}
