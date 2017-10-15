#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "mpc.h"

/* If we are compiling on Windows compile these functions */
#ifdef _WIN32
#include <string.h>

static char	buffer[2048];

/* Fake readline function */
char* readline(char* prompt) {
    fputs(prompt, stdout);
    fgets(buffer, 2048, stdin);
    char* cpy = malloc(strlen(buffer) + 1);
    strcpy(cpy, buffer);
    cpy[strlen(cpy) - 1] = '\0';
    return cpy;
}

/* Fake add_history function */
void add_history(char *unused) {}

/* Otherwise include the editline headers */
#else

#include <editline/readline.h>

#if defined __linux__ || defined _WIN32
#include <editline/history.h> // history.h should be included on macOS by default
#endif
#endif

/* MACROS FOR BUILTINS */
#define LASSERT(args, cond, fmt, ...) \
  if (!(cond)) { lval* err = lval_err(fmt, ##__VA_ARGS__); lval_del(args); return err; }

#define LASSERT_TYPE(func, args, index, expect) \
  LASSERT(args, args->cell[index]->type == expect, \
    "Function '%s' passed incorrect type for argument %i. " \
    "Got %s, Expected %s.", \
    func, index, ltype_name(args->cell[index]->type), ltype_name(expect))

#define LASSERT_NUM(func, args, num) \
  LASSERT(args, args->count == num, \
    "Function '%s' passed incorrect number of arguments. " \
    "Got %i, Expected %i.", \
    func, args->count, num)

#define LASSERT_NOT_EMPTY(func, args, index) \
  LASSERT(args, args->cell[index]->count != 0, \
    "Function '%s' passed {} for argument %i.", func, index);

/* Forward Declarations */

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

/* Lisp Values */

enum {
    LVAL_ERR, LVAL_NUM, LVAL_SYM, LVAL_FUN, LVAL_SEXPR, LVAL_QEXPR
};

/* Map Lisp Values to string names */

char *ltype_name(int t) {
    switch (t) {
        case LVAL_FUN:
            return "Function";
        case LVAL_NUM:
            return "Number";
        case LVAL_ERR:
            return "Error";
        case LVAL_SYM:
            return "Symbol";
        case LVAL_SEXPR:
            return "S-Expression";
        case LVAL_QEXPR:
            return "Q-Expression";
        default:
            return "Unknown";
    }
}

typedef lval *(*lbuiltin)(lenv *, lval *);

struct lval {
    int type;

    /* Basic */
    double num;
    char *err;
    char *sym;

    /* Function */
    lbuiltin builtin;
    lenv *env;
    lval *formals;
    lval *body;

    /* Expression */
    int count;
    lval **cell;
};

struct lenv {
    lenv *par;
    int count;
    char **syms;
    lval **vals;
};

/* Environment related functions */

/* Create a new environment */
lenv *lenv_new(void) {
    lenv *e = malloc(sizeof(lenv));
    e->par = NULL;
    e->count = 0;
    e->syms = NULL;
    e->vals = NULL;
    return e;
}

lenv *lenv_copy(lenv *e);

lval *lval_copy(lval *v) {

    lval *x = malloc(sizeof(lval));
    x->type = v->type;

    switch (v->type) {

        /* Copy Functions and Numbers Directly */
        case LVAL_FUN:
            if (v->builtin) {
                /* If builtin just copy directly  */
                x->builtin = v->builtin;
            } else {
                /* If user defined copy individual stuff */
                x->builtin = NULL;
                x->env = lenv_copy(v->env);
                x->formals = lval_copy(v->formals);
                x->body = lval_copy(v->body);
            }
            break;
        case LVAL_NUM:
            x->num = v->num;
            break;

            /* Copy Strings using malloc and strcpy */
        case LVAL_ERR:
            x->err = malloc(strlen(v->err) + 1);
            strcpy(x->err, v->err);
            break;

        case LVAL_SYM:
            x->sym = malloc(strlen(v->sym) + 1);
            strcpy(x->sym, v->sym);
            break;

            /* Copy Lists by copying each sub-expression */
        case LVAL_SEXPR:
        case LVAL_QEXPR:
            x->count = v->count;
            x->cell = malloc(sizeof(lval *) * x->count);
            for (int i = 0; i < x->count; i++) {
                x->cell[i] = lval_copy(v->cell[i]);
            }
            break;
    }
    return x;
}

lenv *lenv_copy(lenv *e) {
    lenv *n = malloc(sizeof(lenv));
    n->par = e->par;
    n->count = e->count;
    n->syms = malloc(sizeof(char *) * n->count);
    n->vals = malloc(sizeof(lval *) * n->count);
    for (int i = 0; i < e->count; i++) {
        n->syms[i] = malloc(strlen(e->syms[i]) + 1);
        strcpy(n->syms[i], e->syms[i]);
        n->vals[i] = lval_copy(e->vals[i]);
    }
    return n;
}

void lenv_del(lenv *e);

void lval_del(lval *v) {
    switch (v->type) {
        case LVAL_NUM:
            break;
        case LVAL_FUN:
            if (!v->builtin) {
                lenv_del(v->env);
                lval_del(v->formals);
                lval_del(v->body);
            }
            break;
        case LVAL_ERR:
            free(v->err);
            break;
        case LVAL_SYM:
            free(v->sym);
            break;
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

void lenv_del(lenv *e) {
    for (int i = 0; i < e->count; i++) {
        free(e->syms[i]);
        lval_del(e->vals[i]);
    }
    free(e->syms);
    free(e->vals);
    free(e);
}

lval *lval_err(char *fmt, ...) {
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_ERR;

    /* Create a va (variable arguments) list and initialize it */
    va_list va;
    va_start(va, fmt);

    /* Allocate 512 bytes of space */
    v->err = malloc(512);

    /* printf the error string with a maximum of 511 characters */
    vsnprintf(v->err, 511, fmt, va);

    /* Reallocate to number of bytes actually used */
    v->err = realloc(v->err, strlen(v->err) + 1);

    /* Cleanup our va list */
    va_end(va);

    return v;
}

lval *lenv_get(lenv *e, lval *k) {
    /* Iterate over all items in environment */
    for (int i = 0; i < e->count; i++) {
        /* Check if the stored string matches the symbol string */
        /* If it does, return a copy of the value */
        if (strcmp(e->syms[i], k->sym) == 0) {
            return lval_copy(e->vals[i]);
        }
    }

    /* If no symbol check in parent otherwise error */
    if (e->par) {
        return lenv_get(e->par, k);
    } else {
        return lval_err("Unbound Symbol '%s'", k->sym);
    }
}

void lenv_put(lenv *e, lval *k, lval *v) {
    /* Iterate over all items in environment */
    /* This is to see if variable already exists */
    for (int i = 0; i < e->count; i++) {

        /* If variable is found delete item at that position */
        /* And replace with variable supplied by user */
        if (strcmp(e->syms[i], k->sym) == 0) {
            lval_del(e->vals[i]);
            e->vals[i] = lval_copy(v);
            return;
        }
    }

    /* If no existing entry found allocate space for new entry */
    e->count++;
    e->vals = realloc(e->vals, sizeof(lval *) * e->count);
    e->syms = realloc(e->syms, sizeof(char *) * e->count);

    /* Copy contents of lval and symbol string into new location */
    e->vals[e->count - 1] = lval_copy(v);
    e->syms[e->count - 1] = malloc(strlen(k->sym) + 1);
    strcpy(e->syms[e->count - 1], k->sym);
}


/* Define variable in global environment */
void lenv_def(lenv *e, lval *k, lval *v) {
    /* Iterate till e has no parent */
    while (e->par) { e = e->par; }
    /* Put value in e */
    lenv_put(e, k, v);
}

/* Constructor for user defined functions */
lval *lval_lambda(lval *formals, lval *body) {
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_FUN;

    /* Set Builtin to Null */
    v->builtin = NULL;

    /* Build new environment */
    v->env = lenv_new();

    /* Set Formals and Body */
    v->formals = formals;
    v->body = body;
    return v;
}

/* Constructor for  builtin functions */
lval *lval_fun(lbuiltin func) {
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_FUN;
    v->builtin = func;
    return v;
}

/* Construct a pointer to a new Number lval */
lval *lval_num(double x) {
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_NUM;
    v->num = x;
    return v;
}

/* Construct a pointer to a new Symbol lval */
lval *lval_sym(char *s) {
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_SYM;
    v->sym = malloc(strlen(s) + 1);
    strcpy(v->sym, s);
    return v;
}

/* A pointer to a new empty Sexpr lval */
lval *lval_sexpr(void) {
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_SEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

/* A pointer to a new empty Qexpr lval */
lval *lval_qexpr(void) {
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_QEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

lval *lval_add(lval *v, lval *x) {
    v->count++;
    v->cell = realloc(v->cell, sizeof(lval *) * v->count);
    v->cell[v->count - 1] = x;
    return v;
}

lval *lval_pop(lval *v, int i) {
    /* Find the item at "i" */
    lval *x = v->cell[i];

    /* Shift memory after the item at "i" over the top */
    memmove(&v->cell[i], &v->cell[i + 1],
            sizeof(lval *) * (v->count - i - 1));

    /* Decrease the count of items in the list */
    v->count--;

    /* Reallocate the memory used */
    v->cell = realloc(v->cell, sizeof(lval *) * v->count);
    return x;
}

lval *lval_take(lval *v, int i) {
    lval *x = lval_pop(v, i);
    lval_del(v);
    return x;
}

void lval_print(lval *v);

void lval_expr_print(lval *v, char open, char close) {
    putchar(open);
    for (int i = 0; i < v->count; i++) {

        /* Print Value contained within */
        lval_print(v->cell[i]);

        /* Don't print trailing space if last element */
        if (i != (v->count - 1)) {
            putchar(' ');
        }
    }
    putchar(close);
}

void lval_print(lval *v) {
    switch (v->type) {
        case LVAL_NUM:
            printf("%f", v->num);
            break;
        case LVAL_ERR:
            printf("Error: %s", v->err);
            break;
        case LVAL_SYM:
            printf("%s", v->sym);
            break;
        case LVAL_SEXPR:
            lval_expr_print(v, '(', ')');
            break;
        case LVAL_QEXPR:
            lval_expr_print(v, '{', '}');
            break;
        case LVAL_FUN:
            if (v->builtin) {
                printf("<builtin>");
            } else {
                printf("(\\ ");
                lval_print(v->formals);
                putchar(' ');
                lval_print(v->body);
                putchar(')');
            }
            break;
    }
}

void lval_println(lval *v) {
    lval_print(v);
    putchar('\n');
}

lval *builtin_op(lenv *e, lval *a, char *op) {
    /* Ensure all arguments are numbers */
    for (int i = 0; i < a->count; i++) {
        if (a->cell[i]->type != LVAL_NUM) {
            lval_del(a);
            return lval_err("Cannot operate on non-number!");
        }
    }

    /* Pop the first element */
    lval *x = lval_pop(a, 0);

    /* If no arguments and sub then perform unary negation */
    if ((strcmp(op, "-") == 0) && a->count == 0) {
        x->num = -x->num;
    }

    /* While there are still elements remaining */
    while (a->count > 0) {
        /* Pop the next element */
        lval *y = lval_pop(a, 0);

        /* Perform operation */
        if (strcmp(op, "+") == 0) { x->num += y->num; }
        if (strcmp(op, "-") == 0) { x->num -= y->num; }
        if (strcmp(op, "*") == 0) { x->num *= y->num; }
        if (strcmp(op, "^") == 0) { x->num = pow(x->num, y->num); }
        if (strcmp(op, "%") == 0) {
            if (y->num == 0) {
                lval_del(x);
                lval_del(y);
                x = lval_err("Division By Zero.");
                break;
            }
            x->num = fmod(x->num, y->num);
        }
        if (strcmp(op, "/") == 0) {
            if (y->num == 0) {
                lval_del(x);
                lval_del(y);
                x = lval_err("Division By Zero.");
                break;
            }
            x->num /= y->num;
        }

        /* Delete element now finished with */
        lval_del(y);
    }

    /* Delete input expression and return result */
    lval_del(a);
    return x;
}

/* Builtin functions */

lval *builtin_add(lenv *e, lval *a) {
    return builtin_op(e, a, "+");
}

lval *builtin_sub(lenv *e, lval *a) {
    return builtin_op(e, a, "-");
}

lval *builtin_mul(lenv *e, lval *a) {
    return builtin_op(e, a, "*");
}

lval *builtin_div(lenv *e, lval *a) {
    return builtin_op(e, a, "/");
}

lval *builtin_mod(lenv *e, lval *a) {
    return builtin_op(e, a, "%");
}

lval *builtin_pow(lenv *e, lval *a) {
    return builtin_op(e, a, "^");
}

lval *builtin_lambda(lenv *e, lval *a) {
    /* Check Two arguments, each of which are Q-Expressions */
    LASSERT_NUM("\\", a, 2);
    LASSERT_TYPE("\\", a, 0, LVAL_QEXPR);
    LASSERT_TYPE("\\", a, 1, LVAL_QEXPR);

    /* Check first Q-Expression contains only Symbols */
    for (int i = 0; i < a->cell[0]->count; i++) {
        LASSERT(a, (a->cell[0]->cell[i]->type == LVAL_SYM),
                "Cannot define non-symbol. Got %s, Expected %s.",
                ltype_name(a->cell[0]->cell[i]->type), ltype_name(LVAL_SYM));
    }

    /* Pop first two arguments and pass them to lval_lambda */
    lval *formals = lval_pop(a, 0);
    lval *body = lval_pop(a, 0);
    lval_del(a);

    return lval_lambda(formals, body);
}

lval *builtin_head(lenv *e, lval *a) {
    LASSERT(a, a->count == 1,
            "Function 'head' passed too many arguments. "
                    "Got %i, Expected %i.",
            a->count, 1);

    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'head' passed incorrect type for argument 0. "
                    "Got %s, Expected %s.",
            ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));

    lval *v = lval_take(a, 0);
    while (v->count > 1) { lval_del(lval_pop(v, 1)); }
    return v;
}

lval *builtin_tail(lenv *e, lval *a) {
    LASSERT(a, a->count == 1,
            "Function 'tail' passed too many arguments!");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'tail' passed incorrect type!");
    LASSERT(a, a->cell[0]->count != 0,
            "Function 'tail' passed {}!");

    lval *v = lval_take(a, 0);
    lval_del(lval_pop(v, 0));
    return v;
}

lval *builtin_list(lenv *e, lval *a) {
    a->type = LVAL_QEXPR;
    return a;
}

lval *lval_eval(lenv *e, lval *v);

lval *builtin_eval(lenv *e, lval *a) {
    LASSERT(a, a->count == 1,
            "Function 'eval' passed too many arguments!");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'eval' passed incorrect type!");

    lval *x = lval_take(a, 0);
    x->type = LVAL_SEXPR;
    return lval_eval(e, x);
}

lval *lval_join(lval *x, lval *y) {

    /* For each cell in 'y' add it to 'x' */
    while (y->count) {
        x = lval_add(x, lval_pop(y, 0));
    }

    /* Delete the empty 'y' and return 'x' */
    lval_del(y);
    return x;
}

lval *builtin_join(lenv *e, lval *a) {

    for (int i = 0; i < a->count; i++) {
        LASSERT(a, a->cell[i]->type == LVAL_QEXPR,
                "Function 'join' passed incorrect type.");
    }

    lval *x = lval_pop(a, 0);

    while (a->count) {
        x = lval_join(x, lval_pop(a, 0));
    }

    lval_del(a);
    return x;
}

lval *builtin_len(lenv *e, lval *a) {
    LASSERT(a, a->count == 1, "len takes exactly 1 parameter");

    lval *v = lval_take(a, 0);

    LASSERT(v, v->type == LVAL_QEXPR,
            "Invalid type for function 'len'.");

    /* Construct a lval* to hold the length of the input */
    lval *x = NULL;
    x = lval_num(v->count);
    return x;
}

lval *builtin_cons(lenv *e, lval *a) {
    /* Create qexpr from first value */
    lval *x = NULL;
    x = lval_qexpr();
    x = lval_add(x, lval_pop(a, 0));

    x = lval_join(x, lval_pop(a, 0));

    lval_del(a);
    return x;
}

lval *builtin_init(lenv *e, lval *a) {
    LASSERT(a, a->count == 1,
            "Function 'init' passed too many arguments!");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'init' passed incorrect type!");
    LASSERT(a, a->cell[0]->count != 0,
            "Function 'init' passed {}!");

    int tail_i = a->cell[0]->count - 1;
    lval *x = lval_take(a, 0);
    lval_del(lval_pop(x, tail_i));
    return x;
};

lval *builtin_var(lenv *e, lval *a, char *func) {
    LASSERT_TYPE(func, a, 0, LVAL_QEXPR);

    lval *syms = a->cell[0];
    for (int i = 0; i < syms->count; i++) {
        LASSERT(a, (syms->cell[i]->type == LVAL_SYM),
                "Function '%s' cannot define non-symbol. "
                        "Got %s, Expected %s.", func,
                ltype_name(syms->cell[i]->type),
                ltype_name(LVAL_SYM));
    }

    LASSERT(a, (syms->count == a->count - 1),
            "Function '%s' passed too many arguments for symbols. "
                    "Got %i, Expected %i.", func, syms->count, a->count - 1);

    for (int i = 0; i < syms->count; i++) {
        /* If 'def' define in globally. If 'put' define in locally */
        if (strcmp(func, "def") == 0) {
            lenv_def(e, syms->cell[i], a->cell[i + 1]);
        }

        if (strcmp(func, "=") == 0) {
            lenv_put(e, syms->cell[i], a->cell[i + 1]);
        }
    }

    lval_del(a);
    return lval_sexpr();
}

lval *builtin_def(lenv *e, lval *a) {
    return builtin_var(e, a, "def");
}

lval *builtin_put(lenv *e, lval *a) {
    return builtin_var(e, a, "=");
}

void lenv_add_builtin(lenv *e, char *name, lbuiltin func) {
    lval *k = lval_sym(name);
    lval *v = lval_fun(func);
    lenv_put(e, k, v);
    lval_del(k);
    lval_del(v);
}

void lenv_add_builtins(lenv *e) {
    /* List Functions */
    lenv_add_builtin(e, "list", builtin_list);
    lenv_add_builtin(e, "head", builtin_head);
    lenv_add_builtin(e, "tail", builtin_tail);
    lenv_add_builtin(e, "eval", builtin_eval);
    lenv_add_builtin(e, "join", builtin_join);

    /* Mathematical Functions */
    lenv_add_builtin(e, "+", builtin_add);
    lenv_add_builtin(e, "-", builtin_sub);
    lenv_add_builtin(e, "*", builtin_mul);
    lenv_add_builtin(e, "/", builtin_div);
    lenv_add_builtin(e, "%", builtin_mod);
    lenv_add_builtin(e, "^", builtin_pow);

    /* Variable Functions */
    lenv_add_builtin(e, "def", builtin_def);
    lenv_add_builtin(e, "=", builtin_put);

    /* User defined Functions*/
    lenv_add_builtin(e, "\\", builtin_lambda);
}

lval *lval_call(lenv *e, lval *f, lval *a) {

    /* If Builtin then simply apply that */
    if (f->builtin) { return f->builtin(e, a); }

    /* Record Argument Counts */
    int given = a->count;
    int total = f->formals->count;

    /* While arguments still remain to be processed */
    while (a->count) {

        /* If we've ran out of formal arguments to bind */
        if (f->formals->count == 0) {
            lval_del(a);
            return lval_err(
                    "Function passed too many arguments. "
                            "Got %i, Expected %i.", given, total);
        }

        /* Pop the first symbol from the formals */
        lval *sym = lval_pop(f->formals, 0);

        /* Special Case to deal with '&' */
        /* If '&' remains in formal list bind to empty list */
        if (f->formals->count > 0 &&
            strcmp(f->formals->cell[0]->sym, "&") == 0) {

            /* Check to ensure that & is not passed invalidly. */
            if (f->formals->count != 2) {
                return lval_err("Function format invalid. "
                                        "Symbol '&' not followed by single symbol.");
            }

            /* Pop and delete '&' symbol */
            lval_del(lval_pop(f->formals, 0));

            /* Pop next symbol and create empty list */
            lval *sym = lval_pop(f->formals, 0);
            lval *val = lval_qexpr();

            /* Bind to environment and delete */
            lenv_put(f->env, sym, val);
            lval_del(sym);
            lval_del(val);
        }

        /* Pop the next argument from the list */
        lval *val = lval_pop(a, 0);

        /* Bind a copy into the function's environment */
        lenv_put(f->env, sym, val);

        /* Delete symbol and value */
        lval_del(sym);
        lval_del(val);
    }

    /* Argument list is now bound so can be cleaned up */
    lval_del(a);

    /* If all formals have been bound evaluate */
    if (f->formals->count == 0) {

        /* Set environment parent to evaluation environment */
        f->env->par = e;

        /* Evaluate and return */
        return builtin_eval(
                f->env, lval_add(lval_sexpr(), lval_copy(f->body)));
    } else {
        /* Otherwise return partially evaluated function */
        return lval_copy(f);
    }

}


lval *lval_eval_sexpr(lenv *e, lval *v) {

    /* Evaluate Children */
    for (int i = 0; i < v->count; i++) {
        v->cell[i] = lval_eval(e, v->cell[i]);
    }

    /* Error Checking */
    for (int i = 0; i < v->count; i++) {
        if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
    }

    /* Empty Expression */
    if (v->count == 0) { return v; }

    /* Single Expression */
    if (v->count == 1) { return lval_take(v, 0); }

    /* Ensure First Element is a function after evaluation */
    lval *f = lval_pop(v, 0);
    if (f->type != LVAL_FUN) {
        lval *err = lval_err(
                "S-Expression starts with incorrect type. "
                        "Got %s, Expected %s.",
                ltype_name(f->type), ltype_name(LVAL_FUN));
        lval_del(f);
        lval_del(v);
        return err;
    }

    /* If it is a function, call it and return the results */
    lval *result = lval_call(e, f, v);
    lval_del(f);
    return result;
}

lval *lval_eval(lenv *e, lval *v) {
    if (v->type == LVAL_SYM) {
        lval *x = lenv_get(e, v);
        lval_del(v);
        return x;
    }
    /* Evaluate Sexpressions */
    if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(e, v); }
    /* All other lval types remain the same */
    return v;
}

lval *lval_read_num(mpc_ast_t *t) {
    errno = 0;
    double x = strtod(t->contents, NULL);
    return errno != ERANGE ?
           lval_num(x) : lval_err("invalid number");
}

lval *lval_read(mpc_ast_t *t) {

    /* If Symbol or Number return conversion to that type */
    if (strstr(t->tag, "number")) { return lval_read_num(t); }
    if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }

    /* If root (>) or sexpr then create empty list */
    lval *x = NULL;
    if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); }
    if (strstr(t->tag, "sexpr")) { x = lval_sexpr(); }
    if (strstr(t->tag, "qexpr")) { x = lval_qexpr(); }

    /* Fill this list with any valid expression contained within */
    for (int i = 0; i < t->children_num; i++) {
        if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
        if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
        if (strcmp(t->children[i]->contents, "}") == 0) { continue; }
        if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
        if (strcmp(t->children[i]->tag, "regex") == 0) { continue; }
        x = lval_add(x, lval_read(t->children[i]));
    }

    return x;
}

