// parser.c
#include "parser.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static TokenType peek_type(Parser *p, int lookahead) {
    int saved_pos = p->lexer->position;
    int saved_line = p->lexer->line;
    int saved_col = p->lexer->column;

    Token t = {0};
    TokenType result = TOKEN_ERROR;

    for (int i = 0; i < lookahead; i++) {
        t = get_next_token(p->lexer);
        if (i == lookahead - 1)
            result = t.type;

        free_token(&t);
    }

    p->lexer->position = saved_pos;
    p->lexer->line = saved_line;
    p->lexer->column = saved_col;

    return result;
}


/* -----------------------
   Parser creation / destruction
   ----------------------- */

Parser* create_parser(Lexer *lexer) {
    Parser *p = malloc(sizeof(Parser));
    if (!p) return NULL;

    p->lexer = lexer;          // YA NO CREA LEXER
    p->has_error = 0;
    p->error_message[0] = '\0';

    p->current_token = get_next_token(lexer);
    return p;
}

void free_parser(Parser *parser) {
    if (!parser) return;
    free_token(&parser->current_token);
    if (parser->lexer) free_lexer(parser->lexer);
    free(parser);
}

/* -----------------------
   Helpers: peek token (snapshot lexer state)
   Returns a token copy (caller must free_token on the returned token)
   ----------------------- */
static Token lexer_peek_token(Parser *p) {
    Token tk;
    // Save state
    int saved_pos = p->lexer->position;
    int saved_line = p->lexer->line;
    int saved_col = p->lexer->column;

    // Get next token (this will modify lexer state)
    tk = get_next_token(p->lexer);

    // Restore lexer state
    p->lexer->position = saved_pos;
    p->lexer->line = saved_line;
    p->lexer->column = saved_col;

    return tk;
}

/* -----------------------
   Basic parser utilities
   ----------------------- */

void parser_advance(Parser *parser) {
    free_token(&parser->current_token);
    parser->current_token = get_next_token(parser->lexer);
}

int parser_match(Parser *parser, TokenType expected) {
    if (parser->current_token.type == expected) {
        parser_advance(parser);
        return 1;
    }
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "expected token '%s'", token_type_to_string(expected));
    parser_error(parser, tmp);
    return 0;
}

void parser_error(Parser *parser, const char *message) {
    if (parser->has_error) return; // preserve first error
    parser->has_error = 1;
    snprintf(parser->error_message, sizeof(parser->error_message),
             "Syntax error at %d:%d: %s. Found: %s",
             parser->current_token.line,
             parser->current_token.column,
             message,
             token_type_to_string(parser->current_token.type));
}

void parser_print_error(Parser *parser) {
    if (!parser->has_error) return;

    fprintf(stderr, "%s\n", parser->error_message);

    if (!parser->lexer || !parser->lexer->input) return;

    const char *buf = parser->lexer->input;
    int len = parser->lexer->length;
    int target_line = parser->current_token.line;
    if (target_line < 1) return;

    int pos = 0;
    int cur_line = 1;
    int line_start = 0;
    int line_end = 0;

    while (pos < len && cur_line < target_line) {
        if (buf[pos] == '\n') cur_line++;
        pos++;
    }
    line_start = pos;
    while (pos < len && buf[pos] != '\n') pos++;
    line_end = pos;

    fprintf(stderr, "%4d | ", target_line);
    fwrite(buf + line_start, 1, (line_end - line_start), stderr);
    fprintf(stderr, "\n");

    fprintf(stderr, "     | ");
    int caret = parser->current_token.column - 1;
    if (caret < 0) caret = 0;
    for (int i = 0; i < caret; ++i) {
        char ch = (line_start + i < line_end) ? buf[line_start + i] : ' ';
        if (ch == '\t') fputc('\t', stderr);
        else fputc(' ', stderr);
    }
    fprintf(stderr, "^\n");
}

/* -----------------------
   AST helpers
   ----------------------- */

ASTNode* create_node(NodeType type, const char *value,
                    ASTNode *left, ASTNode *right,
                    int line, int column) {
    ASTNode *node = (ASTNode*)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error: No se pudo asignar memoria para el nodo\n");
        return NULL;
    }

    node->type = type;
    if (value) node->value = strdup(value);
    else node->value = NULL;
    node->left = left;
    node->right = right;
    node->line = line;
    node->column = column;

    return node;
}

void free_ast(ASTNode *node) {
    if (!node) return;
    free_ast(node->left);
    free_ast(node->right);
    if (node->value) free(node->value);
    free(node);
}

static void print_indent(int level) {
    for (int i = 0; i < level; ++i) printf("  ");
}

