#include "punyc.h"

typedef struct Macro Macro;
struct Macro {
  Macro *next;
  char *name;
  Token *body;
};

// `#if` can be nested, so we use a stack to manane nested `#if`s.
typedef struct CondIncl CondIncl;
struct CondIncl {
  CondIncl *next;
  enum { IN_THEN, IN_ELSE, IN_ELIF } ctx;
  Token *tok;
  bool included;
};

static int file_no;
static Macro *macros;
static CondIncl *cond_incl;

static Token *read_file2(char *path);

// Returns the contents of a given file.
static char *read_file_string(char *path) {
  // By convention, read from stdin if a given filename is "-".
  FILE *fp = stdin;
  if (strcmp(path, "-")) {
    fp = fopen(path, "r");
    if (!fp)
      return NULL;
  }

  int buflen = 4096;
  int nread = 0;
  char *buf = malloc(buflen);

  // Read the entire file.
  for (;;) {
    int end = buflen - 2; // extra 2 bytes for the trailing "\n\0"
    int n = fread(buf + nread, 1, end - nread, fp);
    if (n == 0)
      break;
    nread += n;
    if (nread == end) {
      buflen *= 2;
      buf = realloc(buf, buflen);
    }
  }

  if (fp != stdin)
    fclose(fp);

  // Canonicalize the last line by appending "\n"
  // if it does not end with a newline.
  if (nread == 0 || buf[nread - 1] != '\n')
    buf[nread++] = '\n';
  buf[nread] = '\0';

  // Emit a .file directive for the assembler.
  if (!preprocess_only)
    printf(".file %d \"%s\"\n", ++file_no, path);
  return buf;
}

static bool is_hash(Token *tok) {
  return tok->at_bol && equal(tok, "#");
}

// Some preprocessor directives suc as #include allow extraneous
// tokens before newline. This function skips such tokens.
static Token *skip_line(Token *tok) {
  if (tok->at_bol)
    return tok;
  warn_tok(tok, "extra token");
  while (tok->at_bol)
    tok = tok->next;
  return tok;
}

static Token *copy_token(Token *tok) {
  Token *t = malloc(sizeof(Token));
  *t = *tok;
  t->next = NULL;
  t->ty = NULL;
  return t;
}

static Token *new_eof(Token *tok) {
  Token *t = copy_token(tok);
  t->kind = TK_EOF;
  t->len = 0;
  return t;
}

// Append tok2 to the end fo tok1.
static Token *append(Token *tok1, Token *tok2) {
  if (!tok1 || tok1->kind == TK_EOF)
    return tok2;

  Token head = {};
  Token *cur = &head;

  for(; tok1 && tok1->kind != TK_EOF; tok1 = tok1->next)
    cur = cur->next =copy_token(tok1);
  cur->next = tok2;
  return head.next;
}

static Token *skip_cond_incl2(Token *tok) {
  while (tok->kind != TK_EOF) {
    if (is_hash(tok) && equal(tok->next, "if")) {
      tok = skip_cond_incl2(tok->next->next);
      continue;
    }
    if (is_hash(tok) && equal(tok->next, "endif"))
      return tok->next->next;
    tok = tok->next;
  }
  return tok;
}

// Skip until next `#else`, `#elif`, or `#endif`.
// Nested `#if` and `#endif` are skipped.
static Token *skip_cond_incl(Token *tok) {
  while (tok->kind != TK_EOF) {
    if (is_hash(tok) && equal(tok->next, "if")) {
      tok = skip_cond_incl2(tok->next->next);
      continue;
    }

    if (is_hash(tok) &&
        equal(tok->next, "else") || equal(tok->next, "elif") ||
        equal(tok->next, "endif"))
      break;
    tok = tok->next;
  }
  return tok;
}

// Copy all tokens until the next newline, terminate them with
// an EOF token and then returns them. This function is used to
// create a new list of tokens for `#if` arguments.
static Token *copy_line(Token **rest, Token *tok) {
  Token head = {};
  Token *cur = &head;

  for (; !tok->at_bol; tok = tok->next)
    cur = cur->next = copy_token(tok);

  cur->next = new_eof(tok);
  *rest = tok;
  return head.next;
}

