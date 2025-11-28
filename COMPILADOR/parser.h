#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

// =====================================================
// AST Node Types
// =====================================================
typedef enum {
    // Program structure
    NODE_PROGRAM,
    NODE_STATEMENTS,
    NODE_STATEMENT,
    NODE_BLOCK,

    // Declarations & Assignments
    NODE_DECLARATION,
    NODE_ASSIGNMENT,
    NODE_ARRAY_DECLARATION,
    NODE_ARRAY_ACCESS,

    // Control flow
    NODE_IF,
    NODE_ELSE,
    NODE_FOR,
    NODE_WHILE,
    NODE_BREAK,
    NODE_RETURN,

    // Functions
    NODE_FUNCTION_DECL,   // Nuevo
    NODE_PARAM,           // Nuevo
    NODE_PARAM_LIST,      // Nuevo
    NODE_FUNCTION_CALL,   // Nuevo
    NODE_ARG_LIST,        // Nuevo

    // Robot commands
    NODE_CMD_AVANZAR,     // Nuevo
    NODE_CMD_RETROCEDER,  // Nuevo
    NODE_CMD_GIRAR_DER,   // Nuevo
    NODE_CMD_GIRAR_IZQ,   // Nuevo
    NODE_CMD_DETENER,     // Nuevo
    NODE_CMD_INICIAR_LOOP,
    NODE_CMD_SENSOR_DER,
    NODE_CMD_SENSOR_IZQ,
    NODE_CMD_AVANZAR_INDEF,
    NODE_CMD_FAST,
    NODE_CMD_SLOW,
    NODE_CMD_IGLOBAL,
    NODE_CMD_FGLOBAL,
    NODE_CMD_DELAY,

    // Expressions
    NODE_LITERAL,
    NODE_IDENTIFIER,
    NODE_BINARY_OP,
    NODE_UNARY_OP

} NodeType;

// =====================================================
// AST Node Structure
// =====================================================
typedef struct ASTNode {

    NodeType type;
    char *value;                   // operator, literal, identifier, type name, etc.

    struct ASTNode *left;          // left child
    struct ASTNode *right;         // right child

    int line;
    int column;

} ASTNode;

// =====================================================
// Parser Structure
// =====================================================
typedef struct {

    Lexer *lexer;
    Token current_token;

    int has_error;
    char error_message[256];

} Parser;

// =====================================================
// Parser creation & destruction
// =====================================================
Parser* create_parser(Lexer *lexer);
void free_parser(Parser *parser);
ASTNode* parse_program(Parser *parser);

// Main entry
ASTNode* parse(Parser *parser);

// =====================================================
// AST Utilities
// =====================================================
ASTNode* create_node(NodeType type, const char *value,
                     ASTNode *left, ASTNode *right,
                     int line, int column);

void free_ast(ASTNode *node);
void print_ast(ASTNode *node, int level);
char* node_type_to_string(NodeType type);

// =====================================================
// Parser Auxiliary Functions
// =====================================================
void parser_advance(Parser *parser);
int parser_match(Parser *parser, TokenType expected);
void parser_error(Parser *parser, const char *msg);
void parser_print_error(Parser *parser);

#endif // PARSER_H