char* node_type_to_string(NodeType type) {
    switch (type) {
        case NODE_PROGRAM: return "PROGRAM";
        case NODE_STATEMENTS: return "STATEMENTS";
        case NODE_STATEMENT: return "STATEMENT";
        case NODE_BLOCK: return "BLOCK";
        case NODE_DECLARATION: return "DECLARATION";
        case NODE_ASSIGNMENT: return "ASSIGNMENT";
        case NODE_ARRAY_DECLARATION: return "ARRAY_DECL";
        case NODE_ARRAY_ACCESS: return "ARRAY_ACCESS";
        case NODE_IF: return "IF";
        case NODE_ELSE: return "ELSE";
        case NODE_FOR: return "FOR";
        case NODE_WHILE: return "WHILE";
        case NODE_BREAK: return "BREAK";
        case NODE_RETURN: return "RETURN";
        case NODE_FUNCTION_DECL: return "FUNCTION_DECL";
        case NODE_PARAM: return "PARAM";
        case NODE_PARAM_LIST: return "PARAM_LIST";
        case NODE_FUNCTION_CALL: return "FUNCTION_CALL";
        case NODE_ARG_LIST: return "ARG_LIST";
        case NODE_CMD_AVANZAR: return "CMD_AVANZAR";
        case NODE_CMD_RETROCEDER: return "CMD_RETROCEDER";
        case NODE_CMD_GIRAR_DER: return "CMD_GIRAR_DER";
        case NODE_CMD_GIRAR_IZQ: return "CMD_GIRAR_IZQ";
        case NODE_CMD_DETENER: return "CMD_DETENER";
        case NODE_CMD_INICIAR_LOOP: return "CMD_INICIAR_LOOP";
        case NODE_CMD_SENSOR_DER: return "CMD_SENSOR_DER";
        case NODE_CMD_SENSOR_IZQ: return "CMD_SENSOR_IZQ";
        case NODE_CMD_FAST: return "CMD_FAST";
        case NODE_CMD_SLOW: return "CMD_SLOW";
        case NODE_CMD_IGLOBAL: return "CMD_IGLOBAL";
        case NODE_CMD_FGLOBAL: return "CMD_FGLOBAL";
        case NODE_CMD_DELAY: return "CMD_DELAY";
        case NODE_CMD_AVANZAR_INDEF: return "CMD_AVANZAR_INDEF";
        case NODE_LITERAL: return "LITERAL";
        case NODE_IDENTIFIER: return "IDENTIFIER";
        case NODE_BINARY_OP: return "BINOP";
        case NODE_UNARY_OP: return "UNOP";
        default: return "UNKNOWN";
    }
}

void print_ast(ASTNode *node, int level) {
    if (!node) return;
    print_indent(level);
    printf("%s", node_type_to_string(node->type));
    if (node->value) printf(" (%s)", node->value);
    printf(" @%d:%d\n", node->line, node->column);
    if (node->left) print_ast(node->left, level + 1);
    if (node->right) print_ast(node->right, level + 1);
}

/* -----------------------
   Forward declarations (grammar functions)
   ----------------------- */

static ASTNode* parse_statements(Parser *p);
static ASTNode* parse_statement(Parser *p);

static ASTNode* parse_function(Parser *p);
static ASTNode* parse_param_list(Parser *p);
static ASTNode* parse_param(Parser *p);

static ASTNode* parse_declaration(Parser *p);
static ASTNode* parse_assignment(Parser *p);

static ASTNode* parse_if(Parser *p);
static ASTNode* parse_for(Parser *p);
static ASTNode* parse_while(Parser *p);
static ASTNode* parse_break(Parser *p);
static ASTNode* parse_return(Parser *p);

static ASTNode* parse_expr(Parser *p);
static ASTNode* parse_expr_or(Parser *p);
static ASTNode* parse_expr_and(Parser *p);
static ASTNode* parse_expr_equal(Parser *p);
static ASTNode* parse_expr_rel(Parser *p);
static ASTNode* parse_expr_arith(Parser *p);
static ASTNode* parse_term(Parser *p);
static ASTNode* parse_unary(Parser *p);
static ASTNode* parse_factor(Parser *p);

static ASTNode* parse_function_call(Parser *p, const char *fname, int line, int col);
static ASTNode* parse_arg_list(Parser *p);
static ASTNode* parse_command(Parser *p);

/* -----------------------
   Public parse entry
   ----------------------- */

ASTNode* parse(Parser *parser) {
    if (!parser) return NULL;
    if (parser->current_token.type == TOKEN_ERROR) {
        parser_error(parser, parser->current_token.value ? parser->current_token.value : "lexical error");
        parser_print_error(parser);
        return NULL;
    }

    ASTNode *root = parse_program(parser);
    if (parser->has_error) {
        parser_print_error(parser);
        free_ast(root);
        return NULL;
    }

    if (parser->current_token.type != TOKEN_EOF) {
        parser_error(parser, "unexpected tokens after program");
        parser_print_error(parser);
        free_ast(root);
        return NULL;
    }

    return root;
}

/* -----------------------
   Grammar implementation
   ----------------------- */

/*
 <program> ::= "abracadabra" "{" <statements> "}"
*/
ASTNode* parse_program(Parser *p) {
    if (p->current_token.type != TOKEN_ABRACADABRA) {
        parser_error(p, "program must start with 'abracadabra'");
        return NULL;
    }
    int line = p->current_token.line, col = p->current_token.column;
    parser_advance(p);

    if (!parser_match(p, TOKEN_LBRACE)) {
        parser_print_error(p);
        return NULL;
    }

    ASTNode *stmts = parse_statements(p);
    if (p->has_error) { free_ast(stmts); return NULL; }

    if (!parser_match(p, TOKEN_RBRACE)) {
        free_ast(stmts);
        parser_print_error(p);
        return NULL;
    }

    ASTNode *prog = create_node(NODE_PROGRAM, NULL, stmts, NULL, line, col);
    return prog;
}

