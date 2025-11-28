// codegen.c
// Backend que convierte el AST (parser.h) a IR (ir.h).
// Soporta funciones, llamadas, y comandos-flag del robot.
// Parche: recolección recursiva de funciones para evitar que queden dentro de main.

#include "ir.h"
#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------
   Estructuras para manejo de variables locales y scopes
   --------------------------- */

typedef struct LocalVar {
    char *name;
    int index;
    struct LocalVar *next;
} LocalVar;

typedef struct LocalScope {
    LocalVar *vars;
    struct LocalScope *prev;
} LocalScope;

/* --- estructura para soporte de break --- */
typedef struct {
    int *positions;
    int count;
    int cap;
} BreakStack;

static void bs_init(BreakStack *bs) { bs->positions = NULL; bs->count = 0; bs->cap = 0; }
static void bs_free(BreakStack *bs) { if (bs->positions) free(bs->positions); bs->positions = NULL; bs->count = bs->cap = 0; }
static void bs_push(BreakStack *bs, int pos) {
    if (bs->count + 1 > bs->cap) {
        bs->cap = bs->cap ? bs->cap * 2 : 16;
        bs->positions = (int*)realloc(bs->positions, sizeof(int) * bs->cap);
    }
    bs->positions[bs->count++] = pos;
}

/* --------------------------- */

typedef struct {
    Function *fn;        // apunta a la función en Module (evitamos copias problemáticas)
    LocalScope *scope;
    BreakStack breaks;
    int temp_counter;    // para generar nombres temporales únicos
} CG;

/* ---------------------------
   Scope helpers
   --------------------------- */

static void cg_push_scope(CG *cg) {
    LocalScope *s = (LocalScope*)malloc(sizeof(LocalScope));
    s->vars = NULL;
    s->prev = cg->scope;
    cg->scope = s;
}

static void cg_pop_scope(CG *cg) {
    if (!cg->scope) return;
    LocalVar *v = cg->scope->vars;
    while (v) {
        LocalVar *n = v->next;
        if (v->name) free(v->name);
        free(v);
        v = n;
    }
    LocalScope *prev = cg->scope->prev;
    free(cg->scope);
    cg->scope = prev;
}

/* devuelve índice asignado (usa fn->locals) */
static int cg_add_local(CG *cg, const char *name) {
    int idx = cg->fn->locals;
    cg->fn->locals++;

    LocalVar *lv = (LocalVar*)malloc(sizeof(LocalVar));
    lv->name = name ? strdup(name) : NULL;
    lv->index = idx;
    lv->next = cg->scope ? cg->scope->vars : NULL;
    if (cg->scope) cg->scope->vars = lv;
    return idx;
}

/* busca variable en scopes - retorna index o -1 */
static int cg_lookup(CG *cg, const char *name) {
    if (!name) return -1;
    LocalScope *s = cg->scope;
    while (s) {
        LocalVar *v = s->vars;
        while (v) {
            if (v->name && strcmp(v->name, name) == 0) return v->index;
            v = v->next;
        }
        s = s->prev;
    }
    return -1;
}

/* ---------------------------
   Emisión helpers (trabajan con cg->fn)
   --------------------------- */

static void emit_int(CG *cg, int v) { chunk_emit(&cg->fn->chunk, v); }
static void emit_op(CG *cg, OpCode op) { chunk_emit(&cg->fn->chunk, (int)op); }
/* emite op,arg1 */
static void emit_op_arg1(CG *cg, OpCode op, int arg) { chunk_emit_op_arg(&cg->fn->chunk, (int)op, arg); }
/* emite op,arg1,arg2 */
static void emit_op_arg2(CG *cg, OpCode op, int arg1, int arg2) {
    chunk_emit(&cg->fn->chunk, (int)op);
    chunk_emit(&cg->fn->chunk, arg1);
    chunk_emit(&cg->fn->chunk, arg2);
}

/* --- break placeholder emit --- */
static void emit_break_placeholder(CG *cg) {
    int pos = cg->fn->chunk.count;
    emit_op(cg, OP_JMP);
    emit_int(cg, -1);
    bs_push(&cg->breaks, pos);
}

/* forward declarations */
static void cg_expr(CG *cg, ASTNode *n);
static void cg_stmt(CG *cg, ASTNode *n);

/* ---------------------------
   Literales
   --------------------------- */

static Value make_int(long long x) { Value v; v.type = TY_INT; v.as.l = x; return v; }
static Value make_float(double f) { Value v; v.type = TY_FLOAT; v.as.f = f; return v; }
static Value make_bool(int b) { Value v; v.type = TY_BOOL; v.as.b = b; v.as.l = b ? 1 : 0; return v; }
static Value make_char(char c) { Value v; v.type = TY_CHAR; v.as.c = c; return v; }

