#include <stdio.h>
#include <string.h>
#include "cli.h"

int parse_options(int argc, char **argv, Options *opts, const char **in_path) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 0;
    }
    
    // Default values
    opts->print_tokens = false;
    opts->print_ast = false;
    opts->print_types = false;
    opts->run_executable = false;
    opts->output_name = "output";
    opts->opt_level = 0;

#ifdef DEV_BUILD
    opts->print_time = true;
    opts->quiet = false;
#else
    opts->print_time = false;
    opts->quiet = true;
#endif

    int pos_args = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--tokens") == 0 || strcmp(argv[i], "-t") == 0) {
            opts->print_tokens = true;
        } else if (strcmp(argv[i], "--ast") == 0 || strcmp(argv[i], "-a") == 0) {
            opts->print_ast = true;
        } else if (strcmp(argv[i], "--types") == 0 || strcmp(argv[i], "-y") == 0) {
            opts->print_types = true;
        } else if (strcmp(argv[i], "--time") == 0 || strcmp(argv[i], "-T") == 0) {
            opts->print_time = true;
            opts->quiet = false; // Showing time implies we want some output
        } else if (strcmp(argv[i], "--no-time") == 0) {
            opts->print_time = false;
        } else if (strcmp(argv[i], "--run") == 0 || strcmp(argv[i], "-r") == 0) {
            opts->run_executable = true;
        } else if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            opts->quiet = true;
            opts->print_time = false;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            opts->quiet = false;
        } else if (strncmp(argv[i], "-O", 2) == 0) {
            if (strlen(argv[i]) == 3) {
                opts->opt_level = argv[i][2] - '0';
                if (opts->opt_level < 0 || opts->opt_level > 3) {
                    fprintf(stderr, "Error: Invalid optimization level: %s\n", argv[i]);
                    return 0;
                }
            } else {
                fprintf(stderr, "Error: -O requires a level (0-3)\n");
                return 0;
            }
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                opts->output_name = argv[++i];
            } else {
                fprintf(stderr, "Error: -o requires an argument\n");
                return 0;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 0;
        } else {
            if (pos_args == 0) {
                *in_path = argv[i];
                pos_args++;
            } else {
                fprintf(stderr, "Error: Unexpected argument: %s\n", argv[i]);
                return 0;
            }
        }
    }

    if (pos_args == 0) {
        fprintf(stderr, "Error: No input file specified\n");
        return 0;
    }

    return 1;
}

void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s <file> [options]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o <name>       Set output executable name (default: 'output')\n");
    fprintf(stderr, "  -q, --quiet     Suppress all non-error output (default in Release)\n");
    fprintf(stderr, "  -v, --verbose   Show compilation progress (default in Dev)\n");
    fprintf(stderr, "  -T, --time      Show compilation performance metrics\n");
    fprintf(stderr, "  --no-time       Disable metrics\n");
    fprintf(stderr, "  -r, --run       Compile and run the program immediately\n");
    fprintf(stderr, "  -a, --ast       Dump the parsed AST\n");
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