/*
 <statements> ::= <statement> <statements> | ε
*/
static ASTNode* parse_statements(Parser *p) {
    ASTNode *head = NULL;
    ASTNode *tail = NULL;

    while (p->current_token.type != TOKEN_RBRACE && p->current_token.type != TOKEN_EOF) {
        ASTNode *stmt = parse_statement(p);
        if (p->has_error) { free_ast(head); return NULL; }
        ASTNode *node = create_node(NODE_STATEMENTS, NULL, stmt, NULL, stmt ? stmt->line : p->current_token.line, stmt ? stmt->column : p->current_token.column);
        if (!head) head = node;
        else tail->right = node;
        tail = node;
    }
    return head;
}

/*
 <statement> ::= <declaration> ";"
               | <assignment> ";"
               | <funcion>
               | <if_stmt>
               | <for_stmt>
               | <while_stmt>
               | <break_stmt> ";"
               | <return_stmt> ";"
               | <llamada_funcion> ";"
               | <comando> ";"
               | "{" <sentencias> "}"
*/
static ASTNode* parse_statement(Parser *p) {
    // Block
    if (p->current_token.type == TOKEN_LBRACE) {
        int line = p->current_token.line, col = p->current_token.column;
        parser_advance(p); // consume '{'
        ASTNode *body = parse_statements(p);
        if (p->has_error) { free_ast(body); return NULL; }
        if (!parser_match(p, TOKEN_RBRACE)) { free_ast(body); parser_print_error(p); return NULL; }
        return create_node(NODE_BLOCK, NULL, body, NULL, line, col);
    }

    // Detect function: type IDENT '('
    if (p->current_token.type == TOKEN_NUM ||
        p->current_token.type == TOKEN_FRAC ||
        p->current_token.type == TOKEN_BOOL ||
        p->current_token.type == TOKEN_CHAR ||
        p->current_token.type == TOKEN_VOID)
    {
        if (peek_type(p, 1) == TOKEN_IDENTIFIER &&
            peek_type(p, 2) == TOKEN_LPAREN)
        {
            return parse_function(p);
        }
    }

    // Declaration (start with type)
    if (p->current_token.type == TOKEN_NUM ||
        p->current_token.type == TOKEN_FRAC ||
        p->current_token.type == TOKEN_BOOL ||
        p->current_token.type == TOKEN_CHAR) {
        ASTNode *decl = parse_declaration(p);
        if (p->has_error) { free_ast(decl); return NULL; }
        if (!parser_match(p, TOKEN_SEMICOLON)) { free_ast(decl); parser_print_error(p); return NULL; }
        return decl;
    }

    // If
    if (p->current_token.type == TOKEN_IF) return parse_if(p);

    // For
    if (p->current_token.type == TOKEN_FOR) return parse_for(p);

    // While
    if (p->current_token.type == TOKEN_WHILE) return parse_while(p);

    // Break
    if (p->current_token.type == TOKEN_BREAK) {
        ASTNode *b = parse_break(p);
        if (p->has_error) { free_ast(b); return NULL; }
        if (!parser_match(p, TOKEN_SEMICOLON)) { free_ast(b); parser_print_error(p); return NULL; }
        return b;
    }

    // Return
    if (p->current_token.type == TOKEN_RETURN) {
        ASTNode *r = parse_return(p);
        if (p->has_error) { free_ast(r); return NULL; }
        if (!parser_match(p, TOKEN_SEMICOLON)) { free_ast(r); parser_print_error(p); return NULL; }
        return r;
    }

    // Commands (robot)
    if (p->current_token.type == TOKEN_AVANZAR ||
        p->current_token.type == TOKEN_RETROCEDER ||
        p->current_token.type == TOKEN_GIRAR_DER ||
        p->current_token.type == TOKEN_GIRAR_IZQ ||
        p->current_token.type == TOKEN_DETENER ||
        p->current_token.type == TOKEN_INICIAR_LOOP ||
        p->current_token.type == TOKEN_SENSOR_DER ||
        p->current_token.type == TOKEN_SENSOR_IZQ ||
        p->current_token.type == TOKEN_AVANZAR_INDEF ||
        p->current_token.type == TOKEN_FAST ||
        p->current_token.type == TOKEN_SLOW ||
        p->current_token.type == TOKEN_IGLOBAL ||
        p->current_token.type == TOKEN_FGLOBAL ||
        p->current_token.type == TOKEN_DELAY){
            ASTNode *cmd = parse_command(p);
            if (p->has_error) { free_ast(cmd); return NULL; }
            if (!parser_match(p, TOKEN_SEMICOLON)) { free_ast(cmd); parser_print_error(p); return NULL; }
            return cmd;
    }

    // Assignment or expression starting with identifier (assignment, array access, function call, or variable expr)
    if (p->current_token.type == TOKEN_IDENTIFIER) {
        // Peek to decide
        Token tk = lexer_peek_token(p);
        int is_assign_like = 0;
        if (tk.type == TOKEN_ASSIGN || tk.type == TOKEN_LBRACKET) is_assign_like = 1;
        // Also if '(' then it's a function call expression/statement
        int is_call_like = (tk.type == TOKEN_LPAREN);
        free_token(&tk);

        if (is_assign_like) {
            ASTNode *asgn = parse_assignment(p);
            if (p->has_error) { free_ast(asgn); return NULL; }
            if (!parser_match(p, TOKEN_SEMICOLON)) { free_ast(asgn); parser_print_error(p); return NULL; }
            return asgn;
        } else if (is_call_like) {
            // parse expression (function call) and expect semicolon
            ASTNode *expr = parse_expr(p);
            if (p->has_error) { free_ast(expr); return NULL; }
            if (!parser_match(p, TOKEN_SEMICOLON)) { free_ast(expr); parser_print_error(p); return NULL; }
            return expr;
        } else {
            // expression statement (variable or other expr)
            ASTNode *expr = parse_expr(p);
            if (p->has_error) { free_ast(expr); return NULL; }
            if (!parser_match(p, TOKEN_SEMICOLON)) { free_ast(expr); parser_print_error(p); return NULL; }
            return expr;
        }
    }

    // Fallback: expression starting with literal, unary, or '('
    if (p->current_token.type == TOKEN_INT_LITERAL ||
        p->current_token.type == TOKEN_FLOAT_LITERAL ||
        p->current_token.type == TOKEN_CHAR_LITERAL ||
        p->current_token.type == TOKEN_AHUEVO ||
        p->current_token.type == TOKEN_PAJA ||
        p->current_token.type == TOKEN_MINUS ||
        p->current_token.type == TOKEN_NOT ||
        p->current_token.type == TOKEN_LPAREN) {
        ASTNode *expr = parse_expr(p);
        if (p->has_error) { free_ast(expr); return NULL; }
        if (!parser_match(p, TOKEN_SEMICOLON)) { free_ast(expr); parser_print_error(p); return NULL; }
        return expr;
    }

    // If none matched: syntax error
    parser_error(p, "unexpected token at start of statement");
    return NULL;
}

