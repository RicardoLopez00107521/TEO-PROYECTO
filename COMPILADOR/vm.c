#include "vm.h"

/* ============================================================
   Heap helpers (arrays)
   ============================================================ */

static void heap_init(Heap *h) {
    h->arrays = NULL;
    h->count = 0;
    h->capacity = 0;
}

static void heap_free(Heap *h) {
    if (!h) return;
    for (int i = 0; i < h->count; ++i) {
        if (h->arrays[i].data) free(h->arrays[i].data);
    }
    free(h->arrays);
    h->arrays = NULL;
    h->count = h->capacity = 0;
}

/* create new array and return handle */
static int heap_new_array(Heap *h, int len, ValueType et) {
    if (len < 0) len = 0;
    if (h->count + 1 > h->capacity) {
        h->capacity = h->capacity ? h->capacity * 2 : 8;
        h->arrays = (Array*)realloc(h->arrays, sizeof(Array) * h->capacity);
    }
    Array *A = &h->arrays[h->count];
    A->len = len;
    A->elem_type = et;
    A->data = (Value*)calloc(len > 0 ? len : 1, sizeof(Value));
    for (int i = 0; i < len; ++i) {
        A->data[i].type = et;
        A->data[i].as.l = 0;
    }
    return h->count++;
}

static Array* heap_get(Heap *h, int handle) {
    if (handle < 0 || handle >= h->count) return NULL;
    return &h->arrays[handle];
}

/* ============================================================
   Stack helpers
   ============================================================ */

static void ensure_stack(VM *vm, int need) {
    if (vm->sp + need < vm->stack_cap) return;
    int newcap = vm->stack_cap ? vm->stack_cap * 2 : 64;
    while (vm->sp + need >= newcap) newcap *= 2;
    vm->stack = (Value*)realloc(vm->stack, sizeof(Value) * newcap);
    vm->stack_cap = newcap;
}

static void push(VM *vm, Value v) {
    ensure_stack(vm, 1);
    vm->stack[++vm->sp] = v;
}

static Value popv(VM *vm) {
    if (vm->sp < 0) {
        Value z; z.type = TY_INT; z.as.l = 0; return z;
    }
    return vm->stack[vm->sp--];
}

static Value peek(VM *vm) {
    if (vm->sp < 0) { Value z; z.type = TY_INT; z.as.l = 0; return z; }
    return vm->stack[vm->sp];
}

/* ============================================================
   Frame helpers
   ============================================================ */

static void frames_init(VM *vm) {
    vm->frames = NULL;
    vm->frame_count = 0;
    vm->frame_cap = 0;
}

static void frames_free(VM *vm) {
    if (!vm->frames) return;
    for (int i = 0; i < vm->frame_count; ++i) {
        if (vm->frames[i].locals) free(vm->frames[i].locals);
    }
    free(vm->frames);
    vm->frames = NULL;
    vm->frame_count = vm->frame_cap = 0;
}

static void frames_ensure(VM *vm) {
    if (vm->frame_count + 1 <= vm->frame_cap) return;
    int newcap = vm->frame_cap ? vm->frame_cap * 2 : 16;
    vm->frames = (Frame*)realloc(vm->frames, sizeof(Frame) * newcap);
    vm->frame_cap = newcap;
}

/* push new frame; returns pointer to new frame */
static Frame* push_frame(VM *vm, Function *fn, Function *caller_fn, int return_ip) {
    frames_ensure(vm);
    Frame *fr = &vm->frames[vm->frame_count++];
    fr->fn = fn;
    fr->ip = 0;
    fr->return_ip = return_ip;
    fr->caller_fn = caller_fn;
    fr->locals_count = fn->locals;
    fr->locals = NULL;
    if (fr->locals_count > 0) {
        fr->locals = (Value*)calloc(fr->locals_count, sizeof(Value));
        for (int i = 0; i < fr->locals_count; ++i) {
            fr->locals[i].type = TY_INT;
            fr->locals[i].as.l = 0;
        }
    }
    return fr;
}

