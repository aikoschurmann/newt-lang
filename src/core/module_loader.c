#include "module_loader.h"
#include "file.h"
#include "lexing/lexer.h"
#include "parsing/parser.h"
#include "parsing/parse_statements.h"
#include "parsing/parse_declarations.h"
#include "core/utils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/stat.h>
#include <stdarg.h>

#define EXIT_OK    0
#define EXIT_USAGE 1
#define EXIT_IO    2
#define EXIT_LEX   3
#define EXIT_PARSE 4
#define EXIT_TYPE  5

#define MAX_RECURSION_DEPTH 256

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    Arena *arena;
} StrBuf;

static void strbuf_init(StrBuf *sb, Arena *arena) {
    sb->arena = arena;
    sb->cap = 128;
    sb->len = 0;
    sb->buf = arena_alloc(arena, sb->cap);
    sb->buf[0] = '\0';
}

static void strbuf_append(StrBuf *sb, const char *s) {
    if (!s) return;
    size_t slen = strlen(s);
    if (sb->len + slen + 1 > sb->cap) {
        size_t new_cap = sb->cap * 2;
        while (sb->len + slen + 1 > new_cap) new_cap *= 2;
        char *new_buf = arena_alloc(sb->arena, new_cap);
        memcpy(new_buf, sb->buf, sb->len + 1);
        sb->buf = new_buf;
        sb->cap = new_cap;
    }
    memcpy(sb->buf + sb->len, s, slen);
    sb->len += slen;
    sb->buf[sb->len] = '\0';
}

static void strbuf_append_fmt(StrBuf *sb, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char tmp[1024]; // Temporary buffer for formatted segment
    int n = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    
    if (n < 0) return;
    if ((size_t)n < sizeof(tmp)) {
        strbuf_append(sb, tmp);
    } else {
        // If 1KB isn't enough, allocate exact size
        char *big_tmp = xmalloc(n + 1);
        va_start(args, fmt);
        vsnprintf(big_tmp, n + 1, fmt, args);
        va_end(args);
        strbuf_append(sb, big_tmp);
        free(big_tmp);
    }
}

ModuleLoader* module_loader_create(Arena *arena, Options *opts, 
                                   DenseArenaInterner *keywords, 
                                   DenseArenaInterner *identifiers, 
                                   DenseArenaInterner *strings) {
    ModuleLoader *loader = arena_alloc(arena, sizeof(ModuleLoader));
    loader->arena = arena;
    loader->opts = opts;
    loader->keywords = keywords;
    loader->identifiers = identifiers;
    loader->strings = strings;
    
    loader->units = hashmap_create(16);
    loader->units_by_logical_path = hashmap_create(16);
    loader->units_ordered = arena_alloc(arena, sizeof(DynArray));
    dynarray_init_in_arena(loader->units_ordered, arena, sizeof(CompilationUnit*), 8);
    
    loader->project_root = NULL;

    return loader;
}

static char* get_absolute_path_real(Arena *arena, const char *path) {
    char *abs = realpath(path, NULL);
    if (!abs) return NULL;
    char *interned = arena_alloc(arena, strlen(abs) + 1);
    strcpy(interned, abs);
    free(abs);
    return interned;
}