/* -----------------------
   Declarations & Assignments
   ----------------------- */

static ASTNode* parse_declaration(Parser *p) {
    TokenType ttype = p->current_token.type;
    char type_name[32] = {0};
    strcpy(type_name, token_type_to_string(ttype));
    int line = p->current_token.line, col = p->current_token.column;
    parser_advance(p); // consume type

    if (p->current_token.type != TOKEN_IDENTIFIER) {
        parser_error(p, "expected identifier after type in declaration");
        return NULL;
    }

    char *ident = strdup(p->current_token.value);
    int idline = p->current_token.line, idcol = p->current_token.column;
    parser_advance(p); // consume identifier

    ASTNode *size_node = NULL;
    ASTNode *init_node = NULL;

    if (p->current_token.type == TOKEN_LBRACKET) {
        parser_advance(p); // '['
        size_node = parse_expr(p);
        if (p->has_error) { free(ident); return NULL; }
        if (!parser_match(p, TOKEN_RBRACKET)) { free(ident); free_ast(size_node); parser_print_error(p); return NULL; }
        if (p->current_token.type == TOKEN_ASSIGN) {
            parser_advance(p);
            init_node = parse_expr(p);
            if (p->has_error) { free(ident); free_ast(size_node); return NULL; }
        }
    } else if (p->current_token.type == TOKEN_ASSIGN) {
        parser_advance(p);
        init_node = parse_expr(p);
        if (p->has_error) { free(ident); return NULL; }
    }

    ASTNode *idnode = create_node(NODE_IDENTIFIER, ident, NULL, NULL, idline, idcol);
    ASTNode *decl = create_node(NODE_DECLARATION, type_name, idnode, NULL, line, col);
    if (size_node) {
        decl->right = size_node;
        if (init_node) size_node->right = init_node;
    } else {
        decl->right = init_node;
    }

    free(ident);
    return decl;
}

static ASTNode* parse_assignment(Parser *p) {
    if (p->current_token.type != TOKEN_IDENTIFIER) {
        parser_error(p, "assignment must start with identifier");
        return NULL;
    }
    char *ident = strdup(p->current_token.value);
    int line = p->current_token.line, col = p->current_token.column;
    parser_advance(p); // consume identifier

    ASTNode *lhs = create_node(NODE_IDENTIFIER, ident, NULL, NULL, line, col);

    if (p->current_token.type == TOKEN_LBRACKET) {
        parser_advance(p); // '['
        ASTNode *index = parse_expr(p);
        if (p->has_error) { free(ident); free_ast(lhs); return NULL; }
        if (!parser_match(p, TOKEN_RBRACKET)) { free(ident); free_ast(lhs); free_ast(index); parser_print_error(p); return NULL; }
        ASTNode *access = create_node(NODE_ARRAY_ACCESS, NULL, lhs, index, line, col);
        lhs = access;
    }

    if (!parser_match(p, TOKEN_ASSIGN)) {
        free(ident); free_ast(lhs); parser_print_error(p); return NULL;
    }

    ASTNode *rhs = parse_expr(p);
    if (p->has_error) { free(ident); free_ast(lhs); return NULL; }

    ASTNode *assign = create_node(NODE_ASSIGNMENT, NULL, lhs, rhs, line, col);
    free(ident);
    return assign;
}

