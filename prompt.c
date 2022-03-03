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
enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR };  // automatically assigned integers

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

lval* lval_qexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_QEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

void lval_del(lval* v) {
  switch (v->type) {
    case LVAL_NUM: break;
    case LVAL_ERR: free(v->err); break;
    case LVAL_SYM: free(v->sym); break;

    case LVAL_QEXPR:
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
  if (strstr(t->tag, "qexpr")) { x = lval_qexpr(); }

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
    case LVAL_QEXPR: lval_expr_print(v, '{', '}'); break;
    break;
  }
}

void lval_println(lval* v) { lval_print(v); putchar('\n'); }

lval* lval_eval(lval* v);
lval* lval_take(lval* v, int i);
lval* lval_pop(lval* v, int i);
lval* builtin_op(lval* a, char* op);

lval* lval_eval_sexpr(lval* v) {
  // evaluate children
  for (int i = 0; i < v-> count; i++) {
    v->cell[i] = lval_eval(v->cell[i]);
  }

  // error checking
  for (int i = 0; i < v->count; i++) {
    if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
  }

  // empty expression
  if (v->count == 0) { return v; }

  // single expression
  if (v->count == 1) { return lval_take(v, 0); }

  // ensure first element is symbol
  lval* f = lval_pop(v, 0);
  if (f->type != LVAL_SYM) {
    lval_del(f);
    lval_del(v);
    return lval_err("S-expression does not start with symbol!");
  }

  // call builtin with operator
  lval* result = builtin_op(v, f->sym);
  lval_del(f);
  return result;
}

lval* lval_eval(lval* v) {
  if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(v); }
  // all other lval types remain the same
  return v;
}

lval* lval_pop(lval* v, int i) {
  lval* x = v->cell[i];

  // shift the memory following the item at "i" over the top of it
  memmove(&v->cell[i], &v->cell[i+1], sizeof(lval*) * (v->count-i-1));

  v->count--;

  // reallocated the memory used
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  return x;
}

lval* lval_take(lval* v, int i) {
  lval* x = lval_pop(v, i);
  lval_del(v);
  return x;
}

lval* builtin_op(lval* a, char* op) {
  // ensure all arguments are numbers
  for (int i = 0; i < a->count; i++) {
    if(a->cell[i]->type != LVAL_NUM) {
      lval_del(a);
      return lval_err("Cannot operate on non number!");
    }
  }

  // pop the first element
  lval* x = lval_pop(a, 0);

  // if no arguments and sub then perform unary negation
  if (!strcmp(op, "-") && a->count == 0) { x->num = -x->num; }

  while (a->count > 0) {
    lval* y = lval_pop(a, 0);

    if (!strcmp(op, "+")) { x->num += y->num; }
    if (!strcmp(op, "-")) { x->num -= y->num; }
    if (!strcmp(op, "*")) { x->num *= y->num; }
    if (!strcmp(op, "/")) {
      if (y->num == 0) {
        lval_del(x); lval_del(y);
        x = lval_err("Division By Zero!");
        break;
      } else {
        x->num /= y->num;
      }
    }

    lval_del(y);
  }

  lval_del(a);
  return x;
}

lval* builtin_head(lval* a) {
  // check error conditions
  if (a->count != 1) {
    lval_del(a);
    return lval_err("Function 'head' passed incorrect number of arguments!");
  }

  if (a->cell[0]->type != LVAL_QEXPR) {
    lval_del(a);
    return lval_err("Function 'head' passed incorrect types!");
  }

  if (a->cell[0]->count == 0) {
    lval_del(a);
    return lval_err("Function 'head' passed {}!");
  }

  // otherwise take first agument
  lval* v = lval_take(a, 0);

  // delete all elements that are not head and return
  while (v->count > 1) { lval_del(lval_pop(v, 1)); }
  return v;
}

lval* builtin_tail(lval* a) {
  // check error conditions
  if (a->count != 1) {
    lval_del(a);
    return lval_err("Function 'tail' passed incorrect number of arguments!");
  }

  if (a->cell[0]->type != LVAL_QEXPR) {
    lval_del(a);
    return lval_err("Function 'tail' passed incorrect types!");
  }

  if (a->cell[0]->count == 0) {
    lval_del(a);
    return lval_err("Function 'tail' passed {}!");
  }

  // take first argument
  lval* v = lval_take(a, 0);

  // delete first element and return
  lval_del(lval_pop(v, 0));
  return v;
}

int main(int argc, char** argv) {
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Symbol = mpc_new("symbol");
  mpc_parser_t* Sexpr = mpc_new("sexpr");
  mpc_parser_t* Qexpr = mpc_new("qexpr");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");

  mpca_lang(MPCA_LANG_DEFAULT,
    "                                                                                         \
      number  : /-?[0-9]+/;                                                                   \
      symbol  : \"list\" | \"head\" | \"tail\" | \"join\" | \"eval\" | '+' | '-' | '*' | '/'; \
      sexpr    : '(' <expr>* ')';                                                             \
      qexpr    : '{' <expr>* '}';                                                             \
      expr    : <number> | <symbol> | <sexpr> | <qexpr>;                                      \
      lispy   : /^/ <expr>* /$/;                                                              \
    ",
    Number, Symbol, Sexpr, Qexpr, Expr, Lispy);

  puts("Lispy Version 0.0.0.0.1");
  puts("Press Ctrl+c to Exit\n");

  while (1) {
    char* input = readline("lispy> ");
    add_history(input);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      lval* x = lval_eval(lval_read(r.output));
      lval_println(x);
      lval_del(x);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
    free(input);
  }

  mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Lispy);
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
