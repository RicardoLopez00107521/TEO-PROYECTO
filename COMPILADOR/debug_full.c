// vm_debug_full.c
// VM de debug completo: ejecuta llamadas/frames/returns.
// Diseñado para usar las estructuras de ir.h (Module, Function, Value, OpCode, etc.)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ir.h"

/* ------------------------
   Dinamic value stack
   ------------------------ */
typedef struct {
    Value *data;
    int sp;     // top index, -1 empty
    int cap;
} ValStack;

static void vs_init(ValStack *s) { s->data = NULL; s->sp = -1; s->cap = 0; }
static void vs_free(ValStack *s) { if (s->data) free(s->data); s->data = NULL; s->sp = -1; s->cap = 0; }
static void vs_push(ValStack *s, Value v) {
    if (s->sp + 1 >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 256;
        s->data = (Value*)realloc(s->data, sizeof(Value) * s->cap);
    }
    s->data[++s->sp] = v;
}
static Value vs_pop(ValStack *s) {
    Value z;
    z.type = TY_INT; z.as.l = 0;
    if (s->sp < 0) return z;
    return s->data[s->sp--];
}
static Value vs_peek(ValStack *s, int offset_from_top) {
    int idx = s->sp - offset_from_top;
    if (idx < 0) { Value z; z.type = TY_INT; z.as.l = 0; return z; }
    return s->data[idx];
}

/* ------------------------
   Call frame
   ------------------------ */
typedef struct {
    Function *fn;     // function being executed
    int ip;           // instruction pointer in fn->chunk.code
    int *locals;      // array of locals (size fn->locals)
    int locals_count;
} CallFrame;

/* Stack of call frames */
typedef struct {
    CallFrame *frames;
    int sp;
    int cap;
} FrameStack;

static void fs_init(FrameStack *fs) { fs->frames = NULL; fs->sp = -1; fs->cap = 0; }
static void fs_free(FrameStack *fs) {
    if (!fs->frames) return;
    // free allocated locals in frames
    for (int i = 0; i <= fs->sp; ++i) {
        if (fs->frames[i].locals) free(fs->frames[i].locals);
    }
    free(fs->frames);
    fs->frames = NULL; fs->sp = -1; fs->cap = 0;
}
static CallFrame* fs_push(FrameStack *fs) {
    if (fs->sp + 1 >= fs->cap) {
        fs->cap = fs->cap ? fs->cap * 2 : 64;
        fs->frames = (CallFrame*)realloc(fs->frames, sizeof(CallFrame) * fs->cap);
    }
    CallFrame *f = &fs->frames[++fs->sp];
    f->fn = NULL; f->ip = 0; f->locals = NULL; f->locals_count = 0;
    return f;
}
static void fs_pop(FrameStack *fs) {
    if (fs->sp < 0) return;
    CallFrame *f = &fs->frames[fs->sp];
    if (f->locals) free(f->locals);
    f->locals = NULL;
    f->fn = NULL;
    f->ip = 0;
    f->locals_count = 0;
    fs->sp--;
}

/* ------------------------
   opname helper (mirror of ir.c)
   ------------------------ */
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
        case OP_NEW_ARRAY: return "NEW_ARRAY";
        case OP_LOAD_INDEX: return "LOAD_INDEX";
        case OP_STORE_INDEX: return "STORE_INDEX";
        case OP_CALL: return "CALL";
        case OP_AVANZAR: return "AVANZAR";
        case OP_RETROCEDER: return "RETROCEDER";
        case OP_GIRAR_DER: return "GIRAR_DER";
        case OP_GIRAR_IZQ: return "GIRAR_IZQ";
        case OP_INICIAR_LOOP: return "INICIAR_LOOP";
        case OP_SENSOR_DER: return "SENSOR_DER";
        case OP_SENSOR_IZQ: return "SENSOR_IZQ";
        case OP_AVANZAR_INDEF: return "AVANZAR_INDEF";
        case OP_DETENER: return "DETENER";
        default: return "UNKNOWN";
    }
}

