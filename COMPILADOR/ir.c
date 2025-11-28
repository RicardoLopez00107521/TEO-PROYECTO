#include "ir.h"

/* -------------------------
   Chunk
   ------------------------- */
void chunk_init(Chunk *c) {
    c->code = NULL;
    c->count = 0;
    c->capacity = 0;
}

void chunk_free(Chunk *c) {
    if (!c) return;
    if (c->code) free(c->code);
    c->code = NULL;
    c->count = c->capacity = 0;
}

static void chunk_ensure(Chunk *c, int need) {
    if (c->count + need <= c->capacity) return;
    int newcap = c->capacity ? c->capacity : 64;
    while (c->count + need > newcap) newcap *= 2;
    c->code = (int*)realloc(c->code, sizeof(int) * newcap);
    c->capacity = newcap;
}

int chunk_emit(Chunk *c, int v) {
    chunk_ensure(c, 1);
    c->code[c->count++] = v;
    return c->count - 1;
}

/* emite opcode seguido de un argumento entero (muy usado) */
int chunk_emit_op_arg(Chunk *c, int op, int arg) {
    chunk_emit(c, op);
    chunk_emit(c, arg);
    return c->count - 2;
}

/* -------------------------
   Function
   ------------------------- */
void function_init(Function *f, const char *name) {
    f->name = name ? strdup(name) : NULL;
    f->arity = 0;
    f->locals = 0;
    chunk_init(&f->chunk);
    f->const_count = 0;
    f->const_cap = 0;
    f->consts = NULL;
}

void function_free(Function *f) {
    if (!f) return;
    if (f->name) { free(f->name); f->name = NULL; }
    chunk_free(&f->chunk);
    if (f->consts) { free(f->consts); f->consts = NULL; }
    f->const_count = f->const_cap = 0;
    f->arity = f->locals = 0;
}

/* añade una constante y devuelve su índice; evita duplicados básicos */
int function_add_const(Function *f, Value v) {
    /* intento de evitar duplicados (útil en programas pequeños) */
    for (int i = 0; i < f->const_count; ++i) {
        Value *ex = &f->consts[i];
        if (ex->type == v.type) {
            if (v.type == TY_INT && ex->as.l == v.as.l) return i;
            if (v.type == TY_LONG && ex->as.l == v.as.l) return i;
            if (v.type == TY_FLOAT && ex->as.f == v.as.f) return i;
            if (v.type == TY_CHAR && ex->as.c == v.as.c) return i;
            if (v.type == TY_BOOL && ex->as.b == v.as.b) return i;
        }
    }

    if (f->const_count + 1 > f->const_cap) {
        int newcap = f->const_cap ? f->const_cap * 2 : 8;
        f->consts = (Value *)realloc(f->consts, sizeof(Value) * newcap);
        f->const_cap = newcap;
    }
    f->consts[f->const_count] = v;
    return f->const_count++;
}

/* -------------------------
   Module
   ------------------------- */
void module_init(Module *m) {
    m->funcs = NULL;
    m->count = 0;
    m->capacity = 0;
}

/* agrega la función al módulo (se copia la estructura Function tal cual) */
int module_add_function(Module *m, Function f) {
    if (m->count + 1 > m->capacity) {
        int newcap = m->capacity ? m->capacity * 2 : 8;
        m->funcs = (Function *)realloc(m->funcs, sizeof(Function) * newcap);
        m->capacity = newcap;
    }
    m->funcs[m->count] = f; /* copia por valor (incluye punteros) */
    return m->count++;
}

void module_free(Module *m) {
    if (!m) return;
    for (int i = 0; i < m->count; ++i) {
        function_free(&m->funcs[i]);
    }
    free(m->funcs);
    m->funcs = NULL;
    m->count = m->capacity = 0;
}

/* -------------------------
   Dump legible y nombres de opcode
   ------------------------- */
static const char *opname(int op) {
    switch (op) {
        case OP_NOP: return "NOP";
        case OP_CONST: return "CONST";
        case OP_LOAD_LOCAL: return "LOAD_LOCAL";
        case OP_STORE_LOCAL: return "STORE_LOCAL";
        case OP_ADD: return "ADD";
        case OP_SUB: return "SUB";
        case OP_MUL: return "MUL";
        case OP_DIV: return "DIV";
        case OP_MOD: return "MOD";
        case OP_EQ: return "EQ";
        case OP_NEQ: return "NEQ";
        case OP_LT: return "LT";
        case OP_GT: return "GT";
        case OP_LE: return "LE";
        case OP_GE: return "GE";
        case OP_AND: return "AND";
        case OP_OR: return "OR";
        case OP_NOT: return "NOT";
        case OP_JMP: return "JMP";
        case OP_JZ: return "JZ";
        case OP_PRINT: return "PRINT";
        case OP_POP: return "POP";
        case OP_RET: return "RET";
        case OP_HALT: return "HALT";
        case OP_AVANZAR: return "AVANZAR";
        case OP_RETROCEDER: return "RETROCEDER";
        case OP_GIRAR_DER: return "GIRAR_DER";
        case OP_GIRAR_IZQ: return "GIRAR_IZQ";
        case OP_DETENER: return "DETENER";
        case OP_INICIAR_LOOP: return "INICAR_LOOP";
        case OP_SENSOR_DER: return "SENSOR_DER";
        case OP_SENSOR_IZQ: return "SENSOR_IZQ";
        case OP_AVANZAR_INDEF: return "AVANZAR_INDEF";
        case OP_FAST: return "ACELERAR";
        case OP_SLOW: return "DESACELERAR";
        case OP_IGLOBAL: return "INIT_GLOBAL";
        case OP_FGLOBAL: return "END_GLOBAL";
        case OP_DELAY: return "DELAY";
        case OP_NEW_ARRAY: return "NEW_ARRAY";
        case OP_LOAD_INDEX: return "LOAD_INDEX";
        case OP_STORE_INDEX: return "STORE_INDEX";
        case OP_CALL: return "CALL";
        default: return "UNKNOWN";
    }
}

