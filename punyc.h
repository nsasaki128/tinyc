#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
// tokenize.c
//

// Token
typedef enum {
  TK_RESERVED, // Keyworeds or punctuators
  TK_IDENT,    // Identifiers
  TK_NUM,      // Integer literals
  TK_EOF,      // End-of-file markers
} TokenKind;

// Token type
typedef struct Token Token;
struct Token {
  TokenKind kind; // Token kind
  Token *next;    // Next token
  long val;       // If kind is TK_NUM, its value
  char *loc;      // Token location
  int len;        // Token length
};

void error(char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
bool equal(Token *tok, char *s);
Token *skip(Token *tok, char *s);
Token *tokenize(char *p);

//
// parse.c
//

// Local variable
typedef struct Var Var;
struct Var {
  Var *next;
  char *name; // Variable name
  int offset; // Offset from RBP
};

// AST node
typedef enum {
  ND_ADD,       // +
  ND_SUB,       // -
  ND_MUL,       // *
  ND_DIV,       // /
  ND_EQ,        // ==
  ND_NE,        // !=
  ND_LT,        // <
  ND_LE,        // <=
  ND_ASSIGN,    // =
  ND_RETURN,    // "return"
  ND_IF,        // "if"
  ND_FOR,       // "for" or "while"
  ND_EXPR_STMT, // Expression statement
  ND_VAR,       // Variable
  ND_NUM,       // Integer
} NodeKind;

// AST node type
typedef struct Node Node;
struct Node {
  NodeKind kind; // Node kind
  Node *next;    // Next node
  Node *lhs;     // Left-hand side
  // "if" or "for" statement
  Node *cond;
  Node *then;
  Node *els;
  Node *init;
  Node *inc;
  Node *rhs;     // Right-hand side
  Var *var;      // Used if kind == ND_VAR
  long val;      // Used if kind == ND_NUM
};

typedef struct Function Function;
struct Function {
  Node *node;
  Var *locals;
  int stack_size;
};

Function *parse(Token *tok);

//
// codegen.c
//

void codegen(Function *prog);