#define _POSIX_C_SOURCE 200809L
#include "parser.h"
#include "executor.h"
#include "repl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

static void print_usage(const char *progname)
{
    printf("PipeFlow — A Dataflow Stream Engine\n");
    printf("Usage:\n");
    printf("  %s                              Start interactive REPL\n", progname);
    printf("  %s run \"<pipeline>\"            Execute inline pipeline\n", progname);
    printf("  %s -f <config.pflow>            Execute pipeline configuration file\n", progname);
    printf("  %s -t <seconds> run \"...\"       Execute with timeout alarm\n", progname);
    printf("  %s -v run \"...\"                 Verbose mode: print process table\n", progname);
    printf("  %s -h, --help                   Display this help message\n", progname);
    printf("\nExamples:\n");
    printf("  %s run \"./bin/pf_source 10 | ./bin/pf_transform upper | ./bin/pf_sink\"\n", progname);
    printf("  %s run \"./bin/pf_source 100 | ./bin/pf_head 5 | ./bin/pf_sink\"\n", progname);
    printf("  %s -f examples/basic.pflow\n", progname);
}

int main(int argc, char *argv[])
{
    exec_options_t opts = {
        .timeout_sec = 0,
        .verbose = true
    };

    int opt;
    char *config_file = NULL;

    static struct option long_options[] = {
        {"file",    required_argument, 0, 'f'},
        {"timeout", required_argument, 0, 't'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "f:t:vh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'f':
                config_file = optarg;
                break;
            case 't':
                opts.timeout_sec = atoi(optarg);
                break;
            case 'v':
                opts.verbose = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Direct file execution mode
    if (config_file) {
        pipeline_t *pipe = parser_parse_file(config_file);
        if (!pipe) {
            fprintf(stderr, "Error: Failed to load pipeline configuration from %s\n", config_file);
            return 1;
        }
        int status = executor_run_pipeline(pipe, &opts);
        pipeline_free(pipe);
        return status;
    }

    // Direct inline pipeline command mode
    if (optind < argc) {
        const char *cmd_verb = argv[optind];
        const char *pipeline_str = NULL;

        if (strcmp(cmd_verb, "run") == 0 && optind + 1 < argc) {
            pipeline_str = argv[optind + 1];
        } else if (strchr(cmd_verb, '|')) {
            pipeline_str = cmd_verb;
        }

        if (pipeline_str) {
            pipeline_t *pipe = parser_parse_string("cli_pipeline", pipeline_str);
            if (!pipe) {
                fprintf(stderr, "Error: Failed to parse pipeline string: %s\n", pipeline_str);
                return 1;
            }
            int status = executor_run_pipeline(pipe, &opts);
            pipeline_free(pipe);
            return status;
        } else {
            fprintf(stderr, "Unknown command '%s'. Use --help for usage.\n", cmd_verb);
            return 1;
        }
    }

    // No arguments provided: launch interactive REPL
    repl_run();
    return 0;
}
