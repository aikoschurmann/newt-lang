#pragma once
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    bool print_tokens;
    bool print_ast;
    bool print_ir;
    bool print_time;
    bool print_types;
    bool verbose;
    bool run_executable;
    bool quiet;
    int opt_level;
    const char *output_name;
} Options;

int parse_options(int argc, char **argv, Options *opts, const char **in_path);
void print_usage(const char *prog);
void print_version(void);

