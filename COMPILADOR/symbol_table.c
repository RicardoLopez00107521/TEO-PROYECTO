// symbol_table.c
#include "symbol_table.h"
#include <string.h>

static unsigned fnv1a_hash(const char *s) {
    unsigned h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)(*s++);
        h *= 16777619u;
    }
    return h;
}

SymTable* symtable_create(int bucket_count) {
    SymTable *st = (SymTable*)malloc(sizeof(SymTable));
    if (!st) return NULL;
    st->bucket_count = (bucket_count > 0) ? bucket_count : 503;
    st->current = (Scope*)malloc(sizeof(Scope));
    if (!st->current) { free(st); return NULL; }
    st->current->buckets = (Symbol**)calloc(st->bucket_count, sizeof(Symbol*));
    st->current->prev = NULL;
    return st;
}

static void free_symbol_list(Symbol *s) {
    while (s) {
        Symbol *n = s->next;
        if (s->name) free(s->name);
        if (s->param_types) free(s->param_types);
        free(s);
        s = n;
    }
}

void symtable_leave_scope(SymTable *st) {
    if (!st || !st->current) return;
    Scope *cur = st->current;
    for (int i = 0; i < st->bucket_count; ++i) {
        if (cur->buckets[i]) {
            free_symbol_list(cur->buckets[i]);
        }
    }
    free(cur->buckets);
    st->current = cur->prev;
    free(cur);
}

void symtable_enter_scope(SymTable *st) {
    if (!st) return;
    Scope *sc = (Scope*)malloc(sizeof(Scope));
    sc->buckets = (Symbol**)calloc(st->bucket_count, sizeof(Symbol*));
    sc->prev = st->current;
    st->current = sc;
}

void symtable_destroy(SymTable *st) {
    if (!st) return;
    while (st->current) {
        symtable_leave_scope(st);
    }
    free(st);
}

int symtable_add_var(SymTable *st, const char *name, SymType type, int is_array, int array_size, int line, int col) {
    if (!st || !st->current || !name) return 0;
    unsigned idx = fnv1a_hash(name) % st->bucket_count;
    Symbol *head = st->current->buckets[idx];
    // check duplicate in current scope
    for (Symbol *s = head; s; s = s->next) {
        if (strcmp(s->name, name) == 0) return 0; // already declared in current scope
    }
    Symbol *sym = (Symbol*)malloc(sizeof(Symbol));
    memset(sym,0,sizeof(Symbol));
    sym->name = strdup(name);
    sym->kind = SYM_KIND_VAR;
    sym->type = type;
    sym->is_array = is_array;
    sym->array_size = array_size;
    sym->param_count = 0;
    sym->param_types = NULL;
    sym->declared_line = line;
    sym->declared_col = col;
    sym->next = head;
    st->current->buckets[idx] = sym;
    return 1;
}

int symtable_add_func(SymTable *st, const char *name, SymType return_type, int param_count, SymType *param_types, int line, int col) {
    if (!st || !st->current || !name) return 0;
    unsigned idx = fnv1a_hash(name) % st->bucket_count;
    Symbol *head = st->current->buckets[idx];
    // check duplicate in current scope
    for (Symbol *s = head; s; s = s->next) {
        if (strcmp(s->name, name) == 0) return 0; // already declared in current scope
    }
    Symbol *sym = (Symbol*)malloc(sizeof(Symbol));
    memset(sym,0,sizeof(Symbol));
    sym->name = strdup(name);
    sym->kind = SYM_KIND_FUNC;
    sym->return_type = return_type;
    sym->param_count = param_count;
    if (param_count > 0 && param_types) {
        sym->param_types = (SymType*)malloc(sizeof(SymType)*param_count);
        for (int i = 0; i < param_count; ++i) sym->param_types[i] = param_types[i];
    } else {
        sym->param_types = NULL;
    }
    sym->declared_line = line;
    sym->declared_col = col;
    sym->next = head;
    st->current->buckets[idx] = sym;
    return 1;
}

Symbol* symtable_lookup(SymTable *st, const char *name) {
    if (!st || !name) return NULL;
    Scope *sc = st->current;
    while (sc) {
        unsigned idx = fnv1a_hash(name) % st->bucket_count;
        Symbol *s = sc->buckets[idx];
        while (s) {
            if (strcmp(s->name, name) == 0) return s;
            s = s->next;
        }
        sc = sc->prev;
    }
    return NULL;
}
