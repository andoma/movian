/*
 *  GL Widgets, Model language
 *  Copyright (C) 2008 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GLW_MODEL_H
#define GLW_MODEL_H

#include <libhts/htsq.h>
#include <inttypes.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>


typedef struct refstr {
  unsigned int refcnt;
  char str[0];
} refstr_t;

refstr_t *refstr_create(const char *str);
refstr_t *refstr_dup(refstr_t *r);
void refstr_unref(refstr_t *r);
const char *refstr_get(refstr_t *r);


#define GLW_MODEL_ERRORINFO



/**
 * 
 */
typedef enum {
  TOKEN_START,                 // 
  TOKEN_END,                   // 
  TOKEN_HASH,                  // #
  TOKEN_ASSIGNMENT,            // =
  TOKEN_END_OF_EXPR,           // ; (end of expression)
  TOKEN_SEPARATOR,             // ,
  TOKEN_BLOCK_OPEN,            // {
  TOKEN_BLOCK_CLOSE,           // }
  TOKEN_LEFT_PARENTHESIS,      // (
  TOKEN_RIGHT_PARENTHESIS,     // )
  TOKEN_LEFT_BRACKET,          // [
  TOKEN_RIGHT_BRACKET,         // ]
  TOKEN_DOT,                   // .
  TOKEN_ADD,                   // +
  TOKEN_SUB,                   // -
  TOKEN_MULTIPLY,              // *
  TOKEN_DIVIDE,                // /
  TOKEN_MODULO,                // %
  TOKEN_BOOLEAN_AND,           // &&
  TOKEN_BOOLEAN_OR,            // ||
  TOKEN_BOOLEAN_XOR,           // ^^
  TOKEN_STRING,                // A quoted string: " ... "
  TOKEN_FLOAT,
  TOKEN_INT,
  TOKEN_IDENTIFIER,
  TOKEN_FUNCTION,              //
  TOKEN_PROPERTY,
  TOKEN_PROPERTY_SUBSCRIPTION,
  TOKEN_OBJECT_ATTRIBUTE,
  TOKEN_VOID,                 // Void property
  TOKEN_DIRECTORY,            // Directory property
  /* Synthetic tokens (after parser) */
  TOKEN_EXPR,                  // infix expression
  TOKEN_RPN,                   // RPN expression
  TOKEN_BLOCK,
  TOKEN_ARRAY,
  TOKEN_NOP,
  TOKEN_VECTOR_FLOAT,
  TOKEN_VECTOR_STRING,
  TOKEN_EVENT,
  TOKEN_num,

} token_type_t;


/**
 *
 */
typedef struct token {
  token_type_t type;

  struct token *next;   /* Next statement, initially (after lexing)
			   all tokens are linked via 'next' only */

  struct token *child;  /* Childs */
  void *tmp;            /* Temporary link, used for various things */

#ifdef GLW_MODEL_ERRORINFO
  refstr_t *file;
  int line;
#endif

  union {
    int elements;
    void *extra;
    float f;
  } arg;

#define t_elements    arg.elements
#define t_extra       arg.extra
#define t_extra_float arg.f

  union {
    int  ival;

    char *string;
    char *string_vec[0];

    float value;
    float value_vec[0];

    const struct token_func   *func;
    const struct token_attrib *attrib;

    struct glw_event_map *gem;

  } u;

#define t_string          u.string
#define t_string_vector   u.string_vec
#define t_float           u.value
#define t_float_vector    u.value_vec
#define t_int             u.ival
#define t_func            u.func
#define t_attrib          u.attrib
#define t_gem             u.gem


  struct glw_prop_sub *propsubr;

} token_t;




/**
 *
 */
typedef struct errorinfo {
  char file[128];
  char error[128];
  int line;
} errorinfo_t;



/**
 *
 */
typedef struct glw_model_eval_context {
  token_t *stack;
  errorinfo_t *ei;
  token_t *alloc;
  struct glw *w;
  struct prop *prop;
  struct glw_root *gr;
  
  int persistence;
#define GLW_MODEL_EVAL_ONCE        0
#define GLW_MODEL_EVAL_PROP        1
#define GLW_MODEL_EVAL_EVERY_FRAME 2

  token_t *rpn; 

  int passive_subscriptions;

} glw_model_eval_context_t;


/**
 *
 */
typedef struct token_func {
  const char *name;
  int (*cb)(glw_model_eval_context_t *ec, struct token *self);
  void (*ctor)(struct token *self);
  void (*dtor)(struct token *self);
} token_func_t;


/**
 *
 */
typedef struct token_attrib {
  const char *name;
  int (*set)(glw_model_eval_context_t *ec, const struct token_attrib *a, 
	     struct token *t);
  int attrib;
} token_attrib_t;



void glw_model_token_free(token_t *t);

token_t *glw_model_token_copy(token_t *src);

token_t *glw_model_lexer(const char *src, errorinfo_t *ei, 
			 refstr_t *f, token_t *prev);


token_t *glw_model_load1(const char *filename, errorinfo_t *ei, token_t *prev);

int glw_model_parse(token_t *sof, errorinfo_t *ei);

void glw_model_free_chain(token_t *t);

const char *token2name(token_t *t);

void glw_model_print_tree(token_t *f, int indent);

int glw_model_function_resolve(token_t *t);

int glw_model_attrib_resolve(token_t *t);

int glw_model_seterr(errorinfo_t *ei, token_t *b, const char *fmt, ...);

int glw_model_eval_block(token_t *t, glw_model_eval_context_t *ec);

int glw_model_preproc(token_t *p, errorinfo_t *ei);

token_t *glw_model_clone_chain(token_t *src);

struct glw;
void glw_model_ctor(struct glw *w, int init, va_list ap);

struct glw_prop_sub_list;
void glw_prop_subscription_destroy_list(struct glw_prop_sub_list *l);

#endif /* GLW_MODEL_H */