/* pop current frame and free locals; return caller frame pointer (or NULL) */
static Frame* pop_frame(VM *vm) {
    if (vm->frame_count <= 0) return NULL;
    Frame *cur = &vm->frames[vm->frame_count - 1];
    if (cur->locals) { free(cur->locals); cur->locals = NULL; }
    vm->frame_count--;
    if (vm->frame_count == 0) return NULL;
    return &vm->frames[vm->frame_count - 1];
}

/* ============================================================
   VM init / free
   ============================================================ */

void vm_init(VM *vm) {
    vm->stack = NULL;
    vm->sp = -1;
    vm->stack_cap = 0;
    frames_init(vm);
    heap_init(&vm->heap);
    vm->module = NULL;
}

void vm_free(VM *vm) {
    if (!vm) return;
    if (vm->stack) free(vm->stack);
    frames_free(vm);
    heap_free(&vm->heap);
    vm->stack = NULL;
    vm->sp = -1;
    vm->stack_cap = 0;
    vm->module = NULL;
}

/* ============================================================
   Core execution
   - We assume module->funcs[0] is "main" entrypoint.
   - CALL pops 'arity' args (pushed left->right), places them in callee locals[0..arity-1].
   - Callee returns by executing OP_RET; it should push 1 return value (convention).
   - If callee returns without pushing, VM will push int 0 for safety.
   ============================================================ */

