#ifndef PIPEFLOW_PARSER_H
#define PIPEFLOW_PARSER_H

#include "stage.h"

// Parse a pipeline from a shell-style pipe string, e.g.:
// "pf_source 10 | pf_transform upper | pf_sink"
pipeline_t *parser_parse_string(const char *name, const char *pipeline_str);

// Parse a pipeline from a .pflow configuration file
pipeline_t *parser_parse_file(const char *filepath);

// Tokenize a command line into argv array (handles double/single quotes)
char **parser_tokenize_command(const char *cmd_line, int *out_argc);
void parser_free_tokens(char **tokens, int count);

#endif // PIPEFLOW_PARSER_H
