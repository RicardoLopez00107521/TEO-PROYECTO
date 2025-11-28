/*
  Seguidor de línea / VM para carrito
  - L298N: IN1/IN2/ENA = Izq, IN3/IN4/ENB = Der
  - Sensores en A0 (izq) y A1 (der) como digitales
*/

#include <SPI.h>
#include <SD.h>

// ======== PINES MOTORES Y SENSORES ========
const int IN1 = 9;   // Motor Izq
const int IN2 = 8;
const int ENA = 5;

const int IN3 = 7;   // Motor Der
const int IN4 = 6;
const int ENB = 3;

const int sensorIzqPin = A0; // Digital en pin analógico A0
const int sensorDerPin = A1; // Digital en pin analógico A1

// ===== AJUSTES RÁPIDOS =====
int PWM_BASE    = 132;   // velocidad recta (variable para acelerar/desacelerar)
int PWM_PIVOT   = 120;   // velocidad de la rueda externa en curva
const bool LINEA_ALTA = false; // true si tu sensor da HIGH sobre negro; false si da LOW

int lastTurn = 1; // 1 = derecha, -1 = izquierda (para búsqueda)

// ======== SD / PROGRAMA ========
const int CHIP_SELECT = 10;
const char *PROGRAM_FILE = "prog.txt";

// Máximo de instrucciones que cargaremos en RAM (optimizado)
const uint8_t MAX_PROGRAM = 64;

// Registros generales para operaciones matemáticas (8-bit suficiente)
const uint8_t NUM_REGS = 8;
uint8_t regs[NUM_REGS];   // regs[0..7]

// ===================================================
//       FUNCIONES DEL CARRITO (PRIMERO QUE TODO)
// ===================================================

bool hayLinea(int pin) {
  int v = digitalRead(pin);
  return LINEA_ALTA ? (v == HIGH) : (v == LOW); // true si ve negro
}

// ESTA ES TU FUNCIÓN "ADELANTE" ORIGINAL
void adelante(/*DISTANCIA EN cm*/) {
  // Dirección hacia delante en ambos motores
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
}

void setPWM(int pwmIzq, int pwmDer) {
  pwmIzq = constrain(pwmIzq, 0, 255);
  pwmDer = constrain(pwmDer, 0, 255);
  analogWrite(ENA, pwmIzq);
  analogWrite(ENB, pwmDer);
}

// ===================================================
//                    VM / INTÉRPRETE
// ===================================================

// ======== VM: definición de opcodes ========
// Usamos campos compactos para ahorrar RAM
enum OpCode : uint8_t {
  OP_CONST,
  OP_STORE_LOCAL,
  OP_LOAD_LOCAL,
  OP_GT,
  OP_LT,
  OP_EQ,
  OP_NEQ,
  OP_JZ,
  OP_JMP,
  OP_ADD,
  OP_SUB,
  OP_CALL,
  OP_RET,
  OP_NOT,
  OP_AND,
  OP_SENSOR_IZQ,
  OP_SENSOR_DER,

  OP_STORE_GLOBAL,
  OP_LOAD_GLOBAL,
  OP_INIT_GLOBAL,
  OP_END_GLOBAL,

  OP_AVANZAR,
  OP_DETENER,
  OP_RETROCEDER,
  OP_GIRAR_DER,
  OP_GIRAR_IZQ,
  OP_ACELERAR,
  OP_DESACELERAR,
  OP_DELAY,

  OP_HALT,
  OP_UNKNOWN
};

struct Instr {
  uint8_t op;   // almacenamos como byte
  int8_t arg1;
  int8_t arg2;
};

Instr program[MAX_PROGRAM];
uint8_t programSize = 0;
uint8_t entryPc = 0; // punto de entrada (por defecto 0)
uint8_t initGlobalStart = 0; // inicio de sección INIT_GLOBAL
uint8_t initGlobalEnd = 0;   // fin de sección INIT_GLOBAL
bool initGlobalExecuted = false; // flag para ejecutar init solo una vez
uint8_t numGlobals = 0; // número de variables globales declaradas en INIT_GLOBAL
// VM runtime state for step execution in loop()
int vmPc = 0;
bool vmRunning = false;
// Si true, reinicia el programa automáticamente cuando termine (loop indefinido)
const bool VM_REPEAT = true;