int vm_run(VM *vm, Module *mod) {
    if (!vm || !mod || mod->count <= 0) return 0;

    vm->module = mod;

    // prepare initial frame for function 0 (main)
    // === FIX: seleccionar correctamente la función main ===
    int main_index = -1;

    // Buscar función llamada "main"
    for (int i = 0; i < mod->count; i++) {
        if (mod->funcs[i].name && strcmp(mod->funcs[i].name, "main") == 0) {
            main_index = i;
            break;
        }
    }

    // Si no hay función llamada main, usar la última (fallback del codegen)
    if (main_index < 0)
        main_index = mod->count - 1;

    Function *mainf = &mod->funcs[main_index];
    push_frame(vm, mainf, NULL, -1);


    int running = 1;

    while (running && vm->frame_count > 0) {
        Frame *cur = &vm->frames[vm->frame_count - 1];
        Function *fn = cur->fn;
        int *code = fn->chunk.code;
        int ip = cur->ip;

        if (ip >= fn->chunk.count) {
            // No explicit return: treat as RET with 0
            // push default 0
            Value v; v.type = TY_INT; v.as.l = 0;
            push(vm, v);
            // simulate OP_RET behavior below
            // set cur->ip to end so it will be popped
            cur->ip = fn->chunk.count;
            ip = cur->ip;
        }

        if (cur->ip >= fn->chunk.count) {
            // perform implicit return
            // pop frame but leave return value on stack
            Frame saved = *cur;
            // pop current frame
            pop_frame(vm);
            if (vm->frame_count == 0) {
                // returned from main -> stop
                running = 0;
                break;
            } else {
                // continue loop so next iteration uses caller frame
                continue;
            }
        }

        OpCode op = (OpCode)code[ip++];
        cur->ip = ip; // store updated ip

        switch (op) {
            case OP_CONST: {
                int ci = code[cur->ip++];
                cur->ip = cur->ip;
                if (ci < 0 || ci >= fn->const_count) {
                    Value z; z.type = TY_INT; z.as.l = 0; push(vm, z);
                } else push(vm, fn->consts[ci]);
            } break;

            case OP_LOAD_LOCAL: {
                int idx = code[cur->ip++];
                if (idx < 0 || idx >= cur->locals_count) {
                    Value z; z.type = TY_INT; z.as.l = 0; push(vm, z);
                } else push(vm, cur->locals[idx]);
            } break;

            case OP_STORE_LOCAL: {
                int idx = code[cur->ip++];
                Value v = popv(vm);
                if (idx >= 0 && idx < cur->locals_count) {
                    cur->locals[idx] = v;
                } // else ignore silently
            } break;

            case OP_ADD: {
                Value b = popv(vm); Value a = popv(vm);
                Value r; r.type = TY_INT; r.as.l = a.as.l + b.as.l; push(vm, r);
            } break;

            case OP_SUB: {
                Value b = popv(vm); Value a = popv(vm);
                Value r; r.type = TY_INT; r.as.l = a.as.l - b.as.l; push(vm, r);
            } break;

            case OP_MUL: {
                Value b = popv(vm); Value a = popv(vm);
                Value r; r.type = TY_INT; r.as.l = a.as.l * b.as.l; push(vm, r);
            } break;

            case OP_DIV: {
                Value b = popv(vm); Value a = popv(vm);
                Value r; r.type = TY_INT; r.as.l = (b.as.l == 0 ? 0 : a.as.l / b.as.l); push(vm, r);
            } break;

            case OP_MOD: {
                Value b = popv(vm); Value a = popv(vm);
                Value r; r.type = TY_INT; r.as.l = a.as.l % b.as.l; push(vm, r);
            } break;

            case OP_EQ: {
                Value b = popv(vm); Value a = popv(vm);
                Value r; r.type = TY_BOOL; r.as.b = (a.as.l == b.as.l); push(vm, r);
            } break;

            case OP_NEQ: {
                Value b = popv(vm); Value a = popv(vm);
                Value r; r.type = TY_BOOL; r.as.b = (a.as.l != b.as.l); push(vm, r);
            } break;

            case OP_LT: {
                Value b = popv(vm); Value a = popv(vm);
                Value r; r.type = TY_BOOL; r.as.b = (a.as.l < b.as.l); push(vm, r);
            } break;

            case OP_GT: {
                Value b = popv(vm); Value a = popv(vm);
                Value r; r.type = TY_BOOL; r.as.b = (a.as.l > b.as.l); push(vm, r);
            } break;

            case OP_LE: {
                Value b = popv(vm); Value a = popv(vm);
                Value r; r.type = TY_BOOL; r.as.b = (a.as.l <= b.as.l); push(vm, r);
            } break;

            case OP_GE: {
                Value b = popv(vm); Value a = popv(vm);
                Value r; r.type = TY_BOOL; r.as.b = (a.as.l >= b.as.l); push(vm, r);
            } break;

            case OP_AND: {
                Value b = popv(vm); Value a = popv(vm);
                Value r; r.type = TY_BOOL; r.as.b = (a.as.b && b.as.b); push(vm, r);
            } break;

            case OP_OR: {
                Value b = popv(vm); Value a = popv(vm);
                Value r; r.type = TY_BOOL; r.as.b = (a.as.b || b.as.b); push(vm, r);
            } break;

            case OP_NOT: {
                Value a = popv(vm);
                Value r; r.type = TY_BOOL; r.as.b = (!a.as.b); push(vm, r);
            } break;

            case OP_JMP: {
                int tgt = code[cur->ip++];
                if (tgt >= 0 && tgt < fn->chunk.count) cur->ip = tgt;
            } break;

            case OP_JZ: {
                int tgt = code[cur->ip++];
                Value v = popv(vm);
                if (v.as.l == 0) {
                    if (tgt >= 0 && tgt < fn->chunk.count) cur->ip = tgt;
                }
            } break;

            case OP_AVANZAR: {
                /* no-op por ahora (comando reconocido pero sin efecto) */
            } break;

            case OP_RETROCEDER: {
                /* no-op por ahora */
            } break;

            case OP_GIRAR_DER: {
                /* no-op por ahora */
            } break;

            case OP_GIRAR_IZQ: {
                /* no-op por ahora */
            } break;

            case OP_DETENER: {
                /* no-op por ahora */
            } break;

            case OP_DELAY: {
                /* no-op por ahora */
            } break;

            case OP_INICIAR_LOOP: {
                /* no-op por ahora */
            } break;

            case OP_FAST: {
                /* no-op por ahora */
            } break;

            case OP_SLOW: {
                /* no-op por ahora */
            } break;

            case OP_SENSOR_DER: {
                /* no-op por ahora */
            } break;

            case OP_SENSOR_IZQ: {
                /* no-op por ahora */
            } break;

            case OP_AVANZAR_INDEF: {
                /* no-op por ahora */
            } break;

            case OP_IGLOBAL: {
                /* no-op por ahora */
            } break;

            case OP_FGLOBAL: {
                /* no-op por ahora */
            } break;

            case OP_PRINT: {
                Value v = popv(vm);
                switch (v.type) {
                    case TY_INT:  printf("%lld\n", (long long)v.as.l); break;
                    case TY_FLOAT:printf("%g\n", v.as.f); break;
                    case TY_BOOL: printf("%s\n", v.as.b ? "true" : "false"); break;
                    case TY_CHAR: printf("%c\n", v.as.c); break;
                    default: printf("<?>\n"); break;
                }
            } break;

            case OP_POP: {
                (void)popv(vm);
            } break;

            case OP_NEW_ARRAY: {
                int len = code[cur->ip++]; int et = code[cur->ip++];
                int handle = heap_new_array(&vm->heap, len, (ValueType)et);
                Value v; v.type = TY_INT; v.as.l = handle; push(vm, v);
            } break;

            case OP_LOAD_INDEX: {
                Value idx = popv(vm);
                Value arr = popv(vm);
                Array *A = heap_get(&vm->heap, (int)arr.as.l);
                if (!A || idx.as.l < 0 || idx.as.l >= A->len) {
                    Value z; z.type = TY_INT; z.as.l = 0; push(vm, z);
                } else {
                    push(vm, A->data[idx.as.l]);
                }
            } break;

            case OP_STORE_INDEX: {
                Value val = popv(vm);
                Value idx = popv(vm);
                Value arr = popv(vm);
                Array *A = heap_get(&vm->heap, (int)arr.as.l);
                if (A && idx.as.l >= 0 && idx.as.l < A->len) {
                    A->data[idx.as.l] = val;
                }
            } break;

            case OP_CALL: {
                // layout: OP_CALL funcIndex arity
                int fid = code[cur->ip++];
                int ar = code[cur->ip++];
                if (fid < 0 || fid >= vm->module->count) {
                    // invalid func -> push 0 as return
                    Value z; z.type = TY_INT; z.as.l = 0; push(vm, z);
                    break;
                }
                Function *callee = &vm->module->funcs[fid];

                // collect arguments (pushed left->right). We pop in reverse
                Value *args = NULL;
                if (ar > 0) {
                    args = (Value*)malloc(sizeof(Value) * ar);
                    for (int i = ar - 1; i >= 0; --i) {
                        args[i] = popv(vm);
                    }
                }

                // save caller frame state: current ip already points after the arity
                int return_ip = cur->ip;
                Function *caller_fn = fn;

                // push callee frame
                Frame *newf = push_frame(vm, callee, caller_fn, return_ip);

                // set callee locals[0..ar-1] = args
                for (int i = 0; i < ar && i < newf->locals_count; ++i) {
                    newf->locals[i] = args[i];
                }
                if (args) free(args);

                // execution continues with callee in next loop iteration
            } break;

            case OP_RET: {
                // callee should push return value before OP_RET (convention).
                Value ret = popv(vm);
                // pop current frame
                Function *just_ret_from = cur->fn;
                pop_frame(vm);
                // if returned from top-level (no caller), place return and stop
                if (vm->frame_count == 0) {
                    push(vm, ret);
                    running = 0;
                    break;
                }
                // else push return onto stack for caller
                push(vm, ret);
                // continue execution in caller: caller frame ip is left as it was (we stored return_ip earlier, but we use the caller's ip)
                // The caller frame is now current; nothing else to do here.
            } break;

            case OP_HALT: {
                running = 0;
            } break;

            default: {
                printf("VM ERROR: unknown opcode %d\n", (int)op);
                running = 0;
            } break;
        } // switch

        // update current ip (already stored in cur->ip used by next iteration)
    } // while

    return 1;
}

