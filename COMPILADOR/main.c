// main.c — Pipeline completo Mauri-K
// Lexer → Parser → AST → Semántico → Codegen → IR → VM

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "symbol_table.h"
#include "semantic.h"
#include "ir.h"
#include "vm.h"       // VM final
                     // + codegen.c debe estar compilado en tu proyecto

// from codegen.c
int codegen_compile(ASTNode *root, Module *mod);

/* ============================================================
   Read entire file into memory
   ============================================================ */
static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char* buf = malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = '\0';

    fclose(f);
    return buf;
}

/* ============================================================
   MAIN
   ============================================================ */

int main(int argc, char **argv) {

    if (argc < 2) {
        fprintf(stderr, "Uso: %s archivo.mk\n", argv[0]);
        return 1;
    }

    /* -------------------------
       LEER ARCHIVO FUENTE
       ------------------------- */
    char *src = read_file(argv[1]);
    if (!src) {
        fprintf(stderr, "❌ Error: No se pudo abrir %s\n", argv[1]);
        return 1;
    }

    printf("📄 Archivo cargado: %s\n\n", argv[1]);

    /* -------------------------
       LEXER + PARSER
       ------------------------- */
    Lexer *lx = create_lexer(src);
    Parser *p = create_parser(lx);

    ASTNode *root = parse_program(p);

    if (!root || p->has_error) {
        printf("❌ Error sintáctico:\n");
        parser_print_error(p);
        free_parser(p);
        free(src);
        return 1;
    }

    printf("✅ AST construido correctamente.\n\n");
    printf("=== AST ===\n");
    print_ast(root, 0);
    printf("===========\n\n");

    /* -------------------------
       SEMÁNTICO
       ------------------------- */
    printf("🔍 Corriendo análisis semántico...\n");

    semantic_set_source(src);
    SymTable *st = symtable_create(503);

    int sem_errors = sem_check_program(root, st);

    if (sem_errors > 0) {
        printf("❌ El análisis semántico encontró %d error(es):\n", sem_errors);
        semantic_print_errors();

        symtable_destroy(st);
        free_ast(root);
        free_parser(p);
        free(src);
        return 1;
    }

    printf("✅ Análisis semántico OK.\n\n");

    /* -------------------------
       CODEGEN → IR
       ------------------------- */
    printf("⚙️ Generando código intermedio (IR)...\n");

    Module mod;
    if (!codegen_compile(root, &mod)) {
        printf("❌ Error durante codegen.\n");
        symtable_destroy(st);
        free_ast(root);
        free_parser(p);
        free(src);
        return 1;
    }

    /* -------------------------
       Dump IR en texto
       ------------------------- */
    FILE *f_ir = fopen("program.ir", "w");
    if (f_ir) {
        dump_module(&mod, f_ir);
        fclose(f_ir);
        printf("📦 IR guardado en program.ir\n");
    } else {
        printf("❌ No se pudo escribir program.ir\n");
    }

    /* -------------------------
       Serializar a binario para Arduino
       ------------------------- */
    FILE *f_bin = fopen("program.bin", "wb");
    if (f_bin) {
        module_serialize_binary(&mod, f_bin);
        fclose(f_bin);
        printf("💾 Binario guardado en program.bin\n");
    } else {
        printf("❌ No se pudo escribir program.bin\n");
    }

    /* -------------------------
   Exportar instrucciones tipo Arduino
   ------------------------- */
    vm_dump_module(&mod, "program_instr.txt");
    printf("📜 Instrucciones exportadas a program_instr.txt\n");
    vm_export_arduino_format(&mod, "program_arduino.txt");
    vm_export_tinyvm(&mod, "program.txt");


    /* -------------------------
       Ejecutar en la VM local
       ------------------------- */
    printf("\n🚀 Ejecutando en la VM local...\n\n");

    VM vm;
    vm_init(&vm);

    vm_run(&vm, &mod);

    vm_free(&vm);

    /* -------------------------
       Limpieza
       ------------------------- */

    module_free(&mod);
    symtable_destroy(st);
    free_ast(root);
    free_parser(p);
    free(src);

    printf("\n🏁 Ejecución finalizada.\n");
    return 0;
}
