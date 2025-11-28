#ifndef LEXER_H
#define LEXER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// =============================
// TIPOS DE TOKENS
// =============================
typedef enum {
    // Identificadores y literales
    TOKEN_IDENTIFIER,
    TOKEN_INT_LITERAL, //
    TOKEN_FLOAT_LITERAL, //
    TOKEN_CHAR_LITERAL, //
    TOKEN_BOOL_LITERAL,

    // Palabras reservadas
    TOKEN_ABRACADABRA,
    TOKEN_NUM,
    TOKEN_FRAC,
    TOKEN_BOOL,
    TOKEN_CHAR,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_FOR,
    TOKEN_WHILE,
    //TOKEN_HABLAR,
    TOKEN_BREAK,
    TOKEN_RETURN,
    TOKEN_OR,
    TOKEN_AND,
    TOKEN_NOT,
    TOKEN_AHUEVO,
    TOKEN_PAJA,

    // Operadores
    TOKEN_ASSIGN, // = //
    TOKEN_EQUAL, // == //
    TOKEN_NOT_EQUAL, // != //
    TOKEN_LESS, // < //
    TOKEN_GREATER, // > //
    TOKEN_LESS_EQ, // <= //
    TOKEN_GREATER_EQ, // >= //
    TOKEN_PLUS, // + //
    TOKEN_MINUS, // - //
    TOKEN_MUL, // * //
    TOKEN_DIV, // / //
    TOKEN_MOD, // % //

    // Símbolos
    TOKEN_LPAREN, // ( //
    TOKEN_RPAREN, // ) //
    TOKEN_LBRACE, // { //
    TOKEN_RBRACE, // } //
    TOKEN_LBRACKET, // [ //
    TOKEN_RBRACKET, // ] //
    TOKEN_SEMICOLON, // ; //
    TOKEN_COMMA, // , //

    TOKEN_VOID,         // nuevo: palabra clave void

    // Comandos del robot
    TOKEN_AVANZAR,
    TOKEN_RETROCEDER,
    TOKEN_GIRAR_DER,
    TOKEN_GIRAR_IZQ,
    TOKEN_DETENER,
    TOKEN_INICIAR_LOOP,
    TOKEN_SENSOR_DER,
    TOKEN_SENSOR_IZQ,
    TOKEN_AVANZAR_INDEF,

    TOKEN_FAST,
    TOKEN_SLOW,

    TOKEN_IGLOBAL,
    TOKEN_FGLOBAL,
    TOKEN_DELAY,

    // Otros
    TOKEN_EOF,
    TOKEN_ERROR
} TokenType;

// =============================
// ESTRUCTURA DEL TOKEN
// =============================
typedef struct {
    TokenType type;
    char* value;
    int line;
    int column;
} Token;


// =============================
// ESTRUCTURA DEL LEXER
// =============================
typedef struct {
    char* input;
    int position;
    int line;
    int column;
    int length;
} Lexer;

// =============================
// FUNCIONES PÚBLICAS
// =============================
Lexer* create_lexer(const char* input);
void free_lexer(Lexer* lexer);

Token get_next_token(Lexer* lexer);
void free_token(Token* token);

char* token_type_to_string(TokenType type);
void print_token(Token* token);

// =============================
// FUNCIONES INTERNAS (prototipos complementarios)
// =============================
void skip_whitespace_and_comments(Lexer *lexer);
Token create_token(TokenType type, const char* value, int line, int column);
Token read_identifier_or_keyword(Lexer* lexer);
Token read_number(Lexer* lexer);
Token read_char_literal(Lexer* lexer);

#endif