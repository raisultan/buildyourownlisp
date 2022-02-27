#include <stdio.h>
#include <stdlib.h>

#include "mpc.h"

#ifdef _WIN32

static char buffer[2048];

#include <string.h>

char* readline(char* prompt) {
  fputs("lispy> ", stdout);
  fgets(buffer, 2048, stdin);
  char* cpy = malloc(strlen(buffer)+1);
  strcpy(cpy, buffer);
  cpy[strlen(cpy)-1] = '\0';
  return cpy
}

// Fake add_history function
void add_history(char* unused) {}

#else

#include <editline/readline.h>
// No need on mac #include <editline/history.h>

#endif

long eval_op(long x, char* op, long y) {
  if (!strcmp(op, "+")) { return x + y; }
  if (!strcmp(op, "-")) { return x - y; }
  if (!strcmp(op, "*")) { return x * y; }
  if (!strcmp(op, "/")) { return x / y; }
  return 0;
}

long eval(mpc_ast_t* t) {
  // base case
  if (strstr(t->tag, "number")) { return atoi(t->contents); }

  // the operator is always second child
  char* op = t->children[1]->contents;

  // store the third child in 'x'
  long x = eval(t->children[2]);

  int i = 3;
  while (strstr(t->children[i]->tag, "expr")) {
    x = eval_op(x, op, eval(t->children[i]));
    i++;
  }

  return x;
}

typedef struct {
  int type;
  long num;
  int err;
} lval;

lval lval_num(long x) {
  lval v;
  v.type = LVAL_NUM;
  v.num = x;
  return v;
}

lval lval_err(int x) {
  lval v;
  v.type = LVAL_ERR;
  v.err = x;
  return v;
}

void lval_print(lval v) {
  switch (v.type) {
    case LVAL_NUM: printf("%li", v.num); break;
    case LVAL_ERR:
      if (v.err == LERR_DIV_ZERO) { printf("Error: Division By Zero!"); }
      if (v.err == LERR_BAD_OP) { printf("Error: Invalid Operator!"); }
      if (v.err == LERR_BAD_NUM) { printf("Error: Invalid Number!"); }
    break;
  }
}

void lval_println(lval v) { lval_print(v); putchar('\n'); }

// type field
enum { LVAL_NUM, LVAL_ERR };  // automatically assigned integers

// err field
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

int main(int argc, char** argv) {
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Operator = mpc_new("operator");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");

  mpca_lang(MPCA_LANG_DEFAULT,
    "                                                  \
      number  : /-?[0-9]+/;                            \
      operator: '+' | '-' | '*' | '/';                 \
      expr    : <number> | '(' <operator> <expr>+ ')'; \
      lispy   : /^/ <operator> <expr>+ /$/;            \
    ",
    Number, Operator, Expr, Lispy);

  puts("Lispy Version 0.0.0.0.1");
  puts("Press Ctrl+c to Exit\n");

  while (1) {
    char* input = readline("lispy> ");
    add_history(input);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      mpc_ast_print(r.output);
      long result = eval(r.output);
      printf("result: %li\n\n", result);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    free(input);
  }

  mpc_cleanup(4, Number, Operator, Expr, Lispy);
  return 0;
}
