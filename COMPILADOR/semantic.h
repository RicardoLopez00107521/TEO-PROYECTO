// semantic.h
#ifndef SEMANTIC_H
#define SEMANTIC_H

#include <stdio.h>
#include "parser.h"       // ASTNode definition and NODE_* enums
#include "symbol_table.h" // SymTable, symtable_*

// Perform semantic analysis on AST 'root' using symbol table 'st'.
// Returns number of semantic errors found.
int sem_check_program(ASTNode *root, SymTable *st);

// Helpers to query/print errors
int semantic_has_errors(void);
void semantic_print_errors(void);

// set source buffer so semantic errors can print context
void semantic_set_source(const char *src);

// helper used internally (but exposed if needed)
void semantic_print_source_error(int line, int column);

#endif // SEMANTIC_H
