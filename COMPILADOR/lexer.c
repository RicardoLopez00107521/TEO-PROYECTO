#include "lexer.h"

// =====================================================
// Helpers internos
// =====================================================

// Retorna el caracter actual del input
static char current_char(Lexer* lexer) {
    if (lexer->position >= lexer->length)
        return '\0';
    return lexer->input[lexer->position];
}

// Retorna el siguiente caracter sin avanzar
static char peek_char(Lexer* lexer) {
    if (lexer->position + 1 >= lexer->length)
        return '\0';
    return lexer->input[lexer->position + 1];
}

// Avanza un caracter en el input, actualizando línea/columna
static void advance(Lexer* lexer) {
    if (current_char(lexer) == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    lexer->position++;
}

// =====================================================
// Crear y destruir lexer
// =====================================================

Lexer* create_lexer(const char* input) {
    Lexer *lexer = (Lexer*)malloc(sizeof(Lexer));
    if (!lexer) {
        fprintf(stderr, "Error: Cant assign memmory to the lexer\n");
        return NULL;
    }

    lexer->length = strlen(input);
    lexer->input = (char*)malloc(lexer->length + 1);
    if (!lexer->input) {
        fprintf(stderr, "Error: Cant assign memmory to the lexer\n");
        free(lexer);
        return NULL;
    }

    strcpy(lexer->input, input);
    lexer->position = 0;
    lexer->line = 1;
    lexer->column = 1;

    return lexer;
}

void free_lexer(Lexer* lexer) {
    if (lexer) {
        if (lexer->input) {
            free(lexer->input);
        }
        free(lexer);    
    }
}

// =====================================================
// Manejo de tokens
// =====================================================

Token create_token(TokenType type, const char* value, int line, int column) {
    Token token;
    token.type = type;
    token.line = line;
    token.column = column;

    if (value) {
        token.value = (char*)malloc(strlen(value) + 1);
        if (token.value) {
            strcpy(token.value, value);
        }
    } else {
        token.value = NULL;
    }

    return token;
}

void free_token(Token* token) {
    if (token && token->value) {
        free(token->value);
        token->value = NULL;
    }
}

char* token_type_to_string(TokenType type) {
    switch (type) {
        case TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TOKEN_INT_LITERAL: return "INT_LITERAL";
        case TOKEN_FLOAT_LITERAL: return "FLOAT_LITERAL";
        case TOKEN_CHAR_LITERAL: return "CHAR_LITERAL";
        case TOKEN_BOOL_LITERAL: return "BOOL_LITERAL";

        case TOKEN_ABRACADABRA: return "ABRACADABRA";
        case TOKEN_NUM: return "NUM";
        case TOKEN_FRAC: return "FRAC";
        case TOKEN_BOOL: return "BOOL";
        case TOKEN_CHAR: return "CHAR";
        case TOKEN_IF: return "IF";
        case TOKEN_ELSE: return "ELSE";
        case TOKEN_FOR: return "FOR";
        case TOKEN_WHILE: return "WHILE";
        //case TOKEN_HABLAR: return "HABLAR";
        case TOKEN_BREAK: return "BREAK";
        case TOKEN_RETURN: return "RETURN";
        case TOKEN_OR: return "OR";
        case TOKEN_AND: return "AND";
        case TOKEN_NOT: return "NOT";
        case TOKEN_AHUEVO: return "AHUEVO";
        case TOKEN_PAJA: return "PAJA";

        case TOKEN_ASSIGN: return "ASSIGN";
        case TOKEN_EQUAL: return "EQUAL";
        case TOKEN_NOT_EQUAL: return "NOT_EQUAL";
        case TOKEN_LESS: return "LESS";
        case TOKEN_GREATER: return "GREATER";
        case TOKEN_LESS_EQ: return "LESS_EQ";
        case TOKEN_GREATER_EQ: return "GREATER_EQ";
        case TOKEN_PLUS: return "PLUS";
        case TOKEN_MINUS: return "MINUS";
        case TOKEN_MUL: return "MUL";
        case TOKEN_DIV: return "DIV";
        case TOKEN_MOD: return "MOD";

        case TOKEN_LPAREN: return "LPAREN";
        case TOKEN_RPAREN: return "RPAREN";
        case TOKEN_LBRACE: return "LBRACE";
        case TOKEN_RBRACE: return "RBRACE";
        case TOKEN_LBRACKET: return "LBRACKET";
        case TOKEN_RBRACKET: return "RBRACKET";
        case TOKEN_SEMICOLON: return "SEMICOLON";
        case TOKEN_COMMA: return "COMMA";

        case TOKEN_EOF: return "EOF";
        case TOKEN_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void print_token(Token* token) {
    if (token->value)
        printf("%s ('%s') at %d:%d\n",
            token_type_to_string(token->type),
            token->value,
            token->line,
            token->column
        );
    else
        printf("%s at %d:%d\n",
            token_type_to_string(token->type),
            token->line,
            token->column
        );
}

// =====================================================
// Saltar espacios y comentarios
// =====================================================

void skip_whitespace_and_comments(Lexer* lexer) {

    while (lexer->position < lexer->length) {

        char c = current_char(lexer);
        char p = peek_char(lexer);

        // Espacios
        if (isspace(c)) {
            advance(lexer);
            continue;
        }

        // Comentario "#!"
        if (c == '#' && p == '!') {

            advance(lexer); // #
            advance(lexer); // !

            while (current_char(lexer) != '\n' &&
                   current_char(lexer) != '\0') {
                advance(lexer);
            }

            if (current_char(lexer) == '\n')
                advance(lexer);

            continue;
        }
        break;
    }
}


// =====================================================
// Leer números (enteros o flotantes)
// =====================================================

Token read_number(Lexer* lexer) {
    int start = lexer->position;
    int line = lexer->line;
    int col  = lexer->column;

    while (isdigit(current_char(lexer))) {
        advance(lexer);
    }

    // ¿Flotante?
    if (current_char(lexer) == '.' && isdigit(peek_char(lexer))) {
        advance(lexer); // consumir '.'

        while (isdigit(current_char(lexer))) {
            advance(lexer);
        }

        int len = lexer->position - start;
        char* text = malloc(len + 1);
        strncpy(text, lexer->input + start, len);
        text[len] = '\0';

        Token t = create_token(TOKEN_FLOAT_LITERAL, text, line, col);
        free(text);
        return t;
    }

    // Entero
    int len = lexer->position - start;
    char* text = malloc(len + 1);
    strncpy(text, lexer->input + start, len);
    text[len] = '\0';

    Token t = create_token(TOKEN_INT_LITERAL, text, line, col);
    free(text);
    return t;
}

// =====================================================
// Leer literal de caracter
// =====================================================

Token read_char_literal(Lexer* lexer) {
    int line = lexer->line;
    int col = lexer->column;

    advance(lexer); // consumir '

    char c = current_char(lexer);

    if (c == '\0' || c == '\n')
        return create_token(TOKEN_ERROR, "Unclosed char literal", line, col);

    advance(lexer); // consumir caracter

    if (current_char(lexer) != '\'')
        return create_token(TOKEN_ERROR, "Unclosed char literal", line, col);

    advance(lexer); // consumir '

    char value[2] = {c, '\0'};
    return create_token(TOKEN_CHAR_LITERAL, value, line, col);
}

// =====================================================
// Leer identificadores o palabras reservadas
// =====================================================

Token read_identifier_or_keyword(Lexer* lexer) {
    int start = lexer->position;
    int line = lexer->line;
    int col  = lexer->column;

    while (isalnum(current_char(lexer)) || current_char(lexer) == '_')
        advance(lexer);

    int len = lexer->position - start;
    char* text = malloc(len + 1);
    strncpy(text, lexer->input + start, len);
    text[len] = '\0';

    // Macros para detectar palabras reservadas
    #define MATCH_KW(word, token_type) \
        if (strcmp(text, word) == 0) { \
            Token t = create_token(token_type, text, line, col); \
            free(text); \
            return t; \
        }

    MATCH_KW("abracadabra", TOKEN_ABRACADABRA)
    MATCH_KW("num",         TOKEN_NUM)
    MATCH_KW("frac",        TOKEN_FRAC)
    MATCH_KW("bool",        TOKEN_BOOL)
    MATCH_KW("char",        TOKEN_CHAR)
    MATCH_KW("void",       TOKEN_VOID)

    MATCH_KW("avanzar",    TOKEN_AVANZAR)
    MATCH_KW("retroceder", TOKEN_RETROCEDER)
    MATCH_KW("girarDer",   TOKEN_GIRAR_DER)
    MATCH_KW("girarIzq",   TOKEN_GIRAR_IZQ)
    MATCH_KW("detener",    TOKEN_DETENER)

    MATCH_KW("acelerar",    TOKEN_FAST)
    MATCH_KW("desacelerar",    TOKEN_SLOW)

    MATCH_KW("iGlobal",    TOKEN_IGLOBAL)
    MATCH_KW("fGlobal",    TOKEN_FGLOBAL)
    MATCH_KW("delay",    TOKEN_DELAY)

    MATCH_KW("iniciarLoop",    TOKEN_INICIAR_LOOP)
    MATCH_KW("sensorDer",    TOKEN_SENSOR_DER)
    MATCH_KW("sensorIzq",    TOKEN_SENSOR_IZQ)
    MATCH_KW("avanzarIndef",    TOKEN_AVANZAR_INDEF)
    
    MATCH_KW("if",          TOKEN_IF)
    MATCH_KW("else",        TOKEN_ELSE)
    MATCH_KW("for",         TOKEN_FOR)
    MATCH_KW("while",       TOKEN_WHILE)
    //MATCH_KW("hablar",      TOKEN_HABLAR)
    MATCH_KW("break",       TOKEN_BREAK)
    MATCH_KW("return",      TOKEN_RETURN)
    MATCH_KW("OR",          TOKEN_OR)
    MATCH_KW("AND",         TOKEN_AND)
    MATCH_KW("NOT",         TOKEN_NOT)
    MATCH_KW("ahuevo",      TOKEN_AHUEVO)
    MATCH_KW("paja",        TOKEN_PAJA)

    // Si no es palabra reservada → identificador
    Token t = create_token(TOKEN_IDENTIFIER, text, line, col);
    free(text);
    return t;
}

// =====================================================
// Leer operadores y símbolos
// =====================================================

static Token read_operator(Lexer* lexer) {
    int line = lexer->line;
    int col  = lexer->column;
    char c = current_char(lexer);
    char n = peek_char(lexer);

    // Operadores dobles
    if (c == '=' && n == '=') {
        advance(lexer);
        advance(lexer);
        return create_token(TOKEN_EQUAL, "==", line, col);
    }

    if (c == '!' && n == '=') {
        advance(lexer);
        advance(lexer);
        return create_token(TOKEN_NOT_EQUAL, "!=", line, col);
    }

    if (c == '<' && n == '=') {
        advance(lexer);
        advance(lexer);
        return create_token(TOKEN_LESS_EQ, "<=", line, col);
    }

    if (c == '>' && n == '=') {
        advance(lexer);
        advance(lexer);
        return create_token(TOKEN_GREATER_EQ, ">=", line, col);
    }

    // Operadores simples
    switch (c) {
        case '=': advance(lexer); return create_token(TOKEN_ASSIGN, "=", line, col);
        case '+': advance(lexer); return create_token(TOKEN_PLUS, "+", line, col);
        case '-': advance(lexer); return create_token(TOKEN_MINUS, "-", line, col);
        case '*': advance(lexer); return create_token(TOKEN_MUL, "*", line, col);
        case '/': advance(lexer); return create_token(TOKEN_DIV, "/", line, col);
        case '%': advance(lexer); return create_token(TOKEN_MOD, "%", line, col);
        case '<': advance(lexer); return create_token(TOKEN_LESS, "<", line, col);
        case '>': advance(lexer); return create_token(TOKEN_GREATER, ">", line, col);

        case '(': advance(lexer); return create_token(TOKEN_LPAREN, "(", line, col);
        case ')': advance(lexer); return create_token(TOKEN_RPAREN, ")", line, col);
        case '{': advance(lexer); return create_token(TOKEN_LBRACE, "{", line, col);
        case '}': advance(lexer); return create_token(TOKEN_RBRACE, "}", line, col);
        case '[': advance(lexer); return create_token(TOKEN_LBRACKET, "[", line, col);
        case ']': advance(lexer); return create_token(TOKEN_RBRACKET, "]", line, col);
        case ';': advance(lexer); return create_token(TOKEN_SEMICOLON, ";", line, col);
        case ',': advance(lexer); return create_token(TOKEN_COMMA, ",", line, col);
    }

    // Caracter desconocido
    char unknown[2] = {c, '\0'};
    advance(lexer);
    return create_token(TOKEN_ERROR, unknown, line, col);
}

// =====================================================
// Función principal del lexer
// =====================================================

Token get_next_token(Lexer* lexer) {

    skip_whitespace_and_comments(lexer);

    if (lexer->position >= lexer->length)
        return create_token(TOKEN_EOF, NULL, lexer->line, lexer->column);

    char c = current_char(lexer);

    if (isdigit(c))
        return read_number(lexer);

    if (c == '\'')
        return read_char_literal(lexer);

    if (isalpha(c) || c == '_')
        return read_identifier_or_keyword(lexer);

    return read_operator(lexer);
}