int main(int argc, char **argv) {

    mpc_parser_t *Number = mpc_new("number");
    mpc_parser_t *Symbol = mpc_new("symbol");
    mpc_parser_t *Sexpr = mpc_new("sexpr");
    mpc_parser_t *Qexpr = mpc_new("qexpr");
    mpc_parser_t *Expr = mpc_new("expr");
    mpc_parser_t *Selyp = mpc_new("selyp");

    mpca_lang(MPCA_LANG_DEFAULT,
              "                                                       \
      number : /[-+]?([0-9]*[.])?[0-9]+/ ;                  \
      symbol : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&%^]+/ ;           \
      sexpr  : '(' <expr>* ')' ;                            \
      qexpr  : '{' <expr>* '}' ;                            \
      expr   : <number> | <symbol> | <sexpr> | <qexpr> ;    \
      selyp  : /^/ <expr>* /$/ ;                            \
    ",
              Number, Symbol, Sexpr, Qexpr, Expr, Selyp);

    puts("Selyp Version 0.0.0.0.1");
    puts("Press Ctrl+c to Exit\n");

    /* Create a new environment and add builtin functions */
    lenv *e = lenv_new();
    lenv_add_builtins(e);

    while (1) {
        char *input = readline("> ");
        add_history(input);

        mpc_result_t r;
        if (mpc_parse("<stdin>", input, Selyp, &r)) {
            lval *x = lval_eval(e, lval_read(r.output));
            lval_println(x);
            lval_del(x);
            mpc_ast_delete(r.output);
        } else {
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }

        free(input);
    }
    lenv_del(e);
    mpc_cleanup(5, Number, Symbol, Sexpr, Qexpr, Expr, Selyp);
    return 0;
}