static void cg_literal(CG *cg, ASTNode *n) {
    if (!n || !n->value) return;

    if (strcmp(n->value, "ahuevo") == 0) {
        int ci = function_add_const(cg->fn, make_bool(1));
        emit_op_arg1(cg, OP_CONST, ci);
        return;
    } else if (strcmp(n->value, "paja") == 0) {
        int ci = function_add_const(cg->fn, make_bool(0));
        emit_op_arg1(cg, OP_CONST, ci);
        return;
    }

    if (strchr(n->value, '.')) {
        double fv = atof(n->value);
        int ci = function_add_const(cg->fn, make_float(fv));
        emit_op_arg1(cg, OP_CONST, ci);
        return;
    }

    long long iv = atoll(n->value);
    int ci = function_add_const(cg->fn, make_int(iv));
    emit_op_arg1(cg, OP_CONST, ci);
}

/* ---------------------------
   Expresiones
   --------------------------- */

static void cg_expr(CG *cg, ASTNode *n) {
    if (!n) return;

    switch (n->type) {

        case NODE_LITERAL:
            cg_literal(cg, n);
            return;

        case NODE_IDENTIFIER: {
            // --- SENSORES ESPECIALES ---
            if (strcmp(n->value, "sensorDerecho") == 0) {
                emit_op(cg, OP_SENSOR_DER);
                return;
            }

            if (strcmp(n->value, "sensorIzquierdo") == 0) {
                emit_op(cg, OP_SENSOR_IZQ);
                return;
            }

            // --- RESTO DE IDENTIFICADORES NORMAL ---
            int idx = cg_lookup(cg, n->value);
            if (idx < 0) {
                idx = cg_add_local(cg, n->value);
                int ci0 = function_add_const(cg->fn, make_int(0));
                emit_op_arg1(cg, OP_CONST, ci0);
                emit_op_arg1(cg, OP_STORE_LOCAL, idx);
            }

            emit_op_arg1(cg, OP_LOAD_LOCAL, idx);
            return;
        }

        case NODE_BINARY_OP: {
            cg_expr(cg, n->left);
            cg_expr(cg, n->right);
            const char *op = n->value;
            if (!op) return;

            if (strcmp(op, "+") == 0) emit_op(cg, OP_ADD);
            else if (strcmp(op, "-") == 0) emit_op(cg, OP_SUB);
            else if (strcmp(op, "*") == 0) emit_op(cg, OP_MUL);
            else if (strcmp(op, "/") == 0) emit_op(cg, OP_DIV);
            else if (strcmp(op, "%") == 0) emit_op(cg, OP_MOD);
            else if (strcmp(op, "<") == 0) emit_op(cg, OP_LT);
            else if (strcmp(op, ">") == 0) emit_op(cg, OP_GT);
            else if (strcmp(op, "<=") == 0) emit_op(cg, OP_LE);
            else if (strcmp(op, ">=") == 0) emit_op(cg, OP_GE);
            else if (strcmp(op, "==") == 0) emit_op(cg, OP_EQ);
            else if (strcmp(op, "!=") == 0) emit_op(cg, OP_NEQ);
            else if (strcmp(op, "AND") == 0) emit_op(cg, OP_AND);
            else if (strcmp(op, "OR") == 0) emit_op(cg, OP_OR);
            return;
        }

        case NODE_UNARY_OP: {
            if (!n->value) return;
            if (strcmp(n->value, "-") == 0) {
                // generate NEG: store operand in temp local, push 0, load temp, SUB
                cg_expr(cg, n->left); // operand on top
                char tmpname[32];
                snprintf(tmpname, sizeof(tmpname), "__neg_tmp_%d", ++cg->temp_counter);
                int tmp = cg_add_local(cg, tmpname);
                emit_op_arg1(cg, OP_STORE_LOCAL, tmp);
                int ci0 = function_add_const(cg->fn, make_int(0));
                emit_op_arg1(cg, OP_CONST, ci0);
                emit_op_arg1(cg, OP_LOAD_LOCAL, tmp);
                emit_op(cg, OP_SUB);
                return;
            } else if (strcmp(n->value, "NOT") == 0) {
                cg_expr(cg, n->left);
                emit_op(cg, OP_NOT);
                return;
            }
            return;
        }

        case NODE_FUNCTION_CALL: {
            // Evaluate arguments in order
            ASTNode *args = n->left;
            int arity = 0;
            ASTNode *it = args;
            while (it) { arity++; it = it->right; }
            if (arity > 0) {
                ASTNode **arr = (ASTNode**)malloc(sizeof(ASTNode*) * arity);
                int i = 0;
                it = args;
                while (it) { arr[i++] = it->left; it = it->right; }
                for (int j = 0; j < arity; ++j) cg_expr(cg, arr[j]);
                free(arr);
            }

            // find function index by name using g_codegen_module
            extern Module *g_codegen_module;
            int fid = -1;
            if (g_codegen_module && n->value) {
                for (int i = 0; i < g_codegen_module->count; ++i) {
                    if (g_codegen_module->funcs[i].name && strcmp(g_codegen_module->funcs[i].name, n->value) == 0) {
                        fid = i;
                        break;
                    }
                }
            }

            // emit CALL func_index, arity (fid may be -1 if unresolved)
            emit_op_arg2(cg, OP_CALL, fid, arity);
            return;
        }

        case NODE_ARRAY_ACCESS: {
            if (n->left) cg_expr(cg, n->left);
            if (n->right) cg_expr(cg, n->right);
            emit_op(cg, OP_LOAD_INDEX);
            return;
        }

        default:
            if (n->left) cg_expr(cg, n->left);
            if (n->right) cg_expr(cg, n->right);
            return;
    }
}

