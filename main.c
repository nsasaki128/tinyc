#include "punyc.h"

// Returns the contents of a given file.
static char *read_file(char *path) {
  // By convention, read from stdin if a given filename is "-".
  FILE *fp = stdin;
  if (strcmp(path, "-")) {
    fp = fopen(path, "r");
    if (!fp)
      error("cannot open %s: %s", path, strerror(errno));
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
  printf(".file 1 \"%s\"\n", path);
  return buf;
}

int main(int argc, char **argv) {
  if (argc != 2)
    error("%s: invalid number of arguments\n", argv[0]);

  // Tokenize and parse.
  char *input = read_file(argv[1]);
  Token *tok = tokenize(argv[1], input);
  Program *prog = parse(tok);

  // Assign offsets to local variables.
  for (Function *fn = prog->fns; fn; fn = fn->next) {
    int offset = 32;
    for (Var *var = fn->locals; var; var = var->next) {
      offset = align_to(offset, var->align);
      offset += size_of(var->ty);
      var->offset = offset;
    }
    fn->stack_size = align_to(offset, 16);
  }

  // Traverse the AST to emit assembly.
  codegen(prog);

  return 0;
}