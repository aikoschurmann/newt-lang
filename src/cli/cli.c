#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cli.h"

typedef bool (*OptionHandler)(Options *opts, int *i, int argc, char **argv);

typedef struct {
    const char *short_f;
    const char *long_f;
    OptionHandler handler;
} CLIOption;

static bool h_tokens(Options *o, int *i, int argc, char **argv) { o->print_tokens = true; return true; }
static bool h_ast(Options *o, int *i, int argc, char **argv)    { o->print_ast = true; return true; }
static bool h_ir(Options *o, int *i, int argc, char **argv)     { o->print_ir = true; return true; }
static bool h_types(Options *o, int *i, int argc, char **argv)  { o->print_types = true; return true; }
static bool h_time(Options *o, int *i, int argc, char **argv)   { o->print_time = true; return true; }
static bool h_run(Options *o, int *i, int argc, char **argv)    { o->run_executable = true; return true; }
static bool h_quiet(Options *o, int *i, int argc, char **argv)  { o->quiet = true; return true; }
static bool h_verbose(Options *o, int *i, int argc, char **argv){ o->verbose = true; return true; }

static bool h_opt(Options *o, int *i, int argc, char **argv) {
    if (strlen(argv[*i]) == 3) {
        o->opt_level = argv[*i][2] - '0';
    } else if (*i + 1 < argc) {
        o->opt_level = atoi(argv[++(*i)]);
    }
    if (o->opt_level < 0 || o->opt_level > 3) {
        fprintf(stderr, "Error: Invalid optimization level: %d\n", o->opt_level);
        return false;
    }
    return true;
}

static bool h_out(Options *o, int *i, int argc, char **argv) {
    if (*i + 1 < argc) {
        o->output_name = argv[++(*i)];
        return true;
    }
    fprintf(stderr, "Error: -o requires an argument\n");
    return false;
}

static const CLIOption REGISTRY[] = {
    {"-t", "--tokens",  h_tokens},
    {"-a", "--ast",     h_ast},
    {NULL, "--ir",      h_ir},
    {"-y", "--types",   h_types},
    {"-T", "--time",    h_time},
    {"-r", "--run",     h_run},
    {"-q", "--quiet",   h_quiet},
    {"-v", "--verbose", h_verbose},
    {"-o", NULL,        h_out},
};

int parse_options(int argc, char **argv, Options *opts, const char **in_path) {
    if (argc < 2) { print_usage(argv[0]); return 0; }
    
    opts->print_tokens = opts->print_ast = opts->print_ir = opts->print_types = false;
    opts->print_time = opts->verbose = opts->run_executable = opts->quiet = false;
    opts->output_name = "output"; opts->opt_level = 0; opts->stdlib_path = "lib";

    int pos_args = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { print_usage(argv[0]); return 0; }
        if (strcmp(argv[i], "--version") == 0) { print_version(); return 0; }

        bool found = false;
        for (size_t j = 0; j < sizeof(REGISTRY)/sizeof(REGISTRY[0]); ++j) {
            if ((REGISTRY[j].short_f && strcmp(argv[i], REGISTRY[j].short_f) == 0) ||
                (REGISTRY[j].long_f && strcmp(argv[i], REGISTRY[j].long_f) == 0)) {
                if (!REGISTRY[j].handler(opts, &i, argc, argv)) return 0;
                found = true; break;
            }
        }
        if (found) continue;

        if (strncmp(argv[i], "-O", 2) == 0) {
            if (!h_opt(opts, &i, argc, argv)) return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]); print_usage(argv[0]); return 0;
        } else {
            if (pos_args++ == 0) *in_path = argv[i];
            else { fprintf(stderr, "Error: Unexpected argument: %s\n", argv[i]); return 0; }
        }
    }

    if (pos_args == 0) { fprintf(stderr, "Error: No input file specified\n"); return 0; }
    return 1;
}

void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s <file> [options]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o <name>       Set output executable name (default: 'output')\n");
    fprintf(stderr, "  -O<level>       Optimization level (0-3)\n");
    fprintf(stderr, "  -r, --run       Compile and run the program immediately\n");
    fprintf(stderr, "  -v, --verbose   Show compilation progress\n");
    fprintf(stderr, "  -q, --quiet     Suppress all non-error output\n");
    fprintf(stderr, "  -T, --time      Show performance metrics\n");
    fprintf(stderr, "  -a, --ast       Dump the parsed AST\n");
    fprintf(stderr, "  --ir            Dump the generated LLVM IR\n");
    fprintf(stderr, "  -t, --tokens    Dump lexer tokens\n");
    fprintf(stderr, "  -y, --types     Dump type system state\n");
    fprintf(stderr, "  --version       Show version information\n");
    fprintf(stderr, "  -h, --help      Show this help message\n");
}

void print_version(void) {
#ifdef DEV_BUILD
    printf("Compiler v3.0 (Development Build)\n");
#else
    printf("Compiler v3.0 (Release)\n");
#endif
}
