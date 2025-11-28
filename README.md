# Proyecto **TEO** - Lenguaje **Mauri-k**

Este proyecto incluye el compilador, la máquina virtual y el debugger para el lenguaje **Mauri-k**, permitiendo transformar código fuente en bytecode y ejecutar o depurar dicho bytecode.

**Compilar el compilador (`mauri-k`):**  
Genera el ejecutable encargado de analizar, validar y convertir programas Mautik en bytecode ejecutable.

```bash
gcc -o maurik main.c lexer.c parser.c semantic.c symbol_table.c ir.c codegen.c vm.c
```

**Compilar el debugger extendido:**
Produce una versión del intérprete con herramientas de inspección para analizar el flujo del bytecode en tiempo de ejecución.

```bash
gcc debug_full.c ir.c -o vm_full_debug.exe
```

**Código de ejemplo en Mauri-k:**
Demuestra la sintaxis básica del lenguaje y una operación simple con variables numéricas, el archivo que buscara el compilador es simple.mk.

```bash
abracadabra {
    #! Codigo de ejemplo
    num primero = 10;
    num segundo = 20;
    num resultado = primero + segundo;
}
```

**Compilar un archivo .mk:**
Ejecuta el compilador sobre un archivo de construcción y genera program.bin, que contiene el bytecode.

```bash
./maurik simple.mk
```

**Ejecutar el debugger:**
Carga y analiza el bytecode generado, permitiendo ver cada instrucción ejecutada.

```bash
./vm_full_debug.exe program.bin
```