/* -----------------------
   Control flow: if / for / while / break / return
   ----------------------- */

static ASTNode* parse_if(Parser *p) {
    int line = p->current_token.line, col = p->current_token.column;
    parser_advance(p); // consume 'if'

    if (!parser_match(p, TOKEN_LPAREN)) { parser_print_error(p); return NULL; }
    ASTNode *cond = parse_expr_or(p);
    if (p->has_error) return NULL;
    if (!parser_match(p, TOKEN_RPAREN)) { free_ast(cond); parser_print_error(p); return NULL; }

    if (!parser_match(p, TOKEN_LBRACE)) { free_ast(cond); parser_print_error(p); return NULL; }
    ASTNode *then_block = parse_statements(p);
    if (p->has_error) { free_ast(cond); free_ast(then_block); return NULL; }
    if (!parser_match(p, TOKEN_RBRACE)) { free_ast(cond); free_ast(then_block); parser_print_error(p); return NULL; }

    ASTNode *else_block = NULL;
    if (p->current_token.type == TOKEN_ELSE) {
        parser_advance(p); // consume else
        if (!parser_match(p, TOKEN_LBRACE)) { free_ast(cond); free_ast(then_block); parser_print_error(p); return NULL; }
        else_block = parse_statements(p);
        if (p->has_error) { free_ast(cond); free_ast(then_block); free_ast(else_block); return NULL; }
        if (!parser_match(p, TOKEN_RBRACE)) { free_ast(cond); free_ast(then_block); free_ast(else_block); parser_print_error(p); return NULL; }
    }

    ASTNode *branch = create_node(NODE_IF, NULL, then_block, else_block, line, col);
    ASTNode *ifnode = create_node(NODE_IF, NULL, cond, branch, line, col);
    return ifnode;
}

static ASTNode* parse_for(Parser *p) {
    int line = p->current_token.line, col = p->current_token.column;
    parser_advance(p); // consume 'for'
    if (!parser_match(p, TOKEN_LPAREN)) { parser_print_error(p); return NULL; }

    ASTNode *init = NULL;
    if (p->current_token.type == TOKEN_NUM || p->current_token.type == TOKEN_FRAC ||
        p->current_token.type == TOKEN_BOOL || p->current_token.type == TOKEN_CHAR) {
        init = parse_declaration(p);
    } else if (p->current_token.type == TOKEN_IDENTIFIER) {
        Token tk = lexer_peek_token(p);
        int is_assign = (tk.type == TOKEN_ASSIGN || tk.type == TOKEN_LBRACKET);
        free_token(&tk);
        if (is_assign) init = parse_assignment(p);
    }

    if (!parser_match(p, TOKEN_SEMICOLON)) { free_ast(init); parser_print_error(p); return NULL; }

    ASTNode *cond = NULL;
    if (p->current_token.type != TOKEN_SEMICOLON) cond = parse_expr_or(p);

    if (!parser_match(p, TOKEN_SEMICOLON)) { free_ast(init); free_ast(cond); parser_print_error(p); return NULL; }

    ASTNode *update = NULL;
    if (p->current_token.type != TOKEN_RPAREN) {
        if (p->current_token.type == TOKEN_IDENTIFIER)
            update = parse_assignment(p);
        else
            update = parse_expr_or(p);
    }

    if (!parser_match(p, TOKEN_RPAREN)) { free_ast(init); free_ast(cond); free_ast(update); parser_print_error(p); return NULL; }
    if (!parser_match(p, TOKEN_LBRACE)) { free_ast(init); free_ast(cond); free_ast(update); parser_print_error(p); return NULL; }

    ASTNode *body = parse_statements(p);
    if (p->has_error) { free_ast(init); free_ast(cond); free_ast(update); free_ast(body); return NULL; }
    if (!parser_match(p, TOKEN_RBRACE)) { free_ast(init); free_ast(cond); free_ast(update); free_ast(body); parser_print_error(p); return NULL; }

    ASTNode *blocknode = create_node(NODE_BLOCK, NULL, body, update, line, col);
    ASTNode *inner = create_node(NODE_FOR, NULL, cond, blocknode, line, col);
    ASTNode *fornode = create_node(NODE_FOR, NULL, init, inner, line, col);
    return fornode;
}

static ASTNode* parse_while(Parser *p) {
    int line = p->current_token.line, col = p->current_token.column;
    parser_advance(p); // consume 'while'
    if (!parser_match(p, TOKEN_LPAREN)) { parser_print_error(p); return NULL; }
    ASTNode *cond = parse_expr_or(p);
    if (p->has_error) return NULL;
    if (!parser_match(p, TOKEN_RPAREN)) { free_ast(cond); parser_print_error(p); return NULL; }
    if (!parser_match(p, TOKEN_LBRACE)) { free_ast(cond); parser_print_error(p); return NULL; }
    ASTNode *body = parse_statements(p);
    if (p->has_error) { free_ast(cond); free_ast(body); return NULL; }
    if (!parser_match(p, TOKEN_RBRACE)) { free_ast(cond); free_ast(body); parser_print_error(p); return NULL; }
    ASTNode *wn = create_node(NODE_WHILE, NULL, cond, body, line, col);
    return wn;
}

