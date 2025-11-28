// semantic.c
#include "semantic.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

// -----------------------------------------------------------------------------
// Global pointer to source code to print pretty errors
// -----------------------------------------------------------------------------
static const char *g_source_code = NULL;
void semantic_set_source(const char *src) { g_source_code = src; }

void semantic_print_source_error(int line, int column) {
    if (!g_source_code) return;
    const char *buf = g_source_code;
    int cur_line = 1;
    int pos = 0;
    while (buf[pos] && cur_line < line) {
        if (buf[pos] == '\n') cur_line++;
        pos++;
    }
    if (!buf[pos]) return;
    int start = pos;
    while (buf[pos] && buf[pos] != '\n') pos++;
    int end = pos;
    fprintf(stderr, "%4d | ", line);
    fwrite(buf + start, 1, end - start, stderr);
    fprintf(stderr, "\n");
    fprintf(stderr, "     | ");
    for (int i = 1; i < column; i++) fputc(' ', stderr);
    fprintf(stderr, "^\n");
}

// -----------------------------------------------------------------------------
// Error list structure
// -----------------------------------------------------------------------------
typedef struct ErrNode {
    int line;
    int column;
    char *msg;
    struct ErrNode *next;
} ErrNode;

static ErrNode *g_errors = NULL;
static int g_error_count = 0;

// portable vasprintf replacement
static char* portable_vasprintf(const char *fmt, va_list ap) {
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int needed = vsnprintf(NULL, 0, fmt, ap_copy);
    va_end(ap_copy);
    if (needed < 0) return strdup("semantic error");
    char *buffer = malloc(needed + 1);
    if (!buffer) return strdup("semantic error");
    vsnprintf(buffer, needed + 1, fmt, ap);
    return buffer;
}

static void record_error(int line, int col, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *msg = portable_vasprintf(fmt, ap);
    va_end(ap);
    ErrNode *e = malloc(sizeof(ErrNode));
    e->line = line;
    e->column = col;
    e->msg = msg;
    e->next = g_errors;
    g_errors = e;
    g_error_count++;
}

int semantic_has_errors(void) { return g_error_count; }

void semantic_print_errors(void) {
    if (!g_errors) return;
    int n = 0;
    for (ErrNode *e = g_errors; e; e = e->next) n++;
    ErrNode **arr = malloc(sizeof(ErrNode*) * n);
    int i = 0;
    for (ErrNode *e = g_errors; e; e = e->next) arr[i++] = e;
    for (int j = n - 1; j >= 0; j--) {
        ErrNode *e = arr[j];
        fprintf(stderr, "Semantic error at %d:%d: %s\n", e->line, e->column, e->msg);
        semantic_print_source_error(e->line, e->column);
        fprintf(stderr, "\n");
    }
    free(arr);
}

static void clear_errors(void) {
    ErrNode *e = g_errors;
    while (e) {
        ErrNode *n = e->next;
        free(e->msg);
        free(e);
        e = n;
    }
    g_errors = NULL;
    g_error_count = 0;
}

// -----------------------------------------------------------------------------
// map type strings to SymType
// -----------------------------------------------------------------------------
static SymType map_type_from_string(const char *s) {
    if (!s) return SYM_UNKNOWN;
    if (!strcmp(s, "NUM") || !strcmp(s, "num")) return SYM_NUM;
    if (!strcmp(s, "FRAC") || !strcmp(s, "frac")) return SYM_FRAC;
    if (!strcmp(s, "BOOL") || !strcmp(s, "bool")) return SYM_BOOL;
    if (!strcmp(s, "CHAR") || !strcmp(s, "char")) return SYM_CHAR;
    return SYM_UNKNOWN;
}

// Forward declarations
static void sem_visit_node(SymTable *st, ASTNode *node);
static void sem_visit_expr(SymTable *st, ASTNode *node);

// -----------------------------------------------------------------------------
// Declaration (variable)
static void sem_visit_declaration(SymTable *st, ASTNode *node) {
    if (!node) return;
    SymType t = map_type_from_string(node->value);
    ASTNode *idnode = node->left;
    if (!idnode || !idnode->value) {
        record_error(node->line, node->column, "invalid declaration (missing identifier)");
        return;
    }
    const char *name = idnode->value;
    int is_array = 0;
    int array_size = -1;
    ASTNode *r = node->right;
    // If AST used literal as size, detect heuristically
    if (r && r->type == NODE_LITERAL && r->right != NULL) {
        is_array = 1;
        array_size = atoi(r->value);
    }
    if (!symtable_add_var(st, name, t, is_array, array_size, node->line, node->column)) {
        record_error(node->line, node->column, "duplicate declaration of '%s'", name);
    }
    // visit initializer expr if present
    if (r) {
        if (is_array && r->right)
            sem_visit_node(st, r->right);
        else if (!is_array)
            sem_visit_node(st, r);
    }
}