// ======== SCHEDULER NO-BLOQUEANTE ========
// Action type constants (using uint8_t instead of enum to avoid tab compilation issues)
const uint8_t ACT_NONE = 0;
const uint8_t ACT_AVANZAR = 1;
const uint8_t ACT_GIRAR_DER = 2;
const uint8_t ACT_GIRAR_IZQ = 3;
const uint8_t ACT_RETROCEDER = 4;

uint8_t currentAction = ACT_NONE;
unsigned long actionStartTime = 0;
unsigned long actionDuration = 0;
unsigned long delayUntil = 0; // timestamp hasta el cual esperar (delay no-bloqueante)

// ======== VM: locales y pila ========
const uint8_t NUM_LOCALS = 8;   // suficientes para tu programa (se reinician en VM_REPEAT)
int16_t locals[NUM_LOCALS];   // usamos 16 bits para seguridad aritmética

// Variables GLOBALES persistentes (NO se reinician en VM_REPEAT)
const uint8_t NUM_GLOBALS = 8;
int16_t globals[NUM_GLOBALS];   // variables que persisten entre reinicios de VM

// reducir tamaño de pila para ahorrar RAM
const uint8_t STACK_SIZE = 32;  // aumentado de 8 a 32 para más capacidad
int16_t stackVM[STACK_SIZE];
int8_t sp = 0;  // stack pointer (8-bit suficiente)

// Pila de retornos para CALL/RET
const uint8_t RET_STACK_SIZE = 16;  // aumentado de 8 a 16
int16_t retStack[RET_STACK_SIZE];
int8_t rp = 0;

// Frames de locales por llamada (para soporte de funciones)
int16_t localsFrames[RET_STACK_SIZE][NUM_LOCALS];

void push(int16_t v) {
  if (sp < STACK_SIZE) {
    stackVM[sp++] = v;
  }
}

int16_t pop() {
  if (sp > 0) {
    return stackVM[--sp];
  }
  return 0;
}


// ======== PROTOTIPOS ========
bool loadProgramFromSD();
bool readLine(File &f, char *buf, size_t maxLen);
OpCode decodeMnemonic(const char *mn);
void runProgram();
void ejecutarTrap(int8_t code, int8_t arg);
void startAction(uint8_t actionType, unsigned long durationMs);
void updateAction();
void stopAction();

// DEBUG: activar para trazas por Serial (desactiva para producción)
// #define DEBUG 1


// ===================================================
//                    SETUP
// ===================================================

void setup() {
  // Motores
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT); pinMode(ENB, OUTPUT);

  // Sensores (por si luego los usas en TRAP)
  pinMode(sensorIzqPin, INPUT);
  pinMode(sensorDerPin, INPUT);

  // Serial opcional
  Serial.begin(9600);
  while (!Serial) { ; }

  Serial.println(F("Iniciando SD..."));
  if (!SD.begin(CHIP_SELECT)) {
    Serial.println(F("Error inicializando SD."));
  } else {
    Serial.println(F("SD OK."));
  }

  // Cargar programa desde SD
  if (loadProgramFromSD()) {
    Serial.print(F("Programa cargado. Instrucciones: "));
    Serial.println(programSize);
    
    // Ejecutar sección INIT_GLOBAL si existe
    if (initGlobalStart < initGlobalEnd) {
      Serial.println(F("Ejecutando INIT_GLOBAL..."));
      vmPc = initGlobalStart;
      vmRunning = true;
      // Ejecutar hasta llegar a END_GLOBAL
      while (vmRunning && vmPc <= initGlobalEnd) {
        runProgram();
      }
      initGlobalExecuted = true;
      Serial.println(F("INIT_GLOBAL completado."));
    }
    
    // inicializar VM para ejecución paso a paso en loop()
    // Si hay INIT_GLOBAL, empezar después de END_GLOBAL
    if (initGlobalExecuted && initGlobalEnd > 0) {
      vmPc = initGlobalEnd + 1;
    } else {
      vmPc = entryPc;
    }
    vmRunning = (programSize > 0);
  } else {
    Serial.println(F("No se pudo cargar programa. (prog.txt)"));
  }

  // Dirección por defecto hacia adelante
  adelante();
  setPWM(0, 0); // arranca detenido
  delay(5000); // espera 5 segundos antes de iniciar
}