static ASTNode* parse_break(Parser *p) {
    int line = p->current_token.line, col = p->current_token.column;
    parser_advance(p);
    return create_node(NODE_BREAK, NULL, NULL, NULL, line, col);
}

static ASTNode* parse_return(Parser *p) {
    int line = p->current_token.line, col = p->current_token.column;
    parser_advance(p); // consume return
    // return may be empty (epsilon) per grammar
    if (p->current_token.type == TOKEN_SEMICOLON || p->current_token.type == TOKEN_RBRACE) {
        return create_node(NODE_RETURN, NULL, NULL, NULL, line, col);
    }
    ASTNode *expr = parse_expr_or(p);
    if (p->has_error) { free_ast(expr); return NULL; }
    ASTNode *rn = create_node(NODE_RETURN, NULL, expr, NULL, line, col);
    return rn;
}

/* -----------------------
   Functions and params
   ----------------------- */

static ASTNode* parse_function(Parser *p) {
    // tipo_retorno (token actual)
    char return_type[32] = {0};
    strcpy(return_type, token_type_to_string(p->current_token.type));
    int line = p->current_token.line, col = p->current_token.column;
    parser_advance(p);

    // nombre
    if (p->current_token.type != TOKEN_IDENTIFIER) {
        parser_error(p, "expected function name");
        return NULL;
    }

    char *fname = strdup(p->current_token.value);
    parser_advance(p);

    if (!parser_match(p, TOKEN_LPAREN)) { free(fname); return NULL; }

    ASTNode *params = NULL;
    if (p->current_token.type != TOKEN_RPAREN)
        params = parse_param_list(p);

    if (!parser_match(p, TOKEN_RPAREN)) { free(fname); free_ast(params); return NULL; }
    if (!parser_match(p, TOKEN_LBRACE)) { free(fname); free_ast(params); return NULL; }

    ASTNode *body = parse_statements(p);
    if (p->has_error) { free(fname); free_ast(params); free_ast(body); return NULL; }

    if (!parser_match(p, TOKEN_RBRACE)) { free(fname); free_ast(params); free_ast(body); return NULL; }

    // Create function node: value = function name, left = params, right = body
    ASTNode *fn = create_node(NODE_FUNCTION_DECL, fname, params, body, line, col);

    // Store return type in the BODY node's value (design B)
    if (fn->right) {
        ASTNode *bodyNode = fn->right;
        if (bodyNode->value) free(bodyNode->value);
        bodyNode->value = strdup(return_type);
    } else {
        // if body somehow missing, still allocate it and set return type
        ASTNode *dummy = create_node(NODE_BLOCK, strdup(return_type), NULL, NULL, line, col);
        fn->right = dummy;
    }

    free(fname);
    return fn;
}

static ASTNode* parse_param_list(Parser *p) {
    ASTNode *first = parse_param(p);
    if (p->has_error) return NULL;

    ASTNode *head = create_node(NODE_PARAM_LIST, NULL, first, NULL, first->line, first->column);
    ASTNode *tail = head;

    while (p->current_token.type == TOKEN_COMMA) {
        parser_advance(p); // consume comma
        ASTNode *next = parse_param(p);
        ASTNode *node = create_node(NODE_PARAM_LIST, NULL, next, NULL, next->line, next->column);
        tail->right = node;
        tail = node;
    }
    return head;
}

static ASTNode* parse_param(Parser *p) {
    if (!(p->current_token.type == TOKEN_NUM ||
          p->current_token.type == TOKEN_FRAC ||
          p->current_token.type == TOKEN_BOOL ||
          p->current_token.type == TOKEN_CHAR)) {
        parser_error(p, "expected type in parameter");
        return NULL;
    }

    char typename[32];
    strcpy(typename, token_type_to_string(p->current_token.type));
    int line = p->current_token.line, col = p->current_token.column;
    parser_advance(p);

    if (p->current_token.type != TOKEN_IDENTIFIER) {
        parser_error(p, "expected identifier for parameter");
        return NULL;
    }

    char *name = strdup(p->current_token.value);
    parser_advance(p);

    ASTNode *var = create_node(NODE_IDENTIFIER, name, NULL, NULL, line, col);
    ASTNode *param = create_node(NODE_PARAM, typename, var, NULL, line, col);
    free(name);
    return param;
}

/* -----------------------
   Expression parsing (precedence)
   ----------------------- */

static ASTNode* parse_expr(Parser *p) {
    return parse_expr_or(p);
}

static ASTNode* parse_expr_or(Parser *p) {
    ASTNode *left = parse_expr_and(p);
    while (p->current_token.type == TOKEN_OR) {
        int line = p->current_token.line, col = p->current_token.column;
        parser_advance(p);
        ASTNode *right = parse_expr_and(p);
        left = create_node(NODE_BINARY_OP, "OR", left, right, line, col);
    }
    return left;
}

