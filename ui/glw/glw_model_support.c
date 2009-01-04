/*
 *  GL Widgets, model support functions
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

#include "glw.h"
#include "glw_model.h"
#include "glw_event.h"

refstr_t *
refstr_create(const char *str)
{
  size_t l = strlen(str) + 1;
  refstr_t *r = malloc(sizeof(refstr_t) + l);
  r->refcnt = 1;
  memcpy(r->str, str, l);
  return r;
}

refstr_t *
refstr_dup(refstr_t *r)
{
  r->refcnt++;
  return r;
}

void
refstr_unref(refstr_t *r)
{
  if(r->refcnt == 1) {
    free(r);
  }
  else
    r->refcnt--;
}

const char *
refstr_get(refstr_t *r)
{
  return r->str;
}



/**
 * Free a token.
 * It must be delinked for all lists before
 */
void
glw_model_token_free(token_t *t)
{
  int i;

#ifdef GLW_MODEL_ERRORINFO
  if(t->file != NULL)
    refstr_unref(t->file);
#endif

  switch(t->type) {
  case TOKEN_FUNCTION:
    if(t->t_func->ctor != NULL)
      t->t_func->dtor(t);
    break;

  case TOKEN_FLOAT:
  case TOKEN_INT:
  case TOKEN_VECTOR_FLOAT:
  case TOKEN_OBJECT_ATTRIBUTE:
  case TOKEN_PROPERTY_SUBSCRIPTION:
  case TOKEN_VOID:
  case TOKEN_DIRECTORY:
    break;

  case TOKEN_VECTOR_STRING:
    for(i = 0; i < t->t_elements; i++)
      free(t->t_string_vector[i]);
    break;

  case TOKEN_START:
  case TOKEN_END:
  case TOKEN_HASH:
  case TOKEN_ASSIGNMENT:
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
  case TOKEN_BOOLEAN_AND:
  case TOKEN_BOOLEAN_OR:
  case TOKEN_BOOLEAN_XOR:
  case TOKEN_EXPR:
  case TOKEN_RPN:
  case TOKEN_BLOCK:
  case TOKEN_ARRAY:
  case TOKEN_NOP:
    break;

  case TOKEN_STRING:
  case TOKEN_IDENTIFIER:
  case TOKEN_PROPERTY_NAME:
    free(t->t_string);
    break;

  case TOKEN_EVENT:
    t->t_gem->gem_dtor(t->t_gem);
    break;

  case TOKEN_num:
    abort();

  }
  free(t);
}



/**
 * Clone a token
 */
