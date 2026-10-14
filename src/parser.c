#define _POSIX_C_SOURCE 200809L
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Helper to trim leading and trailing whitespace
static char *trim_whitespace(char *str)
{
    if (!str) return NULL;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

// Tokenize a command string into argv vector, respecting single and double quotes
char **parser_tokenize_command(const char *cmd_line, int *out_argc)
{
    if (!cmd_line || !out_argc) return NULL;

    int capacity = 8;
    int count = 0;
    char **argv = (char **)calloc(capacity, sizeof(char *));
    if (!argv) return NULL;

    const char *p = cmd_line;
    char token[1024];

    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        int t_idx = 0;
        char quote = 0;

        while (*p) {
            if (!quote && (*p == '\'' || *p == '"')) {
                quote = *p++;
                continue;
            }
            if (quote && *p == quote) {
                quote = 0;
                p++;
                continue;
            }
            if (!quote && isspace((unsigned char)*p)) {
                break;
            }
            if (t_idx < (int)sizeof(token) - 1) {
                token[t_idx++] = *p;
            }
            p++;
        }
        token[t_idx] = '\0';

        if (count >= capacity - 1) {
            capacity *= 2;
            char **new_argv = (char **)realloc(argv, capacity * sizeof(char *));
            if (!new_argv) {
                parser_free_tokens(argv, count);
                return NULL;
            }
            argv = new_argv;
        }

        argv[count++] = strdup(token);
    }

    argv[count] = NULL;
    *out_argc = count;
    return argv;
}

void parser_free_tokens(char **tokens, int count)
{
    if (!tokens) return;
    for (int i = 0; i < count; i++) {
        free(tokens[i]);
    }
    free(tokens);
}

// Parse a linear pipe string: "cmd1 arg | cmd2 | cmd3"
pipeline_t *parser_parse_string(const char *name, const char *pipeline_str)
{
    if (!pipeline_str) return NULL;

    pipeline_t *pipe = pipeline_create(name ? name : "interactive_pipe");
    if (!pipe) return NULL;

    char *copy = strdup(pipeline_str);
    if (!copy) {
        pipeline_free(pipe);
        return NULL;
    }

    char *cursor = copy;
    int stage_idx = 0;

    while (cursor && *cursor) {
        // Find next pipe '|'
        char *pipe_char = strchr(cursor, '|');
        if (pipe_char) {
            *pipe_char = '\0';
        }

        char *stage_cmd = trim_whitespace(cursor);
        if (strlen(stage_cmd) > 0) {
            int argc = 0;
            char **argv = parser_tokenize_command(stage_cmd, &argc);
            if (argv && argc > 0) {
                char stage_name[64];
                // Extract program base name for stage name
                const char *slash = strrchr(argv[0], '/');
                const char *base = slash ? slash + 1 : argv[0];
                snprintf(stage_name, sizeof(stage_name), "%s_%d", base, stage_idx);

                stage_t *st = stage_create(stage_idx, stage_name, argv[0], argv, argc);
                pipeline_add_stage(pipe, st);
                if (stage_idx > 0) {
                    pipeline_add_edge(pipe, stage_idx - 1, stage_idx);
                }
                parser_free_tokens(argv, argc);
                stage_idx++;
            }
        }

        if (pipe_char) {
            cursor = pipe_char + 1;
        } else {
            break;
        }
    }

    free(copy);

    if (pipe->stage_count == 0) {
        pipeline_free(pipe);
        return NULL;
    }

    pipeline_topological_sort(pipe);
    return pipe;
}

// Structure to store raw edge declarations from file
typedef struct {
    char from[64];
    char to[64];
} raw_edge_t;