// ===================================================
//                    LOOP
// ===================================================

void loop() {
  // Si hay un delay activo, solo actualizar motores y esperar
  if (delayUntil > 0) {
    if (millis() < delayUntil) {
      updateAction(); // Mantener motores funcionando
      return; // No ejecutar VM
    }
    // Delay completado
    delayUntil = 0;
  }
  
  // Actualizar acciones no-bloqueantes (motores)
  updateAction();
  
  // Ejecutar múltiples instrucciones por loop para respuesta rápida (hasta 50 por ciclo)
  if (vmRunning) {
    for (uint8_t i = 0; i < 50 && vmRunning; i++) {
      runProgram();
      // Si se activó un delay, detener la ejecución inmediatamente
      if (delayUntil > 0) {
        break;
      }
    }
  }
}

// ===================================================
//            IMPLEMENTACIÓN DE LA VM
// ===================================================

// Cargar programa desde prog.txt en SD
bool loadProgramFromSD() {
  File f = SD.open(PROGRAM_FILE, FILE_READ);
  if (!f) {
    Serial.println(F("No se pudo abrir prog.txt"));
    return false;
  }

  char line[64];
  programSize = 0;
  bool inGlobalSection = false;
  uint8_t globalSectionStart = 0;

  while (programSize < MAX_PROGRAM && readLine(f, line, sizeof(line))) {
    // Quitar espacios al inicio
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') continue;      // línea vacía

    // eliminar comentarios inline (#)
    char *hash = strchr(p, '#');
    if (hash) *hash = '\0';

    // recortar espacios finales
    char *end = p + strlen(p) - 1;
    while (end >= p && (*end == ' ' || *end == '\t')) { *end = '\0'; end--; }

    if (*p == '\0') continue;      // ahora puede quedar vacía

    // Tokenizar: MNEMONIC arg1 arg2
    char *token = strtok(p, " \t");
    if (!token) continue;

    char *mnemonic = token;

    token = strtok(NULL, " \t");
    int arg1 = token ? atoi(token) : 0;

    token = strtok(NULL, " \t");
    int arg2 = token ? atoi(token) : 0;

    // Soporte para directiva ENTRY: ENTRY N
    if (strcmp(mnemonic, "ENTRY") == 0) {
      entryPc = (uint8_t)constrain(arg1, 0, (int)MAX_PROGRAM - 1);
      continue;
    }

    // Detectar inicio de sección INIT_GLOBAL
    if (strcmp(mnemonic, "INIT_GLOBAL") == 0) {
      inGlobalSection = true;
      globalSectionStart = programSize;
      initGlobalStart = programSize;
    }

    // Detectar fin de sección INIT_GLOBAL
    if (strcmp(mnemonic, "END_GLOBAL") == 0) {
      inGlobalSection = false;
      initGlobalEnd = programSize;
    }

    Instr ins;
    ins.op = (uint8_t)decodeMnemonic(mnemonic);
    
    // Dentro de INIT_GLOBAL, convertir STORE_LOCAL a STORE_GLOBAL y contar variables
    if (inGlobalSection && ins.op == OP_STORE_LOCAL) {
      ins.op = OP_STORE_GLOBAL;
      // Actualizar contador de globales (arg1 es el índice de la variable)
      if (arg1 >= numGlobals) {
        numGlobals = arg1 + 1;
      }
    }
    
    ins.arg1 = (int8_t)arg1;
    ins.arg2 = (int8_t)arg2;
    program[programSize++] = ins;
  }

  f.close();
  return (programSize > 0);
}

// Leer una línea de un File a un buffer char[]
bool readLine(File &f, char *buf, size_t maxLen) {
  size_t i = 0;

  while (f.available() && i < maxLen - 1) {
    char c = f.read();
    if (c == '\r') continue; // ignorar CR
    if (c == '\n') break;    // fin de línea
    buf[i++] = c;
  }

  buf[i] = '\0';

  // Si no leí nada y tampoco hay más datos, fin de archivo
  if (i == 0 && !f.available()) {
    return false;
  }
  return true;
}