token_t *
glw_model_token_copy(token_t *src)
{
  token_t *dst = calloc(1, sizeof(token_t));

#ifdef GLW_MODEL_ERRORINFO
  if(src->file != NULL)
    dst->file = refstr_dup(src->file);
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

  case TOKEN_PROPERTY_SUBSCRIPTION:
  case TOKEN_DIRECTORY:
    dst->propsubr = src->propsubr;
    break;

  case TOKEN_FUNCTION:
    dst->t_func = src->t_func;
    if(dst->t_func->ctor != NULL)
      dst->t_func->ctor(dst);
    break;

  case TOKEN_OBJECT_ATTRIBUTE:
    dst->t_attrib = src->t_attrib;
    break;

  case TOKEN_STRING:
  case TOKEN_IDENTIFIER:
  case TOKEN_PROPERTY_NAME:
    dst->t_string = src->t_string ? strdup(src->t_string) : NULL;
    break;

  case TOKEN_START:
  case TOKEN_END:
  case TOKEN_HASH:
  case TOKEN_ASSIGNMENT:
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
  case TOKEN_BOOLEAN_AND:
  case TOKEN_BOOLEAN_OR:
  case TOKEN_BOOLEAN_XOR:
  case TOKEN_EXPR:
  case TOKEN_RPN:
  case TOKEN_BLOCK:
  case TOKEN_ARRAY:
  case TOKEN_NOP:
  case TOKEN_VOID:
    break;

  case TOKEN_VECTOR_FLOAT:
  case TOKEN_VECTOR_STRING:
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
glw_model_free_chain2(token_t *t, int indent)
{
  token_t *n;

  for(; t != NULL; t = n) {
    n = t->next;
    if(t->child != NULL)
      glw_model_free_chain2(t->child, indent + 2);

    //    printf("%*.sFree: %p\n", indent, "",  t);
    //    printf("%*.sFree: %s\n", indent, "",  token2name(t));
    glw_model_token_free(t);
  }
}



/**
 *
 */
void
glw_model_free_chain(token_t *t)
{
  glw_model_free_chain2(t, 0);
}


/**
 *
 */
token_t *
glw_model_clone_chain(token_t *src)
{
  token_t *r = NULL, *d;
  token_t **pp = &r;

  for(; src != NULL; src = src->next) {
    d = glw_model_token_copy(src);
    *pp = d;
    pp = &d->next;

    d->child = glw_model_clone_chain(src->child);
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

  case TOKEN_STRING:        return "<string>";

  case TOKEN_DOT:           return ".";
  case TOKEN_HASH:          return "#";

  case TOKEN_ADD:           return "+";
  case TOKEN_SUB:           return "-";
  case TOKEN_MULTIPLY:      return "*";
  case TOKEN_DIVIDE:        return "/";

  case TOKEN_BLOCK:         return "<block>";
  case TOKEN_EXPR:          return "<infix expr>";
  case TOKEN_RPN:           return "<rpn>";
  case TOKEN_NOP:           return "<nop>";

  case TOKEN_FUNCTION:
    snprintf(buf, sizeof(buf), "%s()", t->t_func->name);
    return buf;

  case TOKEN_PROPERTY_SUBSCRIPTION:
    return "property subscription";

  case TOKEN_PROPERTY_NAME:
    snprintf(buf, sizeof(buf), "<property> %s", t->t_string);
    return buf;

  case TOKEN_OBJECT_ATTRIBUTE:
    snprintf(buf, sizeof(buf), ".%s", t->t_attrib->name);
    return buf;

  case TOKEN_IDENTIFIER:    return t->t_string;
  case TOKEN_ASSIGNMENT:    return "=";

  case TOKEN_FLOAT:
    snprintf(buf, sizeof(buf), "%f", t->t_float);
    return buf;

  case TOKEN_VECTOR_FLOAT:
    buf[0] = '[';
    buf[1] = 0;

    for(i = 0; i < t->t_elements; i++)
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%f ", 
	       t->t_float_vector[i]);

    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "]");
    
    return buf;


  case TOKEN_VECTOR_STRING:
    buf[0] = '[';
    buf[1] = 0;

    for(i = 0; i < t->t_elements; i++)
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\"%s\" ", 
	       t->t_string_vector[i]);

    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "]");
    
    return buf;

  case TOKEN_LEFT_BRACKET:  return "[";
  case TOKEN_RIGHT_BRACKET:  return "]";
  case TOKEN_ARRAY:  return "<array>";

  default:
    abort();
  }
}


/**
 *
 */
void
glw_model_print_tree(token_t *f, int indent)
{
  token_t *c = f;

  while(c != NULL) {
    printf("%*.s%s %p\n", indent, "", token2name(c), c);
    
    if(c->child != NULL) {
      glw_model_print_tree(c->child, indent + 4);
    }
    c = c->next;
  }
}

/**
 *
 */
int
glw_model_seterr(errorinfo_t *ei, token_t *b, const char *fmt, ...)
{
  va_list ap;


  va_start(ap, fmt);

  assert(b != NULL);

  if(ei == NULL) {
    fprintf(stderr, "GLW: %s:%d: ", refstr_get(b->file), b->line);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    return -1;
  }

  vsnprintf(ei->error, sizeof(ei->error), fmt, ap);
  va_end(ap);

  snprintf(ei->file,  sizeof(ei->file),  "%s", refstr_get(b->file));
  ei->line = b->line;
  return -1;
}
