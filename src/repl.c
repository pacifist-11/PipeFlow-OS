#define _POSIX_C_SOURCE 200809L
#include "repl.h"
#include "parser.h"
#include "executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>

static pipeline_t *s_last_pipeline = NULL;

static void print_banner(void)
{
    printf("\n=================================================================\n");
    printf("        PipeFlow Dataflow Stream Engine — Interactive REPL       \n");
    printf("=================================================================\n");
    printf(" Commands:\n");
    printf("   run <pipeline>   - Execute a pipeline (e.g. run ./bin/pf_source 5 | ./bin/pf_sink)\n");
    printf("   load <file>      - Load and run a .pflow pipeline config file\n");
    printf("   status           - Show process table & exit statuses of last run\n");
    printf("   stages           - List available sample stage binaries\n");
    printf("   help             - Show this help message\n");
    printf("   exit / quit      - Exit PipeFlow REPL\n");
    printf("-----------------------------------------------------------------\n\n");
}

static void list_stages(void)
{
    printf("\nAvailable Stage Binaries in ./bin/:\n");
    printf("  pf_source [N] [delay_ms] [prefix] - Generates N records, handles EPIPE\n");
    printf("  pf_transform [upper|lower|reverse] - Modifies stream records\n");
    printf("  pf_filter [keyword]               - Filters stream by substring\n");
    printf("  pf_head [limit]                   - Reads N records then exits early (SIGPIPE test)\n");
    printf("  pf_sink [--quiet]                 - Collects metrics and outputs records\n\n");
}

void repl_run(void)
{
    print_banner();

    char *line = NULL;
    while ((line = readline("pipeflow> ")) != NULL) {
        // Skip leading whitespace
        char *cmd = line;
        while (*cmd == ' ' || *cmd == '\t') cmd++;

        if (*cmd == '\0') {
            free(line);
            continue;
        }

        add_history(line);

        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            free(line);
            break;
        } else if (strcmp(cmd, "help") == 0) {
            print_banner();
        } else if (strcmp(cmd, "stages") == 0) {
            list_stages();
        } else if (strcmp(cmd, "status") == 0) {
            if (s_last_pipeline) {
                pipeline_print_table(s_last_pipeline);
            } else {
                printf("No pipeline has been executed yet.\n");
            }
        } else if (strncmp(cmd, "load ", 5) == 0) {
            char *filepath = cmd + 5;
            while (*filepath == ' ' || *filepath == '\t') filepath++;

            if (s_last_pipeline) {
                pipeline_free(s_last_pipeline);
                s_last_pipeline = NULL;
            }

            printf("[REPL] Loading configuration file: %s\n", filepath);
            s_last_pipeline = parser_parse_file(filepath);
            if (!s_last_pipeline) {
                fprintf(stderr, "Failed to parse pipeline file: %s\n", filepath);
            } else {
                exec_options_t opts = { .timeout_sec = 0, .verbose = true };
                executor_run_pipeline(s_last_pipeline, &opts);
            }
        } else if (strncmp(cmd, "run ", 4) == 0 || strchr(cmd, '|')) {
            char *pipestr = (strncmp(cmd, "run ", 4) == 0) ? (cmd + 4) : cmd;
            while (*pipestr == ' ' || *pipestr == '\t') pipestr++;

            if (s_last_pipeline) {
                pipeline_free(s_last_pipeline);
                s_last_pipeline = NULL;
            }

            s_last_pipeline = parser_parse_string("repl_pipeline", pipestr);
            if (!s_last_pipeline) {
                fprintf(stderr, "Syntax error parsing pipeline.\n");
            } else {
                exec_options_t opts = { .timeout_sec = 0, .verbose = true };
                executor_run_pipeline(s_last_pipeline, &opts);
            }
        } else {
            printf("Unknown command: '%s'. Type 'help' for available commands.\n", cmd);
        }

        free(line);
    }

    if (s_last_pipeline) {
        pipeline_free(s_last_pipeline);
        s_last_pipeline = NULL;
    }

    printf("\nExiting PipeFlow REPL. Goodbye!\n");
}
