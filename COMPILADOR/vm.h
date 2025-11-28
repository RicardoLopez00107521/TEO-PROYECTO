#ifndef VM_H
#define VM_H

#include "ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------
   Arrays / Heap
   ----------------------------- */

typedef struct {
    int len;
    ValueType elem_type;
    Value *data;
} Array;

typedef struct {
    Array *arrays;
    int count;
    int capacity;
} Heap;

/* -----------------------------
   Call Frame (stack of frames)
   ----------------------------- */

typedef struct {
    Function *fn;    // pointer into Module.funcs
    int ip;          // instruction pointer within fn->chunk.code
    Value *locals;   // array of local slots (Value)
    int locals_count;
    int return_ip;   // ip in caller function AFTER call (for simple model)
    Function *caller_fn; // pointer to caller function (NULL for bottom)
} Frame;

/* -----------------------------
   VM structure
   ----------------------------- */

typedef struct {
    Value *stack;
    int sp;
    int stack_cap;

    Frame *frames;
    int frame_count;
    int frame_cap;

    Heap heap;
    Module *module; // current module running
} VM;

/* API pública */
void vm_init(VM *vm);
void vm_free(VM *vm);
int vm_run(VM *vm, Module *mod);

/* Extra opcional: correr desde binario "program.bin" */
int vm_run_binary_file(VM *vm, const char *path);
void vm_dump_module(Module *mod, const char *filename);
void vm_export_arduino_format(Module *mod, const char *filename);
void vm_export_tinyvm(Module *mod, const char *filename);

#endif /* VM_H */