/* ---------------------------
   Statements
   --------------------------- */

static void cg_block(CG *cg, ASTNode *n) {
    if (!n) return;
    cg_push_scope(cg);

    if (n->left) cg_stmt(cg, n->left);

    cg_pop_scope(cg);
}

static void cg_stmt(CG *cg, ASTNode *n) {
    if (!n) return;

    switch (n->type) {

        case NODE_STATEMENTS: {
            ASTNode *cur = n;
            while (cur) {
                ASTNode *st = cur->left;

                // ❌ NO generar código para funciones dentro de main
                if (st && st->type == NODE_FUNCTION_DECL) {
                    // ignorar, ya se generan en la fase 2 del codegen
                    cur = cur->right;
                    continue;
                }

                if (st) cg_stmt(cg, st);
                cur = cur->right;
            }
            return;
        }

        case NODE_BREAK: {
            emit_break_placeholder(cg);
            return;
        }

        case NODE_DECLARATION: {
            const char *vname = n->left ? n->left->value : NULL;
            if (!vname) return;
            int idx = cg_add_local(cg, vname);

            if (n->right) {
                ASTNode *init = n->right;
                if (init->type == NODE_LITERAL && init->right != NULL) {
                    if (init->right) cg_expr(cg, init->right);
                } else {
                    cg_expr(cg, init);
                }
                emit_op_arg1(cg, OP_STORE_LOCAL, idx);
            } else {
                int ci = function_add_const(cg->fn, make_int(0));
                emit_op_arg1(cg, OP_CONST, ci);
                emit_op_arg1(cg, OP_STORE_LOCAL, idx);
            }
            return;
        }

        case NODE_ASSIGNMENT: {
            if (n->left->type == NODE_IDENTIFIER) {
                const char *vname = n->left->value;
                int idx = cg_lookup(cg, vname);
                if (idx < 0) idx = cg_add_local(cg, vname);
                cg_expr(cg, n->right);
                emit_op_arg1(cg, OP_STORE_LOCAL, idx);
            } else if (n->left->type == NODE_ARRAY_ACCESS) {
                cg_expr(cg, n->left->left);  // base
                cg_expr(cg, n->left->right); // index
                cg_expr(cg, n->right);       // value
                emit_op(cg, OP_STORE_INDEX);
            }
            return;
        }

        case NODE_IF: {

            // 1) Generar condición
            cg_expr(cg, n->left);

            // ------------------------------------------------------------------
            // 2) Hook de sensores basado en nombre de variable
            // ------------------------------------------------------------------
            if (n->left->type == NODE_IDENTIFIER && n->left->value) {

                const char *vname = n->left->value;

                if (strcmp(vname, "sensorDerecho") == 0 ||
                    strcmp(vname, "usarSensorDerecho") == 0 ||
                    strcmp(vname, "activarSensorDerecho") == 0) {

                    emit_op(cg, OP_SENSOR_DER);
                }

                if (strcmp(vname, "sensorIzquierdo") == 0 ||
                    strcmp(vname, "usarSensorIzquierdo") == 0 ||
                    strcmp(vname, "activarSensorIzquierdo") == 0) {

                    emit_op(cg, OP_SENSOR_IZQ);
                }
            }

            // 3) Emitir JZ
            int jz_pos = cg->fn->chunk.count;
            emit_op(cg, OP_JZ);
            emit_int(cg, -1);

            // 4) Cuerpo del THEN
            if (n->right && n->right->left)
                cg_stmt(cg, n->right->left);

            int after_then = cg->fn->chunk.count;

            // 5) ELSE opcional
            if (n->right && n->right->right) {
                int jmp_pos = cg->fn->chunk.count;
                emit_op(cg, OP_JMP);
                emit_int(cg, -1);

                int else_start = cg->fn->chunk.count;
                cg->fn->chunk.code[jz_pos + 1] = else_start;

                cg_stmt(cg, n->right->right);

                int after_else = cg->fn->chunk.count;
                cg->fn->chunk.code[jmp_pos + 1] = after_else;
            } else {
                cg->fn->chunk.code[jz_pos + 1] = after_then;
            }

            return;
        }

        case NODE_WHILE: {
            int break_marker = cg->breaks.count;

            int loop_start = cg->fn->chunk.count;
            cg_expr(cg, n->left);

            int jz_pos = cg->fn->chunk.count;
            emit_op(cg, OP_JZ);
            emit_int(cg, -1);

            if (n->right) cg_stmt(cg, n->right);

            emit_op_arg1(cg, OP_JMP, loop_start);

            int exit_pos = cg->fn->chunk.count;
            cg->fn->chunk.code[jz_pos + 1] = exit_pos;

            for (int i = break_marker; i < cg->breaks.count; i++) {
                int p = cg->breaks.positions[i];
                cg->fn->chunk.code[p + 1] = exit_pos;
            }
            cg->breaks.count = break_marker;

            return;
        }

        case NODE_FOR: {
            ASTNode *init  = n->left;
            ASTNode *inner = n->right;

            ASTNode *cond = NULL;
            ASTNode *body = NULL;
            ASTNode *update = NULL;

            if (inner) {
                cond   = inner->left;
                ASTNode *block = inner->right;
                if (block) {
                    body   = block->left;
                    update = block->right;
                }
            }

            if (init) cg_stmt(cg, init);

            int break_marker = cg->breaks.count;

            int loop_start = cg->fn->chunk.count;

            if (cond) cg_expr(cg, cond);
            else {
                int cidx = function_add_const(cg->fn, make_bool(1));
                emit_op_arg1(cg, OP_CONST, cidx);
            }

            int jz_exit = cg->fn->chunk.count;
            emit_op(cg, OP_JZ);
            emit_int(cg, -1);

            if (body) cg_stmt(cg, body);

            if (update) cg_stmt(cg, update);

            emit_op_arg1(cg, OP_JMP, loop_start);

            int exit_pos = cg->fn->chunk.count;
            cg->fn->chunk.code[jz_exit + 1] = exit_pos;

            for (int i = break_marker; i < cg->breaks.count; i++) {
                int pos = cg->breaks.positions[i];
                cg->fn->chunk.code[pos + 1] = exit_pos;
            }

            cg->breaks.count = break_marker;
            return;
        }

        case NODE_RETURN: {
            if (n->left) cg_expr(cg, n->left);
            emit_op(cg, OP_RET);
            return;
        }

        case NODE_BLOCK:
            cg_block(cg, n);
            return;

        case NODE_FUNCTION_CALL:
            cg_expr(cg, n);
            emit_op(cg, OP_POP); // discard return if used as statement
            return;

        /* Comandos del robot — ahora se emiten opcodes directos (sin flags locals) */
        case NODE_CMD_AVANZAR:
            emit_op(cg, OP_AVANZAR);
            return;
        case NODE_CMD_RETROCEDER:
            emit_op(cg, OP_RETROCEDER);
            return;
        case NODE_CMD_GIRAR_DER:
            emit_op(cg, OP_GIRAR_DER);
            return;
        case NODE_CMD_GIRAR_IZQ:
            emit_op(cg, OP_GIRAR_IZQ);
            return;
        case NODE_CMD_DETENER:
            emit_op(cg, OP_DETENER);
            return;
        case NODE_CMD_DELAY:
            emit_op(cg, OP_DELAY);
            return;
        case NODE_CMD_FAST:
            emit_op(cg, OP_FAST);
            return;

        case NODE_CMD_SLOW:
            emit_op(cg, OP_SLOW);
            return;

        case NODE_CMD_IGLOBAL:
            emit_op(cg, OP_IGLOBAL);
            return;

        case NODE_CMD_FGLOBAL:
            emit_op(cg, OP_FGLOBAL);
            return;

        default:
            if (n->left) cg_stmt(cg, n->left);
            if (n->right) cg_stmt(cg, n->right);
            return;
    }
}