/* ============================================================
   Run binary loader (same format TEO2)
   ============================================================ */

int vm_run_binary_file(VM *vm, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    char magic[4];
    if (fread(magic, 1, 4, f) != 4) { fclose(f); return 0; }
    if (memcmp(magic, "TEO2", 4) != 0) { fclose(f); return 0; }

    int func_count = 0;
    fread(&func_count, sizeof(int), 1, f);

    Module mod;
    module_init(&mod);

    for (int i = 0; i < func_count; ++i) {
        int nlen = 0;
        fread(&nlen, sizeof(int), 1, f);
        char *name = NULL;
        if (nlen > 0) {
            name = (char*)malloc(nlen + 1);
            fread(name, 1, nlen, f);
            name[nlen] = '\0';
        }
        Function fn;
        function_init(&fn, name ? name : NULL);
        if (name) free(name);

        fread(&fn.locals, sizeof(int), 1, f);
        int const_count = 0;
        fread(&const_count, sizeof(int), 1, f);
        for (int c = 0; c < const_count; ++c) {
            int t;
            fread(&t, sizeof(int), 1, f);
            Value v; v.type = (ValueType)t;
            switch (v.type) {
                case TY_INT:
                case TY_LONG:
                    fread(&v.as.l, sizeof(int64_t), 1, f);
                    break;
                case TY_FLOAT:
                    fread(&v.as.f, sizeof(double), 1, f);
                    break;
                case TY_BOOL:
                    fread(&v.as.b, sizeof(int), 1, f);
                    break;
                case TY_CHAR:
                    fread(&v.as.c, sizeof(char), 1, f);
                    break;
                default:
                    v.type = TY_INT; v.as.l = 0;
                    break;
            }
            function_add_const(&fn, v);
        }

        int code_count = 0;
        fread(&code_count, sizeof(int), 1, f);
        fn.chunk.count = code_count;
        fn.chunk.capacity = code_count > 0 ? code_count : 0;
        if (code_count > 0) {
            fn.chunk.code = (int*)malloc(sizeof(int) * code_count);
            fread(fn.chunk.code, sizeof(int), code_count, f);
        } else {
            fn.chunk.code = NULL;
        }

        module_add_function(&mod, fn);
    }

    fclose(f);

    int ok = vm_run(vm, &mod);
    module_free(&mod);
    return ok;
}