// Traducir texto a OpCode
OpCode decodeMnemonic(const char *mn) {
  // Lógica de la TinyVM
  if (strcmp(mn, "CONST") == 0)        return OP_CONST;
  if (strcmp(mn, "STORE_LOCAL") == 0)  return OP_STORE_LOCAL;
  if (strcmp(mn, "LOAD_LOCAL") == 0)   return OP_LOAD_LOCAL;
  if (strcmp(mn, "GT") == 0)           return OP_GT;
  if (strcmp(mn, "LT") == 0)           return OP_LT;
  if (strcmp(mn, "EQ") == 0)           return OP_EQ;
  if (strcmp(mn, "NEQ") == 0)          return OP_NEQ;
  if (strcmp(mn, "JZ") == 0)           return OP_JZ;
  if (strcmp(mn, "JMP") == 0)          return OP_JMP;
  if (strcmp(mn, "ADD") == 0)          return OP_ADD;
  if (strcmp(mn, "SUB") == 0)          return OP_SUB;
  if (strcmp(mn, "CALL") == 0)         return OP_CALL;
  if (strcmp(mn, "RET") == 0)          return OP_RET;
  if (strcmp(mn, "NOT") == 0)          return OP_NOT;
  if (strcmp(mn, "STORE_GLOBAL") == 0) return OP_STORE_GLOBAL;
  if (strcmp(mn, "LOAD_GLOBAL") == 0)  return OP_LOAD_GLOBAL;
  if (strcmp(mn, "INIT_GLOBAL") == 0)  return OP_INIT_GLOBAL;
  if (strcmp(mn, "END_GLOBAL") == 0)   return OP_END_GLOBAL;
  if (strcmp(mn, "AND") == 0)          return OP_AND;
  if (strcmp(mn, "SENSOR_IZQ") == 0)   return OP_SENSOR_IZQ;
  if (strcmp(mn, "SENSOR_DER") == 0)   return OP_SENSOR_DER;

  // Instrucciones de movimiento del carrito
  if (strcmp(mn, "AVANZAR") == 0)      return OP_AVANZAR;
  if (strcmp(mn, "DETENER") == 0)      return OP_DETENER;
  if (strcmp(mn, "RETROCEDER") == 0)   return OP_RETROCEDER;
  if (strcmp(mn, "GIRAR_DER") == 0)    return OP_GIRAR_DER;
  if (strcmp(mn, "GIRAR_IZQ") == 0)    return OP_GIRAR_IZQ;
  if (strcmp(mn, "ACELERAR") == 0)     return OP_ACELERAR;
  if (strcmp(mn, "DESACELERAR") == 0)  return OP_DESACELERAR;
  if (strcmp(mn, "DELAY") == 0)        return OP_DELAY;

  if (strcmp(mn, "HALT") == 0)         return OP_HALT;

  return OP_UNKNOWN;
}