static ASTNode* parse_expr_and(Parser *p) {
    ASTNode *left = parse_expr_equal(p);
    while (p->current_token.type == TOKEN_AND) {
        int line = p->current_token.line, col = p->current_token.column;
        parser_advance(p);
        ASTNode *right = parse_expr_equal(p);
        left = create_node(NODE_BINARY_OP, "AND", left, right, line, col);
    }
    return left;
}

static ASTNode* parse_expr_equal(Parser *p) {
    ASTNode *left = parse_expr_rel(p);
    while (p->current_token.type == TOKEN_EQUAL || p->current_token.type == TOKEN_NOT_EQUAL) {
        int line = p->current_token.line, col = p->current_token.column;
        if (p->current_token.type == TOKEN_EQUAL) {
            parser_advance(p);
            ASTNode *right = parse_expr_rel(p);
            left = create_node(NODE_BINARY_OP, "==", left, right, line, col);
        } else {
            parser_advance(p);
            ASTNode *right = parse_expr_rel(p);
            left = create_node(NODE_BINARY_OP, "!=", left, right, line, col);
        }
    }
    return left;
}

static ASTNode* parse_expr_rel(Parser *p) {
    ASTNode *left = parse_expr_arith(p);
    while (p->current_token.type == TOKEN_LESS || p->current_token.type == TOKEN_GREATER ||
           p->current_token.type == TOKEN_LESS_EQ || p->current_token.type == TOKEN_GREATER_EQ) {
        int line = p->current_token.line, col = p->current_token.column;
        char opbuf[8];
        if (p->current_token.type == TOKEN_LESS) strcpy(opbuf, "<");
        else if (p->current_token.type == TOKEN_GREATER) strcpy(opbuf, ">");
        else if (p->current_token.type == TOKEN_LESS_EQ) strcpy(opbuf, "<=");
        else strcpy(opbuf, ">=");
        parser_advance(p);
        ASTNode *right = parse_expr_arith(p);
        left = create_node(NODE_BINARY_OP, opbuf, left, right, line, col);
    }
    return left;
}

static ASTNode* parse_expr_arith(Parser *p) {
    ASTNode *left = parse_term(p);
    while (p->current_token.type == TOKEN_PLUS || p->current_token.type == TOKEN_MINUS) {
        int line = p->current_token.line, col = p->current_token.column;
        if (p->current_token.type == TOKEN_PLUS) {
            parser_advance(p);
            ASTNode *right = parse_term(p);
            left = create_node(NODE_BINARY_OP, "+", left, right, line, col);
        } else {
            parser_advance(p);
            ASTNode *right = parse_term(p);
            left = create_node(NODE_BINARY_OP, "-", left, right, line, col);
        }
    }
    return left;
}

static ASTNode* parse_term(Parser *p) {
    ASTNode *left = parse_unary(p);
    while (p->current_token.type == TOKEN_MUL || p->current_token.type == TOKEN_DIV || p->current_token.type == TOKEN_MOD) {
        int line = p->current_token.line, col = p->current_token.column;
        if (p->current_token.type == TOKEN_MUL) {
            parser_advance(p);
            ASTNode *right = parse_unary(p);
            left = create_node(NODE_BINARY_OP, "*", left, right, line, col);
        } else if (p->current_token.type == TOKEN_DIV) {
            parser_advance(p);
            ASTNode *right = parse_unary(p);
            left = create_node(NODE_BINARY_OP, "/", left, right, line, col);
        } else {
            parser_advance(p);
            ASTNode *right = parse_unary(p);
            left = create_node(NODE_BINARY_OP, "%", left, right, line, col);
        }
    }
    return left;
}

static ASTNode* parse_unary(Parser *p) {
    if (p->current_token.type == TOKEN_MINUS) {
        int line = p->current_token.line, col = p->current_token.column;
        parser_advance(p);
        ASTNode *operand = parse_unary(p);
        return create_node(NODE_UNARY_OP, "-", operand, NULL, line, col);
    }
    if (p->current_token.type == TOKEN_NOT) {
        int line = p->current_token.line, col = p->current_token.column;
        parser_advance(p);
        ASTNode *operand = parse_unary(p);
        return create_node(NODE_UNARY_OP, "NOT", operand, NULL, line, col);
    }
    return parse_factor(p);
}

/* -----------------------
   Factor: literals, identifiers, array access, function calls, parentheses, commands
   ----------------------- */