CompilationUnit* module_loader_get_unit(ModuleLoader *loader, const char *path) {
    return (CompilationUnit*)hashmap_get(loader->units, (void*)path, str_hash, str_cmp);
}

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int load_module_recursive(ModuleLoader *loader, const char *path, const char *logical_path, const char *importer_path, int depth) {
    if (depth > MAX_RECURSION_DEPTH) {
        fprintf(stderr, "Error: Maximum recursion depth (%d) exceeded while loading modules.\n", MAX_RECURSION_DEPTH);
        return EXIT_IO;
    }

    char *abs_path = get_absolute_path_real(loader->arena, path);
    if (!abs_path) {
        // Fallback for module.tn if path might be a directory
        StrBuf fallback_sb;
        strbuf_init(&fallback_sb, loader->arena);
        strbuf_append_fmt(&fallback_sb, "%s/module.tn", path);
        abs_path = get_absolute_path_real(loader->arena, fallback_sb.buf);
        
        if (!abs_path) {
            if (importer_path) {
                fprintf(stderr, "Error in %s: Could not resolve module path '%s'\n", importer_path, path);
            } else {
                fprintf(stderr, "Error: Could not resolve module path '%s'\n", path);
            }
            return EXIT_IO;
        }
    }
    
    // Set project_root on first call
    if (!loader->project_root) {
        char *dir = xstrdup(abs_path);
        char *last_slash = strrchr(dir, '/');
        if (last_slash) *last_slash = '\0';
        loader->project_root = arena_alloc(loader->arena, strlen(dir) + 1);
        strcpy(loader->project_root, dir);
        free(dir);
    }

    // 1. Check if module is already loaded
    CompilationUnit *unit = module_loader_get_unit(loader, abs_path);
    if (unit) {
        // Map to logical path if provided
        if (logical_path) {
            hashmap_put(loader->units_by_logical_path, (void*)logical_path, unit, str_hash, str_cmp);
            if (!unit->logical_path) unit->logical_path = (char*)logical_path;
        }
        return EXIT_OK;
    }

    if (loader->opts->verbose) printf("Loading module: %s\n", abs_path);

    // 2. Read Source
    char *src = read_file(abs_path);
    if (!src) {
        fprintf(stderr, "Error: Failed to read file: %s\n", abs_path);
        return EXIT_IO;
    }
    size_t src_len = strlen(src);

    // 3. Lexing (Shared Interners)
    Lexer *lexer = lexer_create_ex(src, src_len, loader->arena, loader->keywords, loader->identifiers, loader->strings);
    if (!lexer_lex_all(lexer)) {
        fprintf(stderr, "Error: Lexing failed for %s\n", abs_path);
        free(src);
        return EXIT_LEX;
    }

    // 4. Parsing
    Parser *parser = parser_create(lexer->tokens, abs_path, loader->arena);
    ParseError parse_err = {0};
    AstNode *module_ast = parse_program(parser, &parse_err);
    if (parse_err.message) {
        print_parse_error(&parse_err);
        free(src);
        return EXIT_PARSE;
    }

    if (!module_ast) {
        free(src);
        return EXIT_OK;
    }

    // Create CompilationUnit
    unit = arena_alloc(loader->arena, sizeof(CompilationUnit));
    unit->absolute_path = abs_path;
    unit->logical_path = (char*)logical_path; 
    unit->ast_root = module_ast;
    unit->global_scope = NULL; 
    unit->signatures_resolved = false;
    unit->imports_resolved = false;
    
    hashmap_put(loader->units, abs_path, unit, str_hash, str_cmp);
    if (logical_path) {
        hashmap_put(loader->units_by_logical_path, (void*)logical_path, unit, str_hash, str_cmp);
    }

    // 5. Recursive Loading
    AstProgram *module_prog = &module_ast->data.program;
    
    // Get directory of current module for relative resolution
    StrBuf current_dir_sb;
    strbuf_init(&current_dir_sb, loader->arena);
    const char *last_slash = strrchr(abs_path, '/');
    if (last_slash) {
        size_t len = (size_t)(last_slash - abs_path);
        char *tmp = xmalloc(len + 1);
        memcpy(tmp, abs_path, len);
        tmp[len] = '\0';
        strbuf_append(&current_dir_sb, tmp);
        free(tmp);
    } else {
        strbuf_append(&current_dir_sb, ".");
    }

    for (size_t i = 0; i < module_prog->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(module_prog->decls, i);
        
        if (decl->node_type == AST_IMPORT_DECLARATION) {
            AstImportDeclaration *imp = &decl->data.import_declaration;
            
            StrBuf cp_sb, cl_sb;
            strbuf_init(&cp_sb, loader->arena);
            strbuf_init(&cl_sb, loader->arena);
            
            for (size_t j = 0; j < imp->module_path->count; j++) {
                InternResult *part = *(InternResult**)dynarray_get(imp->module_path, j);
                Slice *s = (Slice*)part->key;
                if (j > 0) strbuf_append(&cp_sb, "/");
                strbuf_append_fmt(&cp_sb, "%.*s", (int)s->len, s->ptr);
                
                if (j > 0) strbuf_append(&cl_sb, ".");
                strbuf_append_fmt(&cl_sb, "%.*s", (int)s->len, s->ptr);
            }

            StrBuf mod_path_full_sb;
            strbuf_init(&mod_path_full_sb, loader->arena);
            char *target_logical = NULL;
            
            if (imp->leading_dots > 0) {
                // Relative Import
                StrBuf base_dir_sb;
                strbuf_init(&base_dir_sb, loader->arena);
                strbuf_append(&base_dir_sb, current_dir_sb.buf);
                
                // For .. or more, go up
                for (int d = 1; d < imp->leading_dots; d++) {
                    char *up = strrchr(base_dir_sb.buf, '/');
                    if (up) {
                        *up = '\0';
                        base_dir_sb.len = strlen(base_dir_sb.buf);
                    }
                }
                
                strbuf_append_fmt(&mod_path_full_sb, "%s/%s", base_dir_sb.buf, cp_sb.buf);

                // Target logical name: importer's logical path + components_logical
                if (unit->logical_path) {
                    size_t t_len = strlen(unit->logical_path) + cl_sb.len + 2;
                    target_logical = arena_alloc(loader->arena, t_len);
                    snprintf(target_logical, t_len, "%s.%s", unit->logical_path, cl_sb.buf);
                } else {
                    target_logical = arena_alloc(loader->arena, cl_sb.len + 1);
                    strcpy(target_logical, cl_sb.buf);
                }
            } else if (imp->is_root_relative) {
                // Root-relative Import
                strbuf_append_fmt(&mod_path_full_sb, "%s/%s", loader->project_root, cp_sb.buf);
                target_logical = arena_alloc(loader->arena, cl_sb.len + 1);
                strcpy(target_logical, cl_sb.buf);
            } else {
                // Absolute (Library) Import
                strbuf_append_fmt(&mod_path_full_sb, "%s/%s", loader->opts->stdlib_path, cp_sb.buf);
                target_logical = arena_alloc(loader->arena, cl_sb.len + 1);
                strcpy(target_logical, cl_sb.buf);
            }

            // Try .tn then /module.tn
            StrBuf target_file_sb;
            strbuf_init(&target_file_sb, loader->arena);
            strbuf_append_fmt(&target_file_sb, "%s.tn", mod_path_full_sb.buf);
            
            if (!file_exists(target_file_sb.buf)) {
                 target_file_sb.len = 0;
                 target_file_sb.buf[0] = '\0';
                 strbuf_append_fmt(&target_file_sb, "%s/module.tn", mod_path_full_sb.buf);
            }
            
            int res = load_module_recursive(loader, target_file_sb.buf, target_logical, abs_path, depth + 1);
            if (res == EXIT_OK) {
                decl->data.import_declaration.resolved_logical_path = target_logical;
            } else {
                // M-2: Cleanup before returning failure
                hashmap_remove(loader->units, abs_path, str_hash, str_cmp, NULL, NULL);
                if (logical_path) {
                    hashmap_remove(loader->units_by_logical_path, (void*)logical_path, str_hash, str_cmp, NULL, NULL);
                }
                free(src);
                return res;
            }
        }
    }

    dynarray_push_value(loader->units_ordered, &unit);

    free(src);
    return EXIT_OK;
}