void dump_module(Module *m, FILE *out) {
    if (!m || !out) return;
    for (int fi = 0; fi < m->count; ++fi) {
        Function *f = &m->funcs[fi];
        fprintf(out, "function %s (arity=%d locals=%d) consts=%d code=%d\n",
                f->name ? f->name : "<anon>",
                f->arity,
                f->locals,
                f->const_count,
                f->chunk.count);
        for (int ci = 0; ci < f->const_count; ++ci) {
            Value *v = &f->consts[ci];
            switch (v->type) {
                case TY_INT:  fprintf(out, "  const[%d] INT  %lld\n", ci, (long long)v->as.l); break;
                case TY_LONG: fprintf(out, "  const[%d] LONG %lld\n", ci, (long long)v->as.l); break;
                case TY_FLOAT:fprintf(out, "  const[%d] FLOAT %g\n", ci, v->as.f); break;
                case TY_CHAR: fprintf(out, "  const[%d] CHAR '%c'\n", ci, v->as.c); break;
                case TY_BOOL: fprintf(out, "  const[%d] BOOL %d\n", ci, v->as.b); break;
                default:      fprintf(out, "  const[%d] ? type=%d\n", ci, v->type); break;
            }
        }
        int ip = 0;
        while (ip < f->chunk.count) {
            int op = f->chunk.code[ip++];
            fprintf(out, "  %04d: %s", ip-1, opname(op));
            /* imprime argumentos dependiendo de la instrucción */
            switch (op) {
                case OP_CONST:
                case OP_LOAD_LOCAL:
                case OP_STORE_LOCAL:
                case OP_JMP:
                case OP_JZ:
                    if (ip < f->chunk.count) fprintf(out, " %d", f->chunk.code[ip++]);
                    break;
                case OP_CALL:
                    if (ip + 1 < f->chunk.count) {
                        int fid = f->chunk.code[ip++]; int ar = f->chunk.code[ip++];
                        fprintf(out, " func=%d arity=%d", fid, ar);
                    }
                    break;
                case OP_NEW_ARRAY:
                    if (ip + 1 < f->chunk.count) {
                        int len = f->chunk.code[ip++]; int et = f->chunk.code[ip++];
                        fprintf(out, " len=%d et=%d", len, et);
                    }
                    break;
                default:
                    break;
            }
            fprintf(out, "\n");
        }
        fprintf(out, "\n");
    }
}

/* -------------------------
   Serialización binaria simple (formato "TEO2")
   layout:
     "TEO2" (4 bytes)
     int funcs_count
     for each function:
       int name_len
       bytes name (no null)
       int locals
       int const_count
       for each const:
         int type
         payload depending on type (int64 for ints, double for float, int for bool, char for char)
       int code_count
       code[] (ints)
   ------------------------- */
int module_serialize_binary(Module *m, FILE *out) {
    if (!m || !out) return 0;
    fwrite("TEO2", 1, 4, out);
    fwrite(&m->count, sizeof(int), 1, out);
    for (int fi = 0; fi < m->count; ++fi) {
        Function *f = &m->funcs[fi];
        int nlen = f->name ? (int)strlen(f->name) : 0;
        fwrite(&nlen, sizeof(int), 1, out);
        if (nlen) fwrite(f->name, 1, nlen, out);
        fwrite(&f->locals, sizeof(int), 1, out);
        fwrite(&f->const_count, sizeof(int), 1, out);
        for (int ci = 0; ci < f->const_count; ++ci) {
            Value *v = &f->consts[ci];
            int t = (int)v->type;
            fwrite(&t, sizeof(int), 1, out);
            switch (v->type) {
                case TY_INT:
                case TY_LONG:
                    fwrite(&v->as.l, sizeof(int64_t), 1, out);
                    break;
                case TY_FLOAT:
                    fwrite(&v->as.f, sizeof(double), 1, out);
                    break;
                case TY_BOOL:
                    fwrite(&v->as.b, sizeof(int), 1, out);
                    break;
                case TY_CHAR:
                    fwrite(&v->as.c, sizeof(char), 1, out);
                    break;
                default: {
                    int z = 0; fwrite(&z, sizeof(int), 1, out);
                } break;
            }
        }
        fwrite(&f->chunk.count, sizeof(int), 1, out);
        if (f->chunk.count > 0)
            fwrite(f->chunk.code, sizeof(int), f->chunk.count, out);
    }
    return 1;
}