static ASTNode* parse_factor(Parser *p) {
    // Literals
    if (p->current_token.type == TOKEN_INT_LITERAL ||
        p->current_token.type == TOKEN_FLOAT_LITERAL ||
        p->current_token.type == TOKEN_CHAR_LITERAL ||
        p->current_token.type == TOKEN_AHUEVO ||
        p->current_token.type == TOKEN_PAJA) {
        char *val = p->current_token.value ? strdup(p->current_token.value) : strdup(token_type_to_string(p->current_token.type));
        ASTNode *lit = create_node(NODE_LITERAL, val, NULL, NULL, p->current_token.line, p->current_token.column);
        free(val);
        parser_advance(p);
        return lit;
    }

    // Identifier: could be identifier, array access, or function call
    if (p->current_token.type == TOKEN_IDENTIFIER) {
        char *name = strdup(p->current_token.value);
        int line = p->current_token.line, col = p->current_token.column;
        parser_advance(p);

        // function call: '('
        if (p->current_token.type == TOKEN_LPAREN) {
            ASTNode *call = parse_function_call(p, name, line, col);
            free(name);
            return call;
        }

        // array access
        if (p->current_token.type == TOKEN_LBRACKET) {
            parser_advance(p); // '['
            ASTNode *index = parse_expr(p);
            if (p->has_error) { free(name); return NULL; }
            if (!parser_match(p, TOKEN_RBRACKET)) { free(name); free_ast(index); parser_print_error(p); return NULL; }
            ASTNode *idnode = create_node(NODE_IDENTIFIER, name, NULL, NULL, line, col);
            ASTNode *acc = create_node(NODE_ARRAY_ACCESS, NULL, idnode, index, line, col);
            free(name);
            return acc;
        }

        ASTNode *idnode = create_node(NODE_IDENTIFIER, name, NULL, NULL, line, col);
        free(name);
        return idnode;
    }

    // Parenthesized expression
    if (p->current_token.type == TOKEN_LPAREN) {
        parser_advance(p); // '('
        ASTNode *inner = parse_expr_or(p);
        if (p->has_error) { free_ast(inner); return NULL; }
        if (!parser_match(p, TOKEN_RPAREN)) { free_ast(inner); parser_print_error(p); return NULL; }
        return inner;
    }

    parser_error(p, "expected literal, identifier or '(' in expression");
    return NULL;
}

/* -----------------------
   Function call and argument list parsing
   parse_function_call expects that the identifier has already been consumed,
   and current token is '('.
   AST:
     NODE_FUNCTION_CALL (value = function name)
       left = ARG_LIST (or NULL)
   ARG_LIST is a linked list where left holds argument expr and right points to next.
   ----------------------- */

static ASTNode* parse_function_call(Parser *p, const char *fname, int line, int col) {
    // current token should be '('
    if (!parser_match(p, TOKEN_LPAREN)) {
        parser_error(p, "expected '(' after function name");
        return NULL;
    }

    ASTNode *args = NULL;
    if (p->current_token.type != TOKEN_RPAREN) {
        args = parse_arg_list(p);
        if (p->has_error) { free_ast(args); return NULL; }
    }

    if (!parser_match(p, TOKEN_RPAREN)) { free_ast(args); parser_print_error(p); return NULL; }

    ASTNode *call = create_node(NODE_FUNCTION_CALL, fname, args, NULL, line, col);
    return call;
}

static ASTNode* parse_arg_list(Parser *p) {
    ASTNode *first = parse_expr(p);
    if (p->has_error) return NULL;
    ASTNode *head = create_node(NODE_ARG_LIST, NULL, first, NULL, first->line, first->column);
    ASTNode *tail = head;

    while (p->current_token.type == TOKEN_COMMA) {
        parser_advance(p); // consume comma
        ASTNode *next = parse_expr(p);
        if (p->has_error) { free_ast(head); return NULL; }
        ASTNode *node = create_node(NODE_ARG_LIST, NULL, next, NULL, next->line, next->column);
        tail->right = node;
        tail = node;
    }
    return head;
}

/* -----------------------
   Commands parsing
   Each command is a statement and will produce a NODE_CMD_* with no children.
   ----------------------- */

   static ASTNode* parse_command(Parser *p) {
    int line = p->current_token.line;
    int col  = p->current_token.column;

    NodeType type;

    switch (p->current_token.type) {
        case TOKEN_AVANZAR:     type = NODE_CMD_AVANZAR; break;
        case TOKEN_RETROCEDER:  type = NODE_CMD_RETROCEDER; break;
        case TOKEN_GIRAR_DER:   type = NODE_CMD_GIRAR_DER; break;
        case TOKEN_GIRAR_IZQ:   type = NODE_CMD_GIRAR_IZQ; break;
        case TOKEN_DETENER:     type = NODE_CMD_DETENER; break;
        case TOKEN_INICIAR_LOOP: type = NODE_CMD_INICIAR_LOOP; break;
        case TOKEN_SENSOR_DER: type = NODE_CMD_SENSOR_DER; break;
        case TOKEN_SENSOR_IZQ: type = NODE_CMD_SENSOR_IZQ; break;
        case TOKEN_AVANZAR_INDEF: type = NODE_CMD_AVANZAR_INDEF; break;
        case TOKEN_FAST: type = NODE_CMD_FAST; break;
        case TOKEN_SLOW: type = NODE_CMD_SLOW; break;
        case TOKEN_IGLOBAL: type = NODE_CMD_IGLOBAL; break;
        case TOKEN_FGLOBAL: type = NODE_CMD_FGLOBAL; break;
        case TOKEN_DELAY: type = NODE_CMD_DELAY; break;

        default:
            parser_error(p, "comando inválido");
            return NULL;
    }

    parser_advance(p); // consumir palabra reservada

    // Nombre literal que quieres que pase al IR:
    return create_node(type, p->current_token.value, NULL, NULL, line, col);
}