/* pretty-print Value */
static void print_value(Value *v) {
    switch (v->type) {
        case TY_INT:  printf("%lld", (long long)v->as.l); break;
        case TY_LONG: printf("%lld", (long long)v->as.l); break;
        case TY_FLOAT: printf("%g", v->as.f); break;
        case TY_BOOL: printf("%s", v->as.b ? "true" : "false"); break;
        case TY_CHAR: printf("'%c'", v->as.c); break;
        default: printf("<?>"); break;
    }
}

/* ------------------------
   Loader (same binary format as module_serialize_binary)
   ------------------------ */
static int load_module_from_bin(const char *path, Module *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    char magic[5] = {0};
    if (fread(magic, 1, 4, f) != 4) { fclose(f); return 0; }
    if (strncmp(magic, "TEO2", 4) != 0) { fclose(f); return 0; }

    int funcs = 0;
    fread(&funcs, sizeof(int), 1, f);
    module_init(out);

    for (int i = 0; i < funcs; i++) {
        int nlen = 0;
        fread(&nlen, sizeof(int), 1, f);

        char *name = NULL;
        if (nlen > 0) {
            name = malloc(nlen + 1);
            fread(name, 1, nlen, f);
            name[nlen] = '\0';
        }

        Function fn;
        function_init(&fn, name ? name : "anon");
        if (name) free(name);

        fread(&fn.locals, sizeof(int), 1, f);

        int const_count = 0;
        fread(&const_count, sizeof(int), 1, f);

        for (int c = 0; c < const_count; c++) {
            int t;
            fread(&t, sizeof(int), 1, f);
            Value v;
            v.type = (ValueType)t;

            if (v.type == TY_INT || v.type == TY_LONG) {
                fread(&v.as.l, sizeof(int64_t), 1, f);
            } else if (v.type == TY_FLOAT) {
                fread(&v.as.f, sizeof(double), 1, f);
            } else if (v.type == TY_BOOL) {
                int b=0; fread(&b, sizeof(int), 1, f);
                v.as.b = b ? 1 : 0;
                v.as.l = v.as.b;
            } else if (v.type == TY_CHAR) {
                fread(&v.as.c, sizeof(char), 1, f);
            } else {
                v.as.l = 0;
            }

            function_add_const(&fn, v);
        }

        int codecount = 0;
        fread(&codecount, sizeof(int), 1, f);

        fn.chunk.count = codecount;
        fn.chunk.capacity = codecount ? codecount : 1;
        fn.chunk.code = malloc(sizeof(int) * fn.chunk.capacity);

        fread(fn.chunk.code, sizeof(int), codecount, f);

        module_add_function(out, fn);
    }

    fclose(f);
    return 1;
}

/* ------------------------
   VM runner: ejecuta módulo con call frames
   ------------------------ */