// Ejecutar UNA instrucción del programa (modo cooperativo en loop)
void runProgram() {
  if (programSize == 0) {
    vmRunning = false;
    return;
  }

  if (!vmRunning) return;

  int pc = vmPc;
  if (pc < 0 || pc >= programSize) {
    vmRunning = false;
    return;
  }

  Instr &ins = program[pc];

#ifdef DEBUG
  Serial.print(F("pc=")); Serial.print(pc);
  Serial.print(F(" op=")); Serial.print(ins.op);
  Serial.print(F(" sp=")); Serial.print(sp);
  Serial.print(F(" rp=")); Serial.println(rp);
#endif

  switch (ins.op) {
      // ===== VM genérica =====
      case OP_CONST: {
        // Usa arg1 como valor inmediato
        push((int16_t)ins.arg1);
        pc++;
        break;
      }

      case OP_STORE_LOCAL: {
        int idx = ins.arg1;
        // Si el índice corresponde a una variable global, usar globals[] en su lugar
        if (idx >= 0 && idx < numGlobals && numGlobals > 0) {
          int16_t v = pop();
          globals[idx] = v;
          Serial.print(F("STORE_LOCAL["));
          Serial.print(idx);
          Serial.print(F("] -> GLOBAL = "));
          Serial.println(v);
        } else if (idx >= 0 && idx < NUM_LOCALS) {
          int16_t v = pop();
          locals[idx] = v;
#ifdef DEBUG
          Serial.print(F("STORE_LOCAL ")); Serial.print(idx); Serial.print(F(" = ")); Serial.println(v);
#endif
        }
        pc++;
        break;
      }

      case OP_LOAD_LOCAL: {
        int idx = ins.arg1;
        // Si el índice corresponde a una variable global, usar globals[] en su lugar
        if (idx >= 0 && idx < numGlobals && numGlobals > 0) {
          push(globals[idx]);
          Serial.print(F("LOAD_LOCAL["));
          Serial.print(idx);
          Serial.print(F("] -> GLOBAL = "));
          Serial.println(globals[idx]);
        } else if (idx >= 0 && idx < NUM_LOCALS) {
          push(locals[idx]);
        } else {
          push(0);
        }
        pc++;
        break;
      }

      case OP_STORE_GLOBAL: {
        int idx = ins.arg1;
        if (idx >= 0 && idx < NUM_GLOBALS) {
          int16_t v = pop();
          globals[idx] = v;
          Serial.print(F("STORE_GLOBAL[")); Serial.print(idx); 
          Serial.print(F("] = ")); Serial.println(v);
        }
        pc++;
        break;
      }

      case OP_LOAD_GLOBAL: {
        int idx = ins.arg1;
        if (idx >= 0 && idx < NUM_GLOBALS) {
          push(globals[idx]);
          Serial.print(F("LOAD_GLOBAL[")); Serial.print(idx); 
          Serial.print(F("] = ")); Serial.println(globals[idx]);
        } else {
          push(0);
        }
        pc++;
        break;
      }

      case OP_INIT_GLOBAL:
        // Directiva de marcador - no hace nada, solo avanza
        pc++;
        break;

      case OP_END_GLOBAL:
        // Directiva de marcador - no hace nada, solo avanza
        pc++;
        break;

      case OP_ADD: {
        int16_t b = pop();
        int16_t a = pop();
        push(a + b);
        pc++;
        break;
      }

      case OP_SUB: {
        int16_t b = pop();
        int16_t a = pop();
        push(a - b);
        pc++;
        break;
      }

      case OP_GT: {
        int16_t b = pop();
        int16_t a = pop();
        push(a > b ? 1 : 0);
        pc++;
        break;
      }

      case OP_LT: {
        int16_t b = pop();
        int16_t a = pop();
        push(a < b ? 1 : 0);
        pc++;
        break;
      }

      case OP_EQ: {
        int16_t b = pop();
        int16_t a = pop();
        push(a == b ? 1 : 0);
        pc++;
        break;
      }

      case OP_NEQ: {
        int16_t b = pop();
        int16_t a = pop();
        push(a != b ? 1 : 0);
        pc++;
        break;
      }

      case OP_NOT: {
        int16_t v = pop();
#ifdef DEBUG
        Serial.print(F("NOT -> ")); Serial.println(v == 0 ? 1 : 0);
#endif
        push(v == 0 ? 1 : 0);
        pc++;
        break;
      }

            case OP_AND: {
        int16_t b = pop();
        int16_t a = pop();
      #ifdef DEBUG
        Serial.print(F("AND -> ")); Serial.println((a && b) ? 1 : 0);
      #endif
        push((a && b) ? 1 : 0);
        pc++;
        break;
            }

      case OP_JZ: {
        int16_t cond = pop();
        if (cond == 0) {
          pc = ins.arg1;
        } else {
          pc++;
        }
        break;
      }

      case OP_JMP:
        pc = ins.arg1;
        break;

      case OP_CALL: {
        // arg1 = target address, arg2 = nargs
        int target = ins.arg1;
        int nargs = ins.arg2;
        if (rp < RET_STACK_SIZE) {
          // guardar frame actual de locals
          for (uint8_t i = 0; i < NUM_LOCALS; ++i) localsFrames[rp][i] = locals[i];
          // guardar dirección de retorno
          retStack[rp] = pc + 1;
#ifdef DEBUG
          Serial.print(F("CALL -> ")); Serial.print(target); Serial.print(F(" retAddr=")); Serial.println(pc+1);
#endif
          // preparar nuevos locals: tomar nargs desde la pila (orden correcto)
          for (uint8_t i = 0; i < NUM_LOCALS; ++i) locals[i] = 0;
          for (int i = nargs - 1; i >= 0; --i) {
            if (sp > 0) locals[i] = pop(); else locals[i] = 0;
          }
          rp++; // avanzar puntero de retorno/frames
          pc = target;
        } else {
          // stack overflow on return stack: abort call
          pc++;
        }
        break;
      }

            case OP_SENSOR_IZQ: {
        bool v = hayLinea(sensorIzqPin);
      #ifdef DEBUG
        Serial.print(F("SENSOR_IZQ => ")); Serial.println(v ? 1 : 0);
      #endif
        push(v ? 1 : 0);
        pc++;
        break;
            }

            case OP_SENSOR_DER: {
        bool v = hayLinea(sensorDerPin);
      #ifdef DEBUG
        Serial.print(F("SENSOR_DER => ")); Serial.println(v ? 1 : 0);
      #endif
        push(v ? 1 : 0);
        pc++;
        break;
            }

      case OP_RET: {
        // return to caller; return value should be on data stack
        if (rp > 0) {
          // retroceder al frame anterior
          rp--;
#ifdef DEBUG
          Serial.print(F("RET -> ")); Serial.println(retStack[rp]);
#endif
          // restaurar locals previos
          for (uint8_t i = 0; i < NUM_LOCALS; ++i) locals[i] = localsFrames[rp][i];
          pc = retStack[rp];
        } else {
          // no return address: stop VM
          vmRunning = false;
        }
        break;
      }

      // ===== MOVIMIENTO DEL CARRITO =====
      case OP_AVANZAR: {
        Serial.println(F("AVANZAR"));
        // Duración 0 = continuo hasta que se ejecute otra acción
        unsigned long dur = (ins.arg2 > 0) ? (ins.arg2 * 100UL) : 0UL;
        startAction(ACT_AVANZAR, dur);
        pc++;
        break;
      }

      case OP_DETENER:
        Serial.println(F("DETENER"));
        stopAction();
        pc++;
        break;

      case OP_RETROCEDER: {
        Serial.println(F("RETROCEDER"));
        // Duración 0 = continuo hasta que se ejecute otra acción
        unsigned long dur = (ins.arg2 > 0) ? (ins.arg2 * 100UL) : 0UL;
        startAction(ACT_RETROCEDER, dur);
        pc++;
        break;
      }

      case OP_GIRAR_DER: {
        Serial.println(F("GIRAR_DER"));
        // Duración 0 = continuo hasta que se ejecute otra acción
        unsigned long dur = (ins.arg2 > 0) ? (ins.arg2 * 100UL) : 0UL;
        startAction(ACT_GIRAR_DER, dur);
        pc++;
        break;
      }

      case OP_GIRAR_IZQ: {
        Serial.println(F("GIRAR_IZQ"));
        // Duración 0 = continuo hasta que se ejecute otra acción
        unsigned long dur = (ins.arg2 > 0) ? (ins.arg2 * 100UL) : 0UL;
        startAction(ACT_GIRAR_IZQ, dur);
        pc++;
        break;
      }

      case OP_ACELERAR: {
        Serial.println(F("ACELERAR"));
        PWM_BASE = constrain(PWM_BASE + 20, 0, 255);
        PWM_PIVOT = constrain(PWM_PIVOT + 20, 0, 255);
        Serial.print(F("Velocidad: BASE=")); Serial.print(PWM_BASE);
        Serial.print(F(" PIVOT=")); Serial.println(PWM_PIVOT);
        // Reaplica PWM si hay acción activa
        if (currentAction == ACT_AVANZAR) {
          setPWM(PWM_BASE, PWM_BASE);
        } else if (currentAction == ACT_GIRAR_DER) {
          setPWM(PWM_PIVOT, 0);
        } else if (currentAction == ACT_GIRAR_IZQ) {
          setPWM(0, PWM_PIVOT);
        }
        pc++;
        break;
      }

      case OP_DESACELERAR: {
        Serial.println(F("DESACELERAR"));
        PWM_BASE = constrain(PWM_BASE - 20, 0, 255);
        PWM_PIVOT = constrain(PWM_PIVOT - 20, 0, 255);
        Serial.print(F("Velocidad: BASE=")); Serial.print(PWM_BASE);
        Serial.print(F(" PIVOT=")); Serial.println(PWM_PIVOT);
        // Reaplica PWM si hay acción activa
        if (currentAction == ACT_AVANZAR) {
          setPWM(PWM_BASE, PWM_BASE);
        } else if (currentAction == ACT_GIRAR_DER) {
          setPWM(PWM_PIVOT, 0);
        } else if (currentAction == ACT_GIRAR_IZQ) {
          setPWM(0, PWM_PIVOT);
        }
        pc++;
        break;
      }

      case OP_DELAY:
        Serial.println(F("DELAY"));
        delayUntil = millis() + 1000; // pausar VM por 1 segundo
        pc++;
        break;

      // ===== HALT / desconocidos =====
      case OP_HALT:
        Serial.println(F("HALT"));
        vmRunning = false;
        break;

      case OP_UNKNOWN:
      default:
        Serial.println(F("OP_UNKNOWN, se ignora"));
        pc++;
        break;
    }

  // guardar pc para la siguiente iteración
  vmPc = pc;

  // Si la VM dejó de ejecutarse, NO detener motores (mantener última acción activa)
  if (!vmRunning) {
    if (NUM_LOCALS > 2) {
      Serial.print(F("VERIF LOCAL[2] = "));
      Serial.println(locals[2]);
    }
    if (VM_REPEAT) {
      // reiniciar estado de VM para ejecución indefinida
      sp = 0; rp = 0;
      for (uint8_t i = 0; i < NUM_LOCALS; ++i) locals[i] = 0;
      for (uint8_t i = 0; i < STACK_SIZE; ++i) stackVM[i] = 0;
      // NOTA: globals[] NO se reinicia - persiste entre repeticiones
      // Saltar la sección INIT_GLOBAL en repeticiones
      if (initGlobalExecuted && initGlobalEnd > 0) {
        vmPc = initGlobalEnd + 1; // saltar después de END_GLOBAL
      } else {
        vmPc = entryPc;
      }
      vmRunning = true;
    }
  }
}