/* ============================================================
   Dump de instrucciones a TXT para usar en Arduino
   ============================================================ */

   static const char* opcode_to_string(int opcode) {
    switch (opcode) {
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
        case OP_CALL: return "CALL";
        case OP_RET: return "RET";
        case OP_HALT: return "HALT";

        /* tus opcodes personalizados */
        case OP_AVANZAR: return "AVANZAR";
        case OP_RETROCEDER: return "RETROCEDER";
        case OP_GIRAR_DER: return "GIRAR_DER";
        case OP_GIRAR_IZQ: return "GIRAR_IZQ";
        case OP_DETENER: return "DETENER";
        case OP_FAST: return "ACELERAR";
        case OP_SLOW: return "DESACELERAR";
        case OP_INICIAR_LOOP: return "INICIAR_LOOP";
        case OP_SENSOR_DER: return "SENSOR_DER";
        case OP_SENSOR_IZQ: return "SENSOR_IZQ";
        case OP_AVANZAR_INDEF: return "AVANZAR_INDEF";
        case OP_IGLOBAL: return "INIT_GLOBAL";
        case OP_FGLOBAL: return "END_GLOBAL";
        case OP_DELAY: return "DELAY";

        default: return "UNKNOWN";
    }
}

void vm_dump_module(Module *mod, const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        printf("Error: no se pudo abrir %s\n", filename);
        return;
    }

    fprintf(f, "FUNC\tINDEX\tMNEMONIC\tARG1\tARG2\n");

    for (int fidx = 0; fidx < mod->count; fidx++) {
        Function *fn = &mod->funcs[fidx];
        int *code = fn->chunk.code;
        int n = fn->chunk.count;

        for (int ip = 0; ip < n; ) {

            int index = ip;
            int op = code[ip++];
            const char *name = opcode_to_string(op);

            int a = -1;
            int b = -1;

            switch (op) {
                case OP_CONST:
                case OP_LOAD_LOCAL:
                case OP_STORE_LOCAL:
                case OP_JMP:
                case OP_JZ:
                case OP_PRINT:
                    a = code[ip++];
                    break;

                case OP_CALL:
                case OP_NEW_ARRAY:
                    a = code[ip++];
                    b = code[ip++];
                    break;

                default:
                    // no args
                    break;
            }

            fprintf(f, "%s\t%d\t%s\t%d\t%d\n",
                fn->name ? fn->name : "noname",
                index,
                name,
                a, b
            );
        }
    }

    fclose(f);
    printf("Instrucciones exportadas correctamente.\n");
}