int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Uso: %s program.bin\n", argv[0]);
        return 1;
    }

    Module mod;
    if (!load_module_from_bin(argv[1], &mod)) {
        fprintf(stderr, "❌ Error leyendo %s\n", argv[1]);
        return 1;
    }

    // Encuentra main
    int main_idx = -1;
    for (int i = 0; i < mod.count; ++i) {
        if (mod.funcs[i].name && strcmp(mod.funcs[i].name, "main") == 0) {
            main_idx = i; break;
        }
    }
    if (main_idx < 0) main_idx = mod.count - 1; // fallback

    printf("Loaded module: funcs=%d. Running function index=%d ('%s')\n",
           mod.count, main_idx, mod.funcs[main_idx].name ? mod.funcs[main_idx].name : "<anon>");

    ValStack vstack; vs_init(&vstack);
    FrameStack fstack; fs_init(&fstack);

    // push initial frame for main
    CallFrame *start = fs_push(&fstack);
    start->fn = &mod.funcs[main_idx];
    start->ip = 0;
    start->locals_count = start->fn->locals;
    if (start->locals_count > 0) {
        start->locals = calloc(start->locals_count, sizeof(int));
    } else start->locals = NULL;

    int running = 1;
    while (running && fstack.sp >= 0) {
        CallFrame *cf = &fstack.frames[fstack.sp];
        Function *fn = cf->fn;

        if (cf->ip >= fn->chunk.count) {
            // if we fall off the end of a function, treat as implicit RET (0)
            Value retv; retv.type = TY_INT; retv.as.l = 0;
            // pop current frame
            fs_pop(&fstack);
            if (fstack.sp < 0) {
                // no caller -> finish
                running = 0;
                vs_push(&vstack, retv);
                break;
            } else {
                vs_push(&vstack, retv);
                continue;
            }
        }

        int cur_ip = cf->ip;
        OpCode op = (OpCode)fn->chunk.code[cf->ip++];

        // debug print
        printf("[F=%s IP=%04d] %s",
               fn->name ? fn->name : "<anon>",
               cur_ip, opname(op));

        // print immediate arg if applicable
        if (op == OP_CONST || op == OP_LOAD_LOCAL || op == OP_STORE_LOCAL ||
            op == OP_JMP || op == OP_JZ || op == OP_NEW_ARRAY || op == OP_CALL)
        {
            printf(" %d", fn->chunk.code[cf->ip]);
            if (op == OP_NEW_ARRAY) printf(" %d", fn->chunk.code[cf->ip+1]);
            if (op == OP_CALL) {
                // CALL uses two args in your codegen: fid, arity (emit_op_arg2)
                // but your codegen emits as two ints after opcode. We'll print both.
                printf(" %d", fn->chunk.code[cf->ip+1]);
            }
        }
        printf("\n");

        switch (op) {

            case OP_CONST: {
                int ci = fn->chunk.code[cf->ip++];
                Value v = fn->consts[ci];
                vs_push(&vstack, v);
                printf("  -> push const "); print_value(&v); printf("\n");
            } break;

            case OP_LOAD_LOCAL: {
                int idx = fn->chunk.code[cf->ip++];
                Value v; v.type = TY_INT; v.as.l = (idx < cf->locals_count && cf->locals) ? cf->locals[idx] : 0;
                vs_push(&vstack, v);
                printf("  -> load local[%d] = %d\n", idx, (int)v.as.l);
            } break;

            case OP_STORE_LOCAL: {
                int idx = fn->chunk.code[cf->ip++];
                Value val = vs_pop(&vstack);
                if (idx < cf->locals_count && cf->locals) cf->locals[idx] = (int)val.as.l;
                printf("  -> store local[%d] = %lld\n", idx, (long long)val.as.l);
            } break;

            case OP_ADD: {
                Value b = vs_pop(&vstack), a = vs_pop(&vstack);
                Value r; r.type = TY_INT; r.as.l = a.as.l + b.as.l;
                vs_push(&vstack, r);
                printf("  -> add %lld + %lld = %lld\n", (long long)a.as.l, (long long)b.as.l, (long long)r.as.l);
            } break;

            case OP_SUB: {
                Value b = vs_pop(&vstack), a = vs_pop(&vstack);
                Value r; r.type = TY_INT; r.as.l = a.as.l - b.as.l;
                vs_push(&vstack, r);
                printf("  -> sub %lld - %lld = %lld\n", (long long)a.as.l, (long long)b.as.l, (long long)r.as.l);
            } break;

            case OP_MUL: {
                Value b = vs_pop(&vstack), a = vs_pop(&vstack);
                Value r; r.type = TY_INT; r.as.l = a.as.l * b.as.l;
                vs_push(&vstack, r);
                printf("  -> mul\n");
            } break;

            case OP_DIV: {
                Value b = vs_pop(&vstack), a = vs_pop(&vstack);
                Value r; r.type = TY_INT; r.as.l = (b.as.l == 0 ? 0 : a.as.l / b.as.l);
                vs_push(&vstack, r);
                printf("  -> div\n");
            } break;

            case OP_EQ: case OP_NEQ: case OP_LT: case OP_GT: case OP_LE: case OP_GE: {
                Value b = vs_pop(&vstack), a = vs_pop(&vstack);
                Value r; r.type = TY_BOOL;
                if (op == OP_EQ) r.as.b = (a.as.l == b.as.l);
                if (op == OP_NEQ) r.as.b = (a.as.l != b.as.l);
                if (op == OP_LT) r.as.b = (a.as.l < b.as.l);
                if (op == OP_GT) r.as.b = (a.as.l > b.as.l);
                if (op == OP_LE) r.as.b = (a.as.l <= b.as.l);
                if (op == OP_GE) r.as.b = (a.as.l >= b.as.l);
                r.as.l = r.as.b;
                vs_push(&vstack, r);
                printf("  -> compare result = %d\n", r.as.b);
            } break;

            case OP_AND: case OP_OR: {
                Value b = vs_pop(&vstack), a = vs_pop(&vstack);
                Value r; r.type = TY_BOOL;
                if (op == OP_AND) r.as.b = (a.as.b && b.as.b);
                else r.as.b = (a.as.b || b.as.b);
                r.as.l = r.as.b;
                vs_push(&vstack, r);
                printf("  -> logic result = %d\n", r.as.b);
            } break;

            case OP_NOT: {
                Value a = vs_pop(&vstack);
                Value r; r.type = TY_BOOL; r.as.b = !a.as.b; r.as.l = r.as.b;
                vs_push(&vstack, r);
                printf("  -> not = %d\n", r.as.b);
            } break;

            case OP_JMP: {
                int tgt = fn->chunk.code[cf->ip++];
                printf("  -> jump %d\n", tgt);
                cf->ip = tgt;
            } break;

            case OP_JZ: {
                int tgt = fn->chunk.code[cf->ip++];
                Value cond = vs_pop(&vstack);
                printf("  -> jz target=%d cond=", tgt); print_value(&cond); printf("\n");
                int is_zero = 0;
                if (cond.type == TY_BOOL) is_zero = (cond.as.b == 0);
                else if (cond.type == TY_INT || cond.type == TY_LONG) is_zero = (cond.as.l == 0);
                else if (cond.type == TY_FLOAT) is_zero = (cond.as.f == 0.0);
                else is_zero = (cond.as.l == 0);
                if (is_zero) cf->ip = tgt;
            } break;

            case OP_PRINT: {
                Value v = vs_pop(&vstack);
                printf("  -> PRINT "); print_value(&v); printf("\n");
            } break;

            case OP_POP: {
                Value v = vs_pop(&vstack);
                printf("  -> POP "); print_value(&v); printf("\n");
            } break;

            case OP_NEW_ARRAY: {
                int len = fn->chunk.code[cf->ip++];
                int et  = fn->chunk.code[cf->ip++];
                Value h; h.type = TY_INT; h.as.l = 0;
                vs_push(&vstack, h);
                printf("  -> NEW_ARRAY %d type=%d\n", len, et);
            } break;

            case OP_LOAD_INDEX:
            case OP_STORE_INDEX:
                printf("  -> ARRAY INDEX (debug simplified)\n");
                break;

            case OP_CALL: {
                int fid = fn->chunk.code[cf->ip++];
                int arity = fn->chunk.code[cf->ip++];

                printf("  -> CALL func=%d arity=%d\n", fid, arity);

                // Pop arguments in order: last arg pushed is top -> we pop in reverse to place into callee locals
                Value *args = NULL;
                if (arity > 0) {
                    args = malloc(sizeof(Value) * arity);
                    for (int i = arity - 1; i >= 0; --i) {
                        args[i] = vs_pop(&vstack);
                    }
                }

                if (fid < 0 || fid >= mod.count) {
                    // unresolved: behave like debugger earlier: return 0
                    printf("    -> unresolved function id %d: returning 0\n", fid);
                    Value rv; rv.type = TY_INT; rv.as.l = 0;
                    vs_push(&vstack, rv);
                    if (args) free(args);
                    break;
                }

                // push new frame
                CallFrame *newf = fs_push(&fstack);
                newf->fn = &mod.funcs[fid];
                newf->ip = 0;
                newf->locals_count = newf->fn->locals;
                if (newf->locals_count > 0) {
                    newf->locals = calloc(newf->locals_count, sizeof(int));
                } else newf->locals = NULL;

                // place args into locals 0..arity-1 (convention: parameters are first locals)
                for (int i = 0; i < arity && i < newf->locals_count; ++i) {
                    newf->locals[i] = (int)args[i].as.l;
                }
                if (args) free(args);

                // continue; execution will continue at new frame
                break;
            }

            case OP_RET: {
                // Pop return value (if any)
                Value rv = vs_pop(&vstack);
                printf("  -> RET (return value = "); print_value(&rv); printf(")\n");

                // pop current frame
                fs_pop(&fstack);

                if (fstack.sp < 0) {
                    // no caller, stop running (but push return value for caller inspection)
                    vs_push(&vstack, rv);
                    running = 0;
                    break;
                } else {
                    // push return value to caller stack
                    vs_push(&vstack, rv);
                    // continue execution at caller frame (its ip preserved)
                    break;
                }
            } break;

            case OP_HALT: {
                printf("  -> HALT\n");
                running = 0;
            } break;

            /* Robot commands (no-op but print) */
            case OP_AVANZAR: printf("  -> comando AVANZAR (no-op)\n"); break;
            case OP_RETROCEDER: printf("  -> comando RETROCEDER (no-op)\n"); break;
            case OP_GIRAR_DER: printf("  -> comando GIRAR_DER (no-op)\n"); break;
            case OP_GIRAR_IZQ: printf("  -> comando GIRAR_IZQ (no-op)\n"); break;
            case OP_DETENER: printf("  -> comando DETENER (no-op)\n"); break;
            case OP_INICIAR_LOOP: printf("  -> comando INICIAR_LOOP (no-op)\n"); break;
            case OP_SENSOR_DER: printf("  -> comando SENSOR_DER (no-op)\n"); break;
            case OP_SENSOR_IZQ: printf("  -> comando SENSOR_IZQ (no-op)\n"); break;
            case OP_AVANZAR_INDEF: printf("  -> comando AVANZAR_INDEF (no-op)\n"); break;

            default: {
                printf("  -> UNKNOWN OPCODE %d\n", (int)op);
                running = 0;
            } break;
        } // switch

        // print stack and locals of current frame (if still exists)
        printf("  STACK [");
        for (int i = 0; i <= vstack.sp; ++i) {
            print_value(&vstack.data[i]);
            if (i < vstack.sp) printf(", ");
        }
        printf("]\n");

        // print frames and their locals
        printf("  FRAMES (top=%d):\n", fstack.sp);
        for (int fi = 0; fi <= fstack.sp; ++fi) {
            CallFrame *fif = &fstack.frames[fi];
            printf("    #%d %s (ip=%d) LOCALS [", fi, fif->fn->name ? fif->fn->name : "<anon>", fif->ip);
            for (int li = 0; li < fif->locals_count; ++li) {
                printf("%d", fif->locals ? fif->locals[li] : 0);
                if (li+1 < fif->locals_count) printf(", ");
            }
            printf("]\n");
        }
        printf("\n");

    } // while running

    // final cleanup and summary
    printf("VM finished. Final stack top:\n  [");
    for (int i = 0; i <= vstack.sp; ++i) {
        print_value(&vstack.data[i]);
        if (i < vstack.sp) printf(", ");
    }
    printf("]\n");

    vs_free(&vstack);
    fs_free(&fstack);
    module_free(&mod);

    return 0;
}
