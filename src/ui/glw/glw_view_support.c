/*
 *  GL Widgets, view support functions
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

#include <unistd.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <alloca.h>
#include <limits.h>

#include "glw.h"
#include "glw_view.h"
#include "glw_event.h"

#include "misc/pixmap.h"


token_t *
glw_view_token_alloc(glw_root_t *gr)
{
  return pool_get(gr->gr_token_pool);
}

/**
 * Free a token.
 * It must be delinked for all lists before
 */
void
glw_view_token_free(glw_root_t *gr, token_t *t)
{
#ifdef GLW_VIEW_ERRORINFO
  rstr_release(t->file);
#endif

  switch(t->type) {
  case TOKEN_FUNCTION:
    if(t->t_func->ctor != NULL)
      t->t_func->dtor(gr, t);
    break;

  case TOKEN_PROPERTY_REF:
    prop_ref_dec(t->t_prop);
    break;

  case TOKEN_PROPERTY_OWNER:
    prop_destroy(t->t_prop);
    break;

  case TOKEN_FLOAT:
  case TOKEN_INT:
  case TOKEN_VECTOR_FLOAT:
  case TOKEN_OBJECT_ATTRIBUTE:
  case TOKEN_PROPERTY_SUBSCRIPTION:
  case TOKEN_VOID:
  case TOKEN_DIRECTORY:
  case TOKEN_CSTRING:
    break;

  case TOKEN_START:
  case TOKEN_END:
  case TOKEN_HASH:
  case TOKEN_ASSIGNMENT:
  case TOKEN_COND_ASSIGNMENT:
  case TOKEN_END_OF_EXPR:
  case TOKEN_SEPARATOR:
  case TOKEN_BLOCK_OPEN:
  case TOKEN_BLOCK_CLOSE:
  case TOKEN_LEFT_PARENTHESIS:
  case TOKEN_RIGHT_PARENTHESIS:
  case TOKEN_LEFT_BRACKET:
  case TOKEN_RIGHT_BRACKET:
  case TOKEN_DOT:
  case TOKEN_ADD:
  case TOKEN_SUB:
  case TOKEN_MULTIPLY:
  case TOKEN_DIVIDE:
  case TOKEN_MODULO:
  case TOKEN_DOLLAR:
  case TOKEN_AMPERSAND:
  case TOKEN_BOOLEAN_AND:
  case TOKEN_BOOLEAN_OR:
  case TOKEN_BOOLEAN_XOR:
  case TOKEN_BOOLEAN_NOT:
  case TOKEN_EQ:
  case TOKEN_NEQ:
  case TOKEN_NULL_COALESCE:
  case TOKEN_LT:
  case TOKEN_GT:
  case TOKEN_EXPR:
  case TOKEN_RPN:
  case TOKEN_BLOCK:
  case TOKEN_NOP:
    break;

  case TOKEN_RSTRING:
  case TOKEN_IDENTIFIER:
  case TOKEN_PROPERTY_VALUE_NAME:
  case TOKEN_PROPERTY_CANONICAL_NAME:
    rstr_release(t->t_rstring);
    break;

  case TOKEN_EVENT:
    t->t_gem->gem_dtor(gr, t->t_gem);
    break;

  case TOKEN_LINK:
    rstr_release(t->t_link_rtitle);
    rstr_release(t->t_link_rurl);
    break;

  case TOKEN_num:
    abort();

  }
  pool_put(gr->gr_token_pool, t);
}



/**
 * Clone a token
 */