// ===================================================
//          SCHEDULER NO-BLOQUEANTE
// ===================================================

void startAction(uint8_t actionType, unsigned long durationMs) {
  // Solo cambiar motores si es una acción diferente a la actual
  if (currentAction == actionType) {
    return; // Ya está ejecutando esta acción, no cambiar nada
  }

  currentAction = actionType;
  actionStartTime = millis();
  actionDuration = durationMs;

  // Configurar motores según acción
  switch (actionType) {
    case ACT_AVANZAR:
      adelante();
      setPWM(PWM_BASE, PWM_BASE);
      break;
    case ACT_GIRAR_DER:
      adelante();
      setPWM(PWM_PIVOT, 0);
      break;
    case ACT_GIRAR_IZQ:
      adelante();
      setPWM(0, PWM_PIVOT);
      break;
    case ACT_RETROCEDER:
      digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
      digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);
      setPWM(PWM_BASE, PWM_BASE);
      break;
    default:
      stopAction();
      break;
  }
}

void updateAction() {
  if (currentAction != ACT_NONE && actionDuration > 0) {
    if (millis() - actionStartTime >= actionDuration) {
      stopAction();
    }
  }
}

void stopAction() {
  currentAction = ACT_NONE;
  setPWM(0, 0);
}

// Ejecutar TRAP: conecta VM con tu carrito
void ejecutarTrap(int8_t code, int8_t arg) {
  // arg >= 0 -> ticks = arg
  // arg == -1 -> usar regs[0] como ticks
  uint16_t ticks;

  if (arg == -1) {
    ticks = (regs[0] <= 0) ? 5 : (uint16_t)regs[0];
  } else if (arg <= 0) {
    ticks = 5; // mínimo
  } else {
    ticks = (uint16_t)arg;
  }

  uint16_t duracion = ticks * 100; // ms

  switch (code) {
    case 10: // AVANZAR RECTO
      startAction(ACT_AVANZAR, duracion);
      break;

    case 11: // GIRAR IZQUIERDA
      startAction(ACT_GIRAR_IZQ, duracion);
      break;

    case 12: // GIRAR DERECHA
      startAction(ACT_GIRAR_DER, duracion);
      break;

    default:
      Serial.print(F("TRAP desconocido: "));
      Serial.print(code);
      Serial.print(F(" arg="));
      Serial.println(arg);
      break;
  }
}