/* ---------------------------
   GLOBAL: pointer al módulo en generación (usado para resolución de llamadas)
   --------------------------- */
Module *g_codegen_module = NULL;

/* ---------------------------
   Collect functions recursively (FIX)
   - Busca NODE_FUNCTION_DECL en todo el subtree, crea prototipos en el module
   - Rellena arrays func_asts/func_names en el mismo orden que se añaden al module
*/
static void collect_functions(ASTNode *n, Module *mod,
                              ASTNode ***asts, char ***names,
                              int *count, int *cap)
{
    if (!n) return;

    if (n->type == NODE_FUNCTION_DECL) {
        if (*count + 1 > *cap) {
            *cap = (*cap ? *cap * 2 : 16);
            *asts = (ASTNode**)realloc(*asts, sizeof(ASTNode*) * (*cap));
            *names = (char**)realloc(*names, sizeof(char*) * (*cap));
        }

        (*asts)[*count] = n;
        (*names)[*count] = strdup(n->value ? n->value : "<anon>");

        Function f;
        function_init(&f, n->value ? n->value : NULL);

        // compute arity
        int ar = 0;
        ASTNode *plist = n->left;
        if (plist) {
            if (plist->type == NODE_PARAM_LIST) {
                ASTNode *it = plist;
                while (it) {
                    if (it->left && it->left->type == NODE_PARAM) ar++;
                    it = it->right;
                }
            } else if (plist->type == NODE_PARAM) {
                ar = 1;
            }
        }
        f.arity = ar;

        module_add_function(mod, f); // add proto (body filled later)
        (*count)++;
        return;
    }

    collect_functions(n->left, mod, asts, names, count, cap);
    collect_functions(n->right, mod, asts, names, count, cap);
}