// -----------------------------------------------------------------------------
// Assignment
static void sem_visit_assignment(SymTable *st, ASTNode *node) {
    if (!node) return;
    ASTNode *lhs = node->left;
    ASTNode *rhs = node->right;
    if (!lhs) {
        record_error(node->line, node->column, "malformed assignment (missing left side)");
        return;
    }
    if (lhs->type == NODE_IDENTIFIER) {
        Symbol *s = symtable_lookup(st, lhs->value);
        if (!s)
            record_error(lhs->line, lhs->column, "use of undeclared variable '%s'", lhs->value);
        else if (s->kind != SYM_KIND_VAR)
            record_error(lhs->line, lhs->column, "'%s' is not a variable", lhs->value);
    } else if (lhs->type == NODE_ARRAY_ACCESS) {
        if (!lhs->left || lhs->left->type != NODE_IDENTIFIER) {
            record_error(lhs->line, lhs->column, "invalid array access");
        } else {
            Symbol *s = symtable_lookup(st, lhs->left->value);
            if (!s)
                record_error(lhs->line, lhs->column, "use of undeclared array '%s'", lhs->left->value);
            else if (s->kind != SYM_KIND_VAR)
                record_error(lhs->line, lhs->column, "'%s' is not a variable", lhs->left->value);
            else if (!s->is_array)
                record_error(lhs->line, lhs->column, "'%s' is not an array", lhs->left->value);
        }
        if (lhs->right) sem_visit_node(st, lhs->right);
    } else {
        record_error(lhs->line, lhs->column, "invalid assignment target");
    }
    if (rhs) sem_visit_node(st, rhs);
}

// -----------------------------------------------------------------------------
// Function declaration
static void sem_visit_function_decl(SymTable *st, ASTNode *node) {
    if (!node) return;
    // node->value -> function name
    const char *fname = node->value;
    if (!fname) {
        record_error(node->line, node->column, "function missing name");
        return;
    }
    // get return type from body->value per convention B
    SymType ret_type = SYM_UNKNOWN;
    if (node->right && node->right->value) {
        ret_type = map_type_from_string(node->right->value);
    }

    // parse params to build param_types array
    int param_count = 0;
    SymType *param_types = NULL;
    if (node->left) {
        ASTNode *plist = node->left;
        // count params
        ASTNode *cur = plist;
        while (cur) {
            if (cur->left) param_count++;
            cur = cur->right;
        }
        if (param_count > 0) {
            param_types = (SymType*)malloc(sizeof(SymType) * param_count);
            int i = 0;
            cur = plist;
            while (cur) {
                ASTNode *param = cur->left; // NODE_PARAM
                if (param && param->value) {
                    param_types[i++] = map_type_from_string(param->value);
                } else {
                    param_types[i++] = SYM_UNKNOWN;
                }
                cur = cur->right;
            }
        }
    }

    // Add function symbol in current scope (should be global scope)
    if (!symtable_add_func(st, fname, ret_type, param_count, param_types, node->line, node->column)) {
        record_error(node->line, node->column, "duplicate function declaration '%s'", fname);
        if (param_types) free(param_types);
        return;
    }
    if (param_types) free(param_types);

    // Now open function scope, add parameters as variables and visit body
    symtable_enter_scope(st);
    // Insert params into new scope
    if (node->left) {
        ASTNode *cur = node->left;
        while (cur) {
            ASTNode *param = cur->left; // NODE_PARAM
            if (param) {
                // param->value -> type string, param->left is identifier node
                SymType ptype = map_type_from_string(param->value);
                if (param->left && param->left->value) {
                    const char *pname = param->left->value;
                    if (!symtable_add_var(st, pname, ptype, 0, -1, param->line, param->column)) {
                        record_error(param->line, param->column, "duplicate parameter name '%s' in function '%s'", pname, fname);
                    }
                }
            }
            cur = cur->right;
        }
    }

    // Visit body (node->right is the block)
    if (node->right) sem_visit_node(st, node->right);
    symtable_leave_scope(st);
}