// Read and evaluate a constant expression.
static long eval_const_expr(Token **rest, Token *tok) {
  Token *expr = copy_line(rest, tok);
  Token *rest2;
  long val = const_expr(&rest2, expr);
  if (rest2->kind != TK_EOF)
    error_tok(rest2, "extra token");
  return val;
}

static CondIncl *push_cond_incl(Token *tok, bool included) {
  CondIncl *ci = calloc(1, sizeof(CondIncl));
  ci->next = cond_incl;
  ci->ctx = IN_THEN;
  ci->tok = tok;
  ci->included = included;
  cond_incl = ci;
  return ci;
}

static Macro *find_macro(Token *tok) {
  if (tok->kind != TK_IDENT)
    return NULL;

  for (Macro *m = macros; m; m = m->next)
    if (strlen(m->name) == tok->len && !strncmp(m->name, tok->loc, tok->len))
      return m;
  return NULL;
}

static Macro *add_macro(char *name, Token *body) {
  Macro *m = calloc(1, sizeof(Macro));
  m->next = macros;
  m->name = name;
  m->body = body;
  macros = m;
  return m;
}

static bool expand_macro(Token **rest, Token *tok) {
  Macro *m = find_macro(tok);
  if (!m)
    return false;
  *rest = append(m->body, tok->next);
  return true;
}

// Visit all tokens in `tok` while evaluating preprocessing
// macros and directives.
static Token *preprocess(Token *tok) {
  Token head = {};
  Token *cur = &head;

  while (tok->kind != TK_EOF) {
    // If it is a macro, expand it.
    if (expand_macro(&tok, tok))
      continue;

    // Pass through if it is not a "#".
    if (!is_hash(tok)) {
      cur = cur->next = tok;
      tok = tok->next;
      continue;
    }

    Token *start = tok;
    tok = tok->next;

    if (equal(tok, "include")) {
      tok = tok->next;

      if (tok->kind != TK_STR)
        error_tok(tok, "expected a filename");

      char *path = tok->contents;
      char *input = read_file_string(path);
      if(!input)
        error_tok(tok, "%s", strerror(errno));
      tok = skip_line(tok->next);
      tok = append(tokenize(path, file_no, input), tok);
      continue;
    }

    if (equal(tok, "define")) {
      tok = tok->next;
      if (tok->kind != TK_IDENT)
        error_tok(tok, "macro name must be an identifier");
      char *name = strndup(tok->loc, tok->len);
      add_macro(name, copy_line(&tok, tok->next));
      continue;
    }

    if (equal(tok, "if")) {
      long val = eval_const_expr(&tok, tok->next);
      push_cond_incl(start, val);
      if (!val)
        tok = skip_cond_incl(tok);
      continue;
    }

    if (equal(tok, "else")) {
      if (!cond_incl || cond_incl->ctx == IN_ELSE)
        error_tok(start, "stray #else");
      cond_incl->ctx = IN_ELSE;
      tok = skip_line(tok->next);

      if (cond_incl->included)
        tok = skip_cond_incl(tok);
      continue;
    }

    if (equal(tok, "elif")) {
      if (!cond_incl || cond_incl->ctx == IN_ELSE)
        error_tok(start, "stray #elif");
      cond_incl->ctx = IN_ELIF;

      if (!cond_incl->included && eval_const_expr(&tok, tok->next))
        cond_incl->included = true;
      else
        tok = skip_cond_incl(tok);
      continue;
    }

    if (equal(tok, "endif")) {
      if (!cond_incl)
        error_tok(start, "stray #endif");
      cond_incl = cond_incl->next;
      tok = skip_line(tok->next);
      continue;
    }

    // `#`-only line is legal. It's called a null directive.
    if (tok->at_bol)
      continue;

    error_tok(tok, "invalid preprocessor directive");
  }

  cur->next = tok;
  return head.next;
}

// Entry point function of the preprocessor.
Token *read_file(char *path) {
  char *input = read_file_string(path);
  if (!input)
    error("cannot open %s: %s", path, strerror(errno));
  Token *tok = tokenize(path, file_no, input);
  tok = preprocess(tok);
  if (cond_incl)
    error_tok(cond_incl->tok, "unterminated conditional directive");
  convert_keywords(tok);
  return tok;
}