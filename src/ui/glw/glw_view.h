/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */

#ifndef GLW_VIEW_H
#define GLW_VIEW_H

#include <inttypes.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <limits.h>

#include "glw.h"

/**
 * 
 */
typedef enum {
  TOKEN_START,                 // 
  TOKEN_END,                   // 
  TOKEN_HASH,                  // #
  TOKEN_ASSIGNMENT,            // =
  TOKEN_COND_ASSIGNMENT,       // ?=
  TOKEN_DEBUG_ASSIGNMENT,      // _=_
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
  TOKEN_DOLLAR,                // $
  TOKEN_AMPERSAND,             // &
  TOKEN_BOOLEAN_AND,           // &&
  TOKEN_BOOLEAN_OR,            // ||
  TOKEN_BOOLEAN_XOR,           // ^^
  TOKEN_EQ,                    // ==
  TOKEN_NEQ,                   // !=
  TOKEN_BOOLEAN_NOT,           // !
  TOKEN_NULL_COALESCE,         // ??
  TOKEN_LT,                    // <
  TOKEN_GT,                    // >
  TOKEN_COLON,                 // :
  TOKEN_RSTRING,               // A ref allocated string
  TOKEN_CSTRING,               // A compile time constant string
  TOKEN_FLOAT,
  TOKEN_INT,
  TOKEN_IDENTIFIER,
  TOKEN_FUNCTION,              //
  TOKEN_PROPERTY_REF,          // We just keep a reference
  TOKEN_PROPERTY_OWNER,        // We own the property and must destroy it
                               // when token is free'd
  TOKEN_PROPERTY_NAME,
  TOKEN_PROPERTY_SUBSCRIPTION,
  TOKEN_OBJECT_ATTRIBUTE,
  TOKEN_VOID,                 // Void property
  TOKEN_DIRECTORY,            // Directory property
  /* Synthetic tokens (after parser) */
  TOKEN_EXPR,                  // infix expression
  TOKEN_RPN,                   // RPN expression
  TOKEN_BLOCK,
  TOKEN_NOP,
  TOKEN_VECTOR_FLOAT,
  TOKEN_EVENT,
  TOKEN_LINK,                  // A link with title and url
  TOKEN_VECTOR,                // List of tokens
  TOKEN_MOD_FLAGS,
  TOKEN_num,

} token_type_t;


/**
 *
 */
typedef struct token {
  struct token *next;   /* Next statement, initially (after lexing)
			   all tokens are linked via 'next' only */

  struct token *child;  /* Childs */
  void *tmp;            /* Temporary link, used for various things */

  rstr_t *file;
  int line;

  token_type_t type;
  int16_t t_num_args;
  uint8_t t_flags;
#define TOKEN_F_SELECTED 0x1 // The 'selected' in a vector
#define TOKEN_F_CANONICAL_PATH 0x2 // Do not follow paths when resolving prop

  uint8_t t_dynamic_eval;

  union {
    int elements;
    void *extra;
    float f;
    int args;
    int i;
  } arg;

#define t_elements    arg.elements
#define t_extra       arg.extra
#define t_extra_float arg.f
#define t_extra_int   arg.i

  union {
    const struct token_attrib *t_attrib;
    struct glw_prop_sub *t_propsubr;
  };

#define TOKEN_PROPERTY_NAME_VEC_SIZE (16 / __SIZEOF_POINTER__)

  union {
    int  ival;

    rstr_t *pnvec[TOKEN_PROPERTY_NAME_VEC_SIZE];

    struct {
      float value;
      int how;  // same as PROP_SET_ ...
    } f;

    float float_vec_int[4];

    struct {
      const struct token_func   *func;
      const void *farg;
    };

    struct glw_event_map *gem;

    struct prop *prop;

    struct {
      rstr_t *rtitle;
      rstr_t *rurl;
    } link;

    struct {
      rstr_t *rstr;
      prop_str_type_t type;

    } rstr;

    const char *cstr;

    struct {
      int set;
      int clr;
    } modflags;
  } u;

#define t_set             u.modflags.set
#define t_clr             u.modflags.clr
#define t_pnvec           u.pnvec
#define t_cstring         u.cstr
#define t_rstring         u.rstr.rstr
#define t_rstrtype        u.rstr.type
#define t_float           u.f.value
#define t_float_how       u.f.how
#define t_float_vector_int u.float_vec_int
#define t_int             u.ival
#define t_func            u.func
#define t_func_arg        u.farg
#define t_gem             u.gem
#define t_prop            u.prop
#define t_pixmap          u.pixmap
#define t_link_rtitle     u.link.rtitle
#define t_link_rurl       u.link.rurl

} token_t;