/* ============================================================
   Exportar programa en FORMATO 1 para Arduino
   ============================================================ */

void vm_export_arduino_format(Module *mod, const char *filename) {
    if (!mod) {
        printf("vm_export_arduino_format: Module NULL\n");
        return;
    }

    FILE *f = fopen(filename, "w");
    if (!f) {
        printf("❌ No se pudo escribir %s\n", filename);
        return;
    }

    /* ============================================================
       1) SECTION_OPCODES (SIN HARDCODING)
       ============================================================ */

    fprintf(f, "SECTION_OPCODES\n");

    const char *opnames[] = {
        "CONST",
        "LOAD_LOCAL",
        "STORE_LOCAL",
        "LOAD_INDEX",
        "STORE_INDEX",
        "ADD",
        "SUB",
        "MUL",
        "DIV",
        "MOD",
        "EQ",
        "NEQ",
        "LT",
        "GT",
        "LE",
        "GE",
        "AND",
        "OR",
        "NOT",
        "JMP",
        "JZ",
        "CALL",
        "RET",
        "HALT",
        "PRINT",
        "POP",
        "NEW_ARRAY",
        "AVANZAR",
        "RETROCEDER",
        "GIRAR_DER",
        "GIRAR_IZQ",
        "DETENER",
        "DELAY",
        "ACELERAR",
        "DESACELERAR",
        "INICIAR_LOOP",
        "SENSOR_DER",
        "SENSOR_IZQ",
        "AVANZAR_INDEF",
        "INIT_GLOBAL",
        "END_GLOBAL"
    };

    int opcode_count = sizeof(opnames) / sizeof(opnames[0]);

    for (int i = 0; i < opcode_count; i++) {
        fprintf(f, "%s %d\n", opnames[i], i);
    }

    fprintf(f, "\nSECTION_CODE\n\n");

    /* ============================================================
       2) SECTION_CODE (por función)
       ============================================================ */

    for (int fi = 0; fi < mod->count; fi++) {
        Function *fn = &mod->funcs[fi];

        fprintf(f, "FUNC %s\n", fn->name ? fn->name : "noname");

        int *code = fn->chunk.code;
        int n = fn->chunk.count;
        int ip = 0;

        while (ip < n) {

            int op = code[ip++];
            const char *mn =
                (op >= 0 && op < opcode_count) ? opnames[op] : "UNKNOWN";

            int a = -9999;
            int b = -9999;

            switch (op) {
                case OP_CONST:
                case OP_LOAD_LOCAL:
                case OP_STORE_LOCAL:
                case OP_JMP:
                case OP_JZ:
                case OP_PRINT:
                    a = code[ip++];
                    break;

                case OP_CALL:
                case OP_NEW_ARRAY:
                    a = code[ip++];
                    b = code[ip++];
                    break;

                default:
                    // sin argumentos
                    break;
            }

            // Escribir instrucción en formato:
            // MNEMONIC
            // MNEMONIC arg1
            // MNEMONIC arg1 arg2

            fprintf(f, "%s", mn);

            if (a != -9999) fprintf(f, " %d", a);
            if (b != -9999) fprintf(f, " %d", b);

            fprintf(f, "\n");
        }

        fprintf(f, "\n");
    }

    fclose(f);
    printf("📦 Archivo Arduino exportado: %s\n", filename);
}