token_t *
glw_view_token_copy(glw_root_t *gr, token_t *src)
{
  token_t *dst = pool_get(gr->gr_token_pool);

#ifdef GLW_VIEW_ERRORINFO
  dst->file = rstr_dup(src->file);
  dst->line = src->line;
#endif

  dst->type = src->type;

  switch(src->type) {
  case TOKEN_FLOAT:
    dst->t_float = src->t_float;
    break;

  case TOKEN_INT:
    dst->t_int = src->t_int;
    break;

  case TOKEN_PROPERTY_REF:
    dst->t_prop = prop_ref_inc(src->t_prop);
    break;

  case TOKEN_PROPERTY_OWNER:
    dst->t_prop = prop_xref_addref(src->t_prop);
    break;

  case TOKEN_PROPERTY_SUBSCRIPTION:
  case TOKEN_DIRECTORY:
    dst->propsubr = src->propsubr;
    break;

  case TOKEN_FUNCTION:
    dst->t_func = src->t_func;
    if(dst->t_func->ctor != NULL)
      dst->t_func->ctor(dst);
  case TOKEN_LEFT_BRACKET:
    dst->t_num_args = src->t_num_args;
    break;

  case TOKEN_OBJECT_ATTRIBUTE:
    dst->t_attrib = src->t_attrib;
    break;

  case TOKEN_CSTRING:
    dst->t_cstring = src->t_cstring;
    break;

  case TOKEN_RSTRING:
    dst->t_rstrtype = src->t_rstrtype;
    // FALLTHRU
  case TOKEN_IDENTIFIER:
  case TOKEN_PROPERTY_VALUE_NAME:
  case TOKEN_PROPERTY_CANONICAL_NAME:
    dst->t_rstring = rstr_dup(src->t_rstring);
    break;

  case TOKEN_START:
  case TOKEN_END:
  case TOKEN_HASH:
  case TOKEN_ASSIGNMENT:
  case TOKEN_COND_ASSIGNMENT:
  case TOKEN_END_OF_EXPR:
  case TOKEN_SEPARATOR:
  case TOKEN_BLOCK_OPEN:
  case TOKEN_BLOCK_CLOSE:
  case TOKEN_LEFT_PARENTHESIS:
  case TOKEN_RIGHT_PARENTHESIS:
  case TOKEN_RIGHT_BRACKET:
  case TOKEN_DOT:
  case TOKEN_ADD:
  case TOKEN_SUB:
  case TOKEN_MULTIPLY:
  case TOKEN_DIVIDE:
  case TOKEN_MODULO:
  case TOKEN_DOLLAR:
  case TOKEN_AMPERSAND:
  case TOKEN_BOOLEAN_AND:
  case TOKEN_BOOLEAN_OR:
  case TOKEN_BOOLEAN_XOR:
  case TOKEN_BOOLEAN_NOT:
  case TOKEN_EQ:
  case TOKEN_NEQ:
  case TOKEN_LT:
  case TOKEN_GT:
  case TOKEN_NULL_COALESCE:
  case TOKEN_EXPR:
  case TOKEN_RPN:
  case TOKEN_BLOCK:
  case TOKEN_NOP:
  case TOKEN_VOID:
    break;

  case TOKEN_LINK:
    dst->t_link_rtitle = rstr_dup(src->t_link_rtitle);
    dst->t_link_rurl   = rstr_dup(src->t_link_rurl);
    break;

  case TOKEN_VECTOR_FLOAT:
  case TOKEN_num:
  case TOKEN_EVENT:
    abort();
  }
  return dst;
}

/**
 *
 */
static void
glw_view_free_chain2(glw_root_t *gr, token_t *t, int indent)
{
  token_t *n;

  for(; t != NULL; t = n) {
    n = t->next;
    if(t->child != NULL)
      glw_view_free_chain2(gr, t->child, indent + 2);

    //    printf("%*.sFree: %p\n", indent, "",  t);
    //    printf("%*.sFree: %s\n", indent, "",  token2name(t));
    glw_view_token_free(gr, t);
  }
}



/**
 *
 */
void
glw_view_free_chain(glw_root_t *gr, token_t *t)
{
  glw_view_free_chain2(gr, t, 0);
}


/**
 *
 */
token_t *
glw_view_clone_chain(glw_root_t *gr, token_t *src)
{
  token_t *r = NULL, *d;
  token_t **pp = &r;

  for(; src != NULL; src = src->next) {
    d = glw_view_token_copy(gr, src);
    *pp = d;
    pp = &d->next;

    d->child = glw_view_clone_chain(gr, src->child);
  }
  return r;
}



/**
 *
 */
