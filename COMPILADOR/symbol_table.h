// symbol_table.h
#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <stdio.h>
#include <stdlib.h>

typedef enum {
    SYM_NUM,
    SYM_FRAC,
    SYM_BOOL,
    SYM_CHAR,
    SYM_UNKNOWN
} SymType;

typedef enum {
    SYM_KIND_VAR,
    SYM_KIND_FUNC
} SymKind;

typedef struct Symbol {
    char *name;
    SymKind kind;

    // for variables
    SymType type;
    int is_array;
    int array_size; // -1 if unknown

    // for functions
    SymType return_type;
    int param_count;
    SymType *param_types; // dynamic array (NULL if none)

    int declared_line;
    int declared_col;
    struct Symbol *next; // chaining in bucket
} Symbol;

typedef struct Scope {
    Symbol **buckets; // array of bucket heads
    struct Scope *prev;
} Scope;

typedef struct {
    Scope *current;
    int bucket_count;
} SymTable;

// API
SymTable* symtable_create(int bucket_count);
void symtable_destroy(SymTable *st);

void symtable_enter_scope(SymTable *st);
void symtable_leave_scope(SymTable *st);

// returns 1 on success, 0 if already exists in current scope
int symtable_add_var(SymTable *st, const char *name, SymType type, int is_array, int array_size, int line, int col);

// function add: returns 1 on success, 0 if already exists in current scope
int symtable_add_func(SymTable *st, const char *name, SymType return_type, int param_count, SymType *param_types, int line, int col);

// lookup symbol across scopes (returns pointer or NULL)
Symbol* symtable_lookup(SymTable *st, const char *name);

#endif // SYMBOL_TABLE_H