/**
 *
 */
typedef struct errorinfo {
  char file[PATH_MAX];
  char error[128];
  int line;
} errorinfo_t;



/**
 *
 */
typedef struct glw_view_eval_context {
  token_t *stack;
  errorinfo_t *ei;
  token_t *alloc;
  struct glw *w;
  struct prop *prop, *prop_parent, *prop_viewx, *prop_args, *prop_clone;
  struct glw_root *gr;
  const struct glw_rctx *rc;

  int dynamic_eval;

  token_t *rpn; 

  int passive_subscriptions;

  struct glw_prop_sub_list *sublist;

  struct event *event;

  prop_t *tgtprop;

  int debug;

} glw_view_eval_context_t;



/**
 *
 */
typedef struct glw_clone {
  LIST_ENTRY(glw_clone) c_link;
  struct sub_cloner *c_sc;
  glw_t *c_w;
  prop_t *c_prop;
  prop_t *c_clone_root;

  uint16_t c_pos;
  char c_evaluated;

} glw_clone_t;

/**
 *
 */
typedef struct token_func {
  const char *name;
  int nargs;
  int (*cb)(glw_view_eval_context_t *ec, struct token *self, 
	    struct token **argv, unsigned int argc);
  void (*ctor)(struct token *self);
  void (*dtor)(glw_root_t *gr, struct token *self);
  token_t *(*preproc)(glw_root_t *gr, errorinfo_t *ei, token_t *t);
} token_func_t;


/**
 *
 */
typedef struct token_attrib {
  const char *name;
  int (*set)(glw_view_eval_context_t *ec, const struct token_attrib *a, 
	     struct token *t);
  int attrib;
  void *fn;
} token_attrib_t;


token_t *glw_view_token_alloc(glw_root_t *gr)  __attribute__ ((malloc));

void glw_view_token_free(glw_root_t *gr, token_t *t);

token_t *glw_view_token_copy(glw_root_t *gr, token_t *src);

token_t *glw_view_lexer(const char *src, errorinfo_t *ei, 
			 rstr_t *f, token_t *prev);


token_t *glw_view_load1(glw_root_t *gr, rstr_t *url,
			errorinfo_t *ei, token_t *prev,
			int *nofile);

int glw_view_parse(token_t *sof, errorinfo_t *ei, glw_root_t *gr);

void glw_view_free_chain(glw_root_t *gr, token_t *t);

const char *token2name(token_t *t);

void glw_view_print_tree(token_t *f, int indent);

token_t *glw_view_function_resolve(glw_root_t *gr, errorinfo_t *ei, token_t *t);

int glw_view_attrib_resolve(token_t *t);

void glw_view_attrib_optimize(token_t *t, glw_root_t *gr);

int glw_view_seterr(errorinfo_t *ei, token_t *b, const char *fmt, ...);

int glw_view_eval_block(token_t *t, glw_view_eval_context_t *ec);

int glw_view_preproc(glw_root_t *gr, token_t *p, errorinfo_t *ei);

token_t *glw_view_clone_chain(glw_root_t *gr, token_t *src, token_t **lp);

void glw_view_cache_flush(glw_root_t *gr);

struct glw_prop_sub_list;
void glw_prop_subscription_destroy_list(glw_root_t *gr, 
					struct glw_prop_sub_list *l);

void glw_prop_subscription_suspend_list(struct glw_prop_sub_list *l);


#endif /* GLW_VIEW_H */
