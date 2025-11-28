#ifndef IR_H
#define IR_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* -------------------------
   Tipos de valores
   ------------------------- */
typedef enum {
    TY_INT = 1,
    TY_FLOAT,
    TY_LONG,
    TY_CHAR,
    TY_BOOL,
    TY_UNKNOWN
} ValueType;

typedef struct {
    ValueType type;
    union {
        int64_t l;
        int32_t i;
        double f;
        char c;
        int b;
    } as;
} Value;

/* -------------------------
   Opcodes (stack-based)
   Cada instrucción es opcode seguido de 0..n argumentos enteros.
   ------------------------- */
typedef enum {
    OP_NOP = 0,
    OP_CONST,        // arg: const_index
    OP_LOAD_LOCAL,   // arg: local_index
    OP_STORE_LOCAL,  // arg: local_index
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_EQ,
    OP_NEQ,
    OP_LT,
    OP_GT,
    OP_LE,
    OP_GE,
    OP_AND,
    OP_OR,
    OP_NOT,
    OP_JMP,          // arg: target_ip
    OP_JZ,           // arg: target_ip (if top == 0)
    OP_PRINT,        // pop and print
    OP_POP,
    OP_RET,
    OP_HALT,
    // Comandos del robot (no hacen nada en VM por ahora)
    OP_AVANZAR,
    OP_RETROCEDER,
    OP_GIRAR_DER,
    OP_GIRAR_IZQ,
    OP_DETENER,
    OP_INICIAR_LOOP,
    OP_SENSOR_DER,
    OP_SENSOR_IZQ,
    OP_AVANZAR_INDEF,
    OP_FAST,
    OP_SLOW,
    OP_IGLOBAL,
    OP_FGLOBAL,
    OP_DELAY,
    // extensiones (por si las necesitas)
    OP_NEW_ARRAY,    // args: length, elem_type
    OP_LOAD_INDEX,   // args: (handled on stack)
    OP_STORE_INDEX,  // args: (handled on stack)
    OP_CALL         // args: func_index, arity
} OpCode;

/* -------------------------
   Chunk: secuencia de enteros que representan opcodes y args
   ------------------------- */
typedef struct {
    int *code;
    int count;
    int capacity;
} Chunk;

/* -------------------------
   Function: nombre, chunk, constantes, locals
   ------------------------- */
typedef struct {
    char *name;       // dinamically allocated
    int arity;
    int locals;       // número de slots locales (incluye params)
    Chunk chunk;
    Value *consts;
    int const_count;
    int const_cap;
} Function;

/* -------------------------
   Module: array de funciones
   ------------------------- */
typedef struct {
    Function *funcs;
    int count;
    int capacity;
} Module;

/* -------------------------
   Prototipos públicos
   ------------------------- */

/* Chunk */
void chunk_init(Chunk *c);
void chunk_free(Chunk *c);
int chunk_emit(Chunk *c, int v);
int chunk_emit_op_arg(Chunk *c, int op, int arg);

/* Function */
void function_init(Function *f, const char *name);
void function_free(Function *f);
int function_add_const(Function *f, Value v);

/* Module */
void module_init(Module *m);
int module_add_function(Module *m, Function f); /* copia por valor */
void module_free(Module *m);

/* Dump / Serialización */
void dump_module(Module *m, FILE *out);
int module_serialize_binary(Module *m, FILE *out); /* escribe formato TEO2, retorna 1 en OK */

#endif /* IR_H */