/* ---------------------------
   Punto de entrada
   --------------------------- */

int codegen_compile(ASTNode *root, Module *mod) {
    if (!root || !mod) return 0;

    module_init(mod);

    // temporales
    ASTNode **func_asts = NULL;
    char **func_names = NULL;
    int func_count = 0;
    int func_cap = 0;

    // Primera pasada (FIX): recolectar funciones en todo el árbol
    collect_functions(root, mod, &func_asts, &func_names, &func_count, &func_cap);

    // Crear main al final (main será la última función)
    Function mainf;
    function_init(&mainf, "main");
    mainf.arity = 0;
    module_add_function(mod, mainf);

    // Exponer módulo global para resolución de llamadas
    g_codegen_module = mod;

    // Segunda pasada: generar código para cada función (0..func_count-1) y luego main (index func_count)
    for (int fi = 0; fi < mod->count; ++fi) {
        Function *F = &mod->funcs[fi];

        CG cg;
        cg.fn = F;
        cg.scope = NULL;
        bs_init(&cg.breaks);
        cg.temp_counter = 0;

        cg_push_scope(&cg);

        ASTNode *decl_ast = NULL;
        if (fi < func_count) decl_ast = func_asts[fi];

        // if user function, set up parameter locals (slots 0..arity-1)
        if (decl_ast && decl_ast->left) {
            ASTNode *plist = decl_ast->left;
            if (plist->type == NODE_PARAM_LIST) {
                ASTNode *it = plist;
                while (it) {
                    ASTNode *param = it->left; // NODE_PARAM
                    if (param && param->left && param->left->type == NODE_IDENTIFIER) {
                        char *pname = param->left->value;
                        if (pname) cg_add_local(&cg, pname);
                    }
                    it = it->right;
                }
            } else if (plist->type == NODE_PARAM) {
                ASTNode *param = plist;
                if (param && param->left && param->left->type == NODE_IDENTIFIER) {
                    char *pname = param->left->value;
                    if (pname) cg_add_local(&cg, pname);
                }
            }
        }

        if (fi < func_count) {
            // generate body for user function
            ASTNode *body = decl_ast->right;
            if (body) cg_stmt(&cg, body);
            // ensure RET at end of function
            if (cg.fn->chunk.count == 0 || cg.fn->chunk.code[cg.fn->chunk.count - 1] != OP_RET) {
                emit_op(&cg, OP_RET);
            }
        } else {
            // main: generate from root->left (top-level statements)
            if (root->left) cg_stmt(&cg, root->left);
            emit_op(&cg, OP_HALT);
        }

        cg_pop_scope(&cg);
        bs_free(&cg.breaks);
    }

    // cleanup temps
    for (int i = 0; i < func_count; ++i) {
        if (func_names[i]) free(func_names[i]);
    }
    free(func_names);
    free(func_asts);

    return 1;
}