// Parse a .pflow config file
pipeline_t *parser_parse_file(const char *filepath)
{
    if (!filepath) return NULL;

    FILE *f = fopen(filepath, "r");
    if (!f) {
        perror("PipeFlow parser: cannot open file");
        return NULL;
    }

    char pipe_name[128] = "file_pipeline";
    raw_edge_t raw_edges[PF_MAX_EDGES];
    int raw_edge_count = 0;

    pipeline_t *pipe = NULL;
    char line[1024];

    // First pass: collect pipeline name and direct inline pipe check
    while (fgets(line, sizeof(line), f)) {
        char *trimmed = trim_whitespace(line);
        if (*trimmed == '#' || *trimmed == '\0' || *trimmed == '/') continue;

        if (strchr(trimmed, '|') && strncmp(trimmed, "pipeline", 8) != 0 && strncmp(trimmed, "stage", 5) != 0) {
            fclose(f);
            return parser_parse_string(pipe_name, trimmed);
        }

        if (strncmp(trimmed, "pipeline", 8) == 0) {
            char *pname = trim_whitespace(trimmed + 8);
            if (*pname) {
                strncpy(pipe_name, pname, sizeof(pipe_name) - 1);
            }
        }
    }

    pipe = pipeline_create(pipe_name);
    if (!pipe) {
        fclose(f);
        return NULL;
    }

    // Second pass: instantiate stages and collect edges
    rewind(f);
    while (fgets(line, sizeof(line), f)) {
        char *trimmed = trim_whitespace(line);
        if (*trimmed == '#' || *trimmed == '\0' || *trimmed == '/') continue;

        if (strncmp(trimmed, "stage", 5) == 0) {
            char *rest = trim_whitespace(trimmed + 5);
            char *eq = strchr(rest, '=');
            if (eq) {
                *eq = '\0';
                char *sname = trim_whitespace(rest);
                char *scmd = trim_whitespace(eq + 1);

                int argc = 0;
                char **argv = parser_tokenize_command(scmd, &argc);
                if (argv && argc > 0) {
                    stage_t *st = stage_create(pipe->stage_count, sname, argv[0], argv, argc);
                    pipeline_add_stage(pipe, st);
                    parser_free_tokens(argv, argc);
                }
            }
        } else if (strncmp(trimmed, "edge", 4) == 0 || strncmp(trimmed, "pipeline_chain", 14) == 0) {
            char *rest = strstr(trimmed, "edge") ? trimmed + 4 : trimmed + 14;
            char edge_line[512];
            strncpy(edge_line, trim_whitespace(rest), sizeof(edge_line) - 1);
            edge_line[sizeof(edge_line) - 1] = '\0';

            // Split by "->" to get sequence of stages
            char *saveptr = NULL;
            char *node = strtok_r(edge_line, "->", &saveptr);
            char prev_node[64] = "";

            while (node) {
                char *curr_node = trim_whitespace(node);
                if (strlen(prev_node) > 0 && strlen(curr_node) > 0 && raw_edge_count < PF_MAX_EDGES) {
                    strncpy(raw_edges[raw_edge_count].from, prev_node, 63);
                    strncpy(raw_edges[raw_edge_count].to, curr_node, 63);
                    raw_edge_count++;
                }
                strncpy(prev_node, curr_node, 63);
                node = strtok_r(NULL, "->", &saveptr);
            }
        }
    }
    fclose(f);

    if (pipe->stage_count == 0) {
        pipeline_free(pipe);
        return NULL;
    }

    // Connect edges
    if (raw_edge_count > 0) {
        for (int i = 0; i < raw_edge_count; i++) {
            int u = pipeline_find_stage_by_name(pipe, raw_edges[i].from);
            int v = pipeline_find_stage_by_name(pipe, raw_edges[i].to);
            if (u >= 0 && v >= 0) {
                pipeline_add_edge(pipe, u, v);
            } else {
                fprintf(stderr, "PipeFlow: Warning: edge '%s -> %s' references unknown stage\n",
                        raw_edges[i].from, raw_edges[i].to);
            }
        }
        pipe->is_dag = true;
    } else {
        // Default linear chain if no edges specified
        for (int i = 0; i < pipe->stage_count - 1; i++) {
            pipeline_add_edge(pipe, i, i + 1);
        }
    }

    // Run topological sort and verify DAG acyclicity
    if (!pipeline_topological_sort(pipe)) {
        fprintf(stderr, "PipeFlow Error: Cycle detected in pipeline DAG '%s'! Graph must be acyclic.\n", pipe->name);
        pipeline_free(pipe);
        return NULL;
    }

    return pipe;
}