const char *
token2name(token_t *t)
{
  static char buf[200];
  int i;

  if(t == NULL)
    return "(null)";
  switch(t->type) {
  case TOKEN_START:         return "<start>";
  case TOKEN_END:           return "<end>";
  case TOKEN_END_OF_EXPR:   return ";";
  case TOKEN_BLOCK_OPEN:    return "{";
  case TOKEN_BLOCK_CLOSE:   return "}";
  case TOKEN_SEPARATOR:     return ",";

  case TOKEN_LEFT_PARENTHESIS:  return "(";
  case TOKEN_RIGHT_PARENTHESIS:  return ")";

  case TOKEN_RSTRING: return rstr_get(t->t_rstring);
  case TOKEN_CSTRING: return t->t_cstring;


  case TOKEN_DOT:           return ".";
  case TOKEN_HASH:          return "#";

  case TOKEN_ADD:           return "+";
  case TOKEN_SUB:           return "-";
  case TOKEN_MULTIPLY:      return "*";
  case TOKEN_DIVIDE:        return "/";
  case TOKEN_BOOLEAN_NOT:   return "NOT";
  case TOKEN_BOOLEAN_AND:   return "AND";
  case TOKEN_BOOLEAN_OR:    return "OR";
  case TOKEN_BOOLEAN_XOR:   return "XOR";

  case TOKEN_BLOCK:         return "<block>";
  case TOKEN_EXPR:          return "<infix expr>";
  case TOKEN_RPN:           return "<rpn>";
  case TOKEN_NOP:           return "<nop>";

  case TOKEN_FUNCTION:
    snprintf(buf, sizeof(buf), "%s()", t->t_func->name);
    return buf;

  case TOKEN_PROPERTY_SUBSCRIPTION:
    return "property subscription";

  case TOKEN_PROPERTY_REF:
    return "property ref";

  case TOKEN_PROPERTY_VALUE_NAME:
  case TOKEN_PROPERTY_CANONICAL_NAME:
    snprintf(buf, sizeof(buf), "<property> %s", rstr_get(t->t_rstring));
    return buf;

  case TOKEN_OBJECT_ATTRIBUTE:
    snprintf(buf, sizeof(buf), ".%s", t->t_attrib->name);
    return buf;

  case TOKEN_IDENTIFIER:    return rstr_get(t->t_rstring);
  case TOKEN_ASSIGNMENT:    return "=";

  case TOKEN_FLOAT:
    snprintf(buf, sizeof(buf), "%ff", t->t_float);
    return buf;

  case TOKEN_INT:
    snprintf(buf, sizeof(buf), "%d", t->t_int);
    return buf;

  case TOKEN_VOID:
    snprintf(buf, sizeof(buf), "(void)");
    return buf;

  case TOKEN_VECTOR_FLOAT:
    buf[0] = '[';
    buf[1] = 0;

    for(i = 0; i < t->t_elements; i++)
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%f ", 
	       t->t_float_vector_int[i]);

    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "]");
    
    return buf;

  case TOKEN_LEFT_BRACKET:  return "[";
  case TOKEN_RIGHT_BRACKET:  return "]";

  default:
    snprintf(buf, sizeof(buf), "Tokentype<%d>", t->type);
    return buf;
  }
}


/**
 *
 */
void
glw_view_print_tree(token_t *f, int indent)
{
  token_t *c = f;

  while(c != NULL) {
    printf("%*.s%s %p\n", indent, "", token2name(c), c);
    
    if(c->child != NULL) {
      glw_view_print_tree(c->child, indent + 4);
    }
    c = c->next;
  }
}

/**
 *
 */
int
glw_view_seterr(errorinfo_t *ei, token_t *b, const char *fmt, ...)
{
  char buf[PATH_MAX];

  va_list ap;
  va_start(ap, fmt);

  assert(b != NULL);

  if(ei == NULL) {
#ifdef GLW_VIEW_ERRORINFO
    snprintf(buf, sizeof(buf), "GLW: %s:%d", rstr_get(b->file), b->line);
#else
    snprintf(buf, sizeof(buf), "GLW: file?:##");
#endif
    tracev(TRACE_NO_PROP, TRACE_ERROR, buf, fmt, ap);
    return -1;
  }

  vsnprintf(ei->error, sizeof(ei->error), fmt, ap);
  va_end(ap);

#ifdef GLW_VIEW_ERRORINFO
  snprintf(ei->file,  sizeof(ei->file),  "%s", rstr_get(b->file));
  ei->line = b->line;
#else
  snprintf(ei->file,  sizeof(ei->file),  "file?");
#endif
  return -1;
}
