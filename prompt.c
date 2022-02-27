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

typedef struct lval {
  int type;

  long num;

  char* err;
  char* sym;

  int count;
  // when referencing itself it must contain only pointer, not the type directly
  struct lval** cell;  // pointer to list of lvals - a pointer to lval pointers
} lval;

// type field
enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR };  // automatically assigned integers

// err field
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

lval* lval_num(long x) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

lval* lval_err(char* m) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  // strlen returns number of bytes excluding the null terminator, so we add space for it ourselves
  v->err = malloc(strlen(m) + 1);
  strcpy(v->err, m);
  return v;
}

lval* lval_sym(char* s) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym = malloc(strlen(s) + 1);
  strcpy(v->sym, s);
  return v;
}

lval* lval_sexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->cell = NULL;  // memory location zero - non-value or empty data
  return v;
}

void lval_del(lval* v) {
  switch (v->type) {
    case LVAL_NUM: break;
    case LVAL_ERR: free(v->err); break;
    case LVAL_SYM: free(v->sym); break;
    case LVAL_SEXPR:
      for (int i = 0; i < v->count; i++) {
        lval_del(v->cell[i]);
      }
      free(v->cell);
    break;
  }
  free(v);
}

void lval_print(lval* v);  // forward declaration to pass cyclic use

lval* lval_add(lval* v, lval* x) {
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  v->cell[v->count-1] = x;
  return v;
}

lval* lval_read_num(mpc_ast_t* t) {
  long x = strtol(t->contents, NULL, 10);
  return errno != ERANGE ? lval_num(x) : lval_err("invalid number");
}

lval* lval_read(mpc_ast_t* t) {
  if (strstr(t->tag, "number")) { return lval_read_num(t); }
  if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }

  lval* x = NULL;
  if (!strcmp(t->tag, ">")) { x = lval_sexpr(); }
  if (strstr(t->tag, "sexpr")) { x = lval_sexpr(); }

  for (int i = 0; i < t->children_num; i++) {
    if (!strcmp(t->children[i]->contents, "(")) { continue; }
    if (!strcmp(t->children[i]->contents, ")")) { continue; }
    if (!strcmp(t->children[i]->contents, "{")) { continue; }
    if (!strcmp(t->children[i]->contents, "}")) { continue; }
    if (!strcmp(t->children[i]->tag, "regex")) { continue; }
    x = lval_add(x, lval_read(t->children[i]));
  }

  return x;
}

void lval_expr_print(lval* v, char open, char close) {
  putchar(open);
  for (int i = 0; i < v->count; i++) {
    lval_print(v->cell[i]);

    // don't print trailing space if last element
    if (i != (v->count-1)) {
      putchar(' ');
    }
  }
  putchar(close);
}

void lval_print(lval* v) {
  switch (v->type) {
    case LVAL_NUM: printf("%li", v->num); break;
    case LVAL_ERR: printf("Error: %s", v->err); break;
    case LVAL_SYM: printf("%s", v->sym); break;
    case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
    break;
  }
}

void lval_println(lval* v) { lval_print(v); putchar('\n'); }

/*
lval eval_op(lval x, char* op, lval y) {
  if (x.type == LVAL_ERR) { return x; }
  if (y.type == LVAL_ERR) { return y; }

  if (!strcmp(op, "+")) { return lval_num(x.num + y.num); }
  if (!strcmp(op, "-")) { return lval_num(x.num - y.num); }
  if (!strcmp(op, "*")) { return lval_num(x.num * y.num); }
  if (!strcmp(op, "/")) {
    return y.num == 0 ? lval_err(LERR_DIV_ZERO) : lval_num(x.num / y.num);
  }
  return lval_err(LERR_BAD_OP);
}

lval eval(mpc_ast_t* t) {
  // base case
  if (strstr(t->tag, "number")) {
    long x = strtol(t->contents, NULL, 10);
    return errno != ERANGE ? lval_num(x) : lval_err(LERR_BAD_NUM);
  }

  // the operator is always second child
  char* op = t->children[1]->contents;

  // store the third child in 'x'
  lval x = eval(t->children[2]);

  int i = 3;
  while (strstr(t->children[i]->tag, "expr")) {
    x = eval_op(x, op, eval(t->children[i]));
    i++;
  }

  return x;
}
*/

int main(int argc, char** argv) {
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Symbol = mpc_new("symbol");
  mpc_parser_t* Sexpr = mpc_new("sexpr");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");

  mpca_lang(MPCA_LANG_DEFAULT,
    "                                                  \
      number  : /-?[0-9]+/;                            \
      symbol  : '+' | '-' | '*' | '/';                 \
      sexpr    : '(' <expr>* ')';                      \
      expr    : <number> | <symbol> | <sexpr>;          \
      lispy   : /^/ <expr>* /$/;                       \
    ",
    Number, Symbol, Sexpr, Expr, Lispy);

  puts("Lispy Version 0.0.0.0.1");
  puts("Press Ctrl+c to Exit\n");

  while (1) {
    char* input = readline("lispy> ");
    add_history(input);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      lval* x = lval_read(r.output);
      lval_println(x);
      lval_del(x);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
    free(input);
  }

  mpc_cleanup(5, Number, Symbol, Sexpr, Expr, Lispy);
  return 0;
}

// & address of
// * pointer type
// getting data from an adress is called "dereferencing"
/*
void assign_value(some_struct_type* v) {
  // v is adress here
  // *v is value of the adress
  v->field = 0;
}

some_struct_type v;
assign_value(&v);
*/

/*
The Stack
- part of memory where your program lives: vars, funcs, etc
- after the part of memory is used it is unallocated

The Heap
- storage of objects with a longer lifespan
- has to be manually allocated and deallocated: malloc -> *, free(*)
*/