void vm_export_tinyvm(Module *mod, const char *filename) {
    if (!mod) {
        printf("vm_export_tinyvm: Module NULL\n");
        return;
    }

    /* ============================================================
       1) Encontrar función main
       ============================================================ */
    int main_index = -1;
    for (int i = 0; i < mod->count; i++) {
        if (mod->funcs[i].name && strcmp(mod->funcs[i].name, "main") == 0) {
            main_index = i;
            break;
        }
    }

    if (main_index < 0) {
        printf("❌ No se encontró main. Usando última.\n");
        main_index = mod->count - 1;
    }

    Function *fn = &mod->funcs[main_index];
    int *code = fn->chunk.code;
    int n = fn->chunk.count;

    /* ============================================================
       2) PRIMERA PASADA: mapear ip_real → numero_de_linea
       ============================================================ */

    int *ip_to_line = (int *)malloc(sizeof(int) * n);
    int line = 0;

    for (int ip = 0; ip < n; ) {

        ip_to_line[ip] = line++;

        int op = code[ip++];

        switch (op) {
            case OP_CONST:
            case OP_LOAD_LOCAL:
            case OP_STORE_LOCAL:
            case OP_JMP:
            case OP_JZ:
            case OP_PRINT:
                ip++;        // 1 argumento
                break;

            case OP_CALL:
            case OP_NEW_ARRAY:
                ip += 2;     // 2 argumentos
                break;

            default:
                break;       // sin argumentos
        }
    }

    /* ============================================================
       3) SEGUNDA PASADA: escribir TinyVM, recalculando saltos
       ============================================================ */

    FILE *f = fopen(filename, "w");
    if (!f) {
        printf("❌ No se pudo abrir %s\n", filename);
        free(ip_to_line);
        return;
    }

    fprintf(f, "# Programa TinyVM para carrito\n");
    fprintf(f, "# Formato: MNEMONIC arg1 arg2\n\n");

    for (int ip = 0; ip < n; ) {

        int op = code[ip++];
        const char *mn = opcode_to_string(op);

        int arg1 = 0;
        int arg2 = 0;

        switch (op) {

            case OP_CONST: {
                int ci = code[ip++];
                long long v = 0;
                if (ci >= 0 && ci < fn->const_count)
                    v = fn->consts[ci].as.l;
                arg1 = (int)v;
            } break;

            case OP_LOAD_LOCAL:
            case OP_STORE_LOCAL:
            case OP_PRINT:
                arg1 = code[ip++];
                break;

            case OP_JMP: {
                int tgt = code[ip++];
                if (tgt >= 0 && tgt < n)
                    arg1 = ip_to_line[tgt];   // <==== REEMPLAZO DE SALTO
                else
                    arg1 = 0;
            } break;

            case OP_JZ: {
                int tgt = code[ip++];
                if (tgt >= 0 && tgt < n)
                    arg1 = ip_to_line[tgt];   // <==== REEMPLAZO DE SALTO
                else
                    arg1 = 0;
            } break;

            case OP_CALL:
            case OP_NEW_ARRAY:
                arg1 = code[ip++];
                arg2 = code[ip++];
                break;

            default:
                break;
        }

        fprintf(f, "%s %d %d\n", mn, arg1, arg2);
    }

    fclose(f);
    free(ip_to_line);

    printf("📦 TinyVM exportado con saltos corregidos → %s\n", filename);
}