// -----------------------------------------------------------------------------
// Function call
static void sem_visit_function_call(SymTable *st, ASTNode *node) {
    if (!node) return;
    // node->value == function name
    const char *fname = node->value;
    Symbol *s = symtable_lookup(st, fname);
    if (!s) {
        record_error(node->line, node->column, "call to undeclared function '%s'", fname);
        // still visit args
        if (node->left) sem_visit_node(st, node->left);
        return;
    }
    if (s->kind != SYM_KIND_FUNC) {
        record_error(node->line, node->column, "'%s' is not a function", fname);
        if (node->left) sem_visit_node(st, node->left);
        return;
    }
    // Check arg count
    int call_arg_count = 0;
    if (node->left) {
        ASTNode *cur = node->left;
        while (cur) { call_arg_count++; cur = cur->right; }
    }
    if (call_arg_count != s->param_count) {
        record_error(node->line, node->column, "function '%s' called with %d args, but declared with %d", fname, call_arg_count, s->param_count);
    }
    // visit each arg expression
    if (node->left) sem_visit_node(st, node->left);
}

// -----------------------------------------------------------------------------
// Expression visitor
static void sem_visit_expr(SymTable *st, ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case NODE_LITERAL: return;
        case NODE_IDENTIFIER: {
            if (!symtable_lookup(st, node->value))
                record_error(node->line, node->column, "use of undeclared identifier '%s'", node->value);
            return;
        }
        case NODE_ARRAY_ACCESS:
            if (node->left) sem_visit_expr(st, node->left);
            if (node->right) sem_visit_expr(st, node->right);
            return;
        case NODE_BINARY_OP:
        case NODE_UNARY_OP:
            if (node->left) sem_visit_expr(st, node->left);
            if (node->right) sem_visit_expr(st, node->right);
            return;
        case NODE_FUNCTION_CALL:
            sem_visit_function_call(st, node);
            return;
        default:
            if (node->left) sem_visit_node(st, node->left);
            if (node->right) sem_visit_node(st, node->right);
            return;
    }
}

// -----------------------------------------------------------------------------
// Main visitor
static void sem_visit_node(SymTable *st, ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case NODE_PROGRAM:
            // enter global scope
            symtable_enter_scope(st);
            if (node->left) sem_visit_node(st, node->left);
            // leave global scope (but caller of sem_check_program may want table preserved; we leave it)
            // NOTE: We will leave at the end of sem_check_program
            return;

        case NODE_STATEMENTS: {
            ASTNode *cur = node;
            while (cur) {
                if (cur->left) sem_visit_node(st, cur->left);
                cur = cur->right;
            }
            return;
        }

        case NODE_BLOCK:
            symtable_enter_scope(st);
            if (node->left) sem_visit_node(st, node->left);
            symtable_leave_scope(st);
            return;

        case NODE_DECLARATION:
            sem_visit_declaration(st, node);
            return;

        case NODE_ASSIGNMENT:
            sem_visit_assignment(st, node);
            return;

        case NODE_IF: {
            // structure: NODE_IF: left = condition, right = branch (then/else as blocks inside)
            if (node->left) sem_visit_expr(st, node->left);
            if (node->right) {
                // in your AST the then/else blocks are inside right->left and right->right maybe
                sem_visit_node(st, node->right);
            }
            return;
        }

        case NODE_FOR:
            symtable_enter_scope(st);
            if (node->left) sem_visit_node(st, node->left);
            if (node->right) sem_visit_node(st, node->right);
            symtable_leave_scope(st);
            return;

        case NODE_WHILE:
            if (node->left) sem_visit_expr(st, node->left);
            if (node->right) sem_visit_node(st, node->right);
            return;

        case NODE_RETURN:
            if (node->left) sem_visit_node(st, node->left);
            return;

        case NODE_BREAK: return;

        case NODE_FUNCTION_DECL:
            sem_visit_function_decl(st, node);
            return;

        case NODE_FUNCTION_CALL:
            sem_visit_function_call(st, node);
            return;

        case NODE_LITERAL:
        case NODE_IDENTIFIER:
        case NODE_ARRAY_ACCESS:
        case NODE_BINARY_OP:
        case NODE_UNARY_OP:
            sem_visit_expr(st, node);
            return;

        // robot commands - nothing to do semantically
        case NODE_CMD_AVANZAR:
        case NODE_CMD_RETROCEDER:
        case NODE_CMD_GIRAR_DER:
        case NODE_CMD_GIRAR_IZQ:
        case NODE_CMD_DETENER:
        case NODE_CMD_FAST:
        case NODE_CMD_SLOW:
        case NODE_CMD_IGLOBAL:
        case NODE_CMD_FGLOBAL:
        case NODE_CMD_DELAY:
            return;

        default:
            if (node->left) sem_visit_node(st, node->left);
            if (node->right) sem_visit_node(st, node->right);
            return;
    }
}

// -----------------------------------------------------------------------------
// Public API
int sem_check_program(ASTNode *root, SymTable *st) {
    clear_errors();
    if (!root || !st) return 0;
    sem_visit_node(st, root);
    // leave global scope if still present
    // (sem_visit_node entered one at program start)
    // caller might want to keep symbol table, so we won't destroy it here.
    return g_error_count;
}
