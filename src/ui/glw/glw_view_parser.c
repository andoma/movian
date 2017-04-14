/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
#include <assert.h>
#include "glw_view.h"
#include "i18n.h"

#include <stdio.h>
static void glw_view_nls_string(token_t *t, const char *str);

static int parse_block(token_t *first, errorinfo_t *ei, token_type_t term,
		       glw_root_t *gr);


typedef struct tokenqueue {
  token_t *head, *tail;
} tokenqueue_t;

/**
 *
 */
static token_t *
tokenqueue_enqueue(tokenqueue_t *q, token_t *t, token_t *f)
{
  token_t *r = t->next;
  t->next = NULL;

  if(q->head == NULL) {
    q->head = q->tail = t;
  } else {
    q->tail->next = t;
    q->tail = t;
  }

  if(f != NULL && !(f->t_num_args & 1))
    f->t_num_args++;

  return r;
}

/**
 *
 */
static token_t *
tokenstack_push(token_t **s, token_t *t)
{
  token_t *r = t->next;

  t->next = *s;
  *s = t;
  return r;
}


/**
 *
 */
static token_t *
tokenstack_pop(token_t **s)
{
  token_t *r = *s;

  *s = r->next;
  return r;
}


/**
 *
 */
static const int tokenprecedence[TOKEN_num] = {
  [TOKEN_ASSIGNMENT]    = 1,
  [TOKEN_COND_ASSIGNMENT]    = 1,
  [TOKEN_REF_ASSIGNMENT]    = 1,
  [TOKEN_DEBUG_ASSIGNMENT]    = 1,
  [TOKEN_LINK_ASSIGNMENT]    = 1,

  [TOKEN_COLON] = 2,

  [TOKEN_QUESTIONMARK] = 3,
  [TOKEN_TENARY] = 3,

  [TOKEN_NULL_COALESCE] = 4,

  [TOKEN_BOOLEAN_OR] = 5,
  [TOKEN_BOOLEAN_AND]= 6,
  [TOKEN_BOOLEAN_XOR]= 7,
  [TOKEN_EQ]         = 8,
  [TOKEN_NEQ]        = 8,
  [TOKEN_LT]         = 8,
  [TOKEN_GT]         = 8,
  [TOKEN_ADD]        = 9,
  [TOKEN_SUB]        = 9,
  [TOKEN_MULTIPLY]   = 10,
  [TOKEN_DIVIDE]     = 10,
  [TOKEN_MODULO]     = 11,
  [TOKEN_BLOCK]      = 12,
  [TOKEN_BOOLEAN_NOT]= 13,
};


/**
 * Convert an infix expression into an RPN expression
 *
 * Based on: http://en.wikipedia.org/wiki/Shunting_yard_algorithm
 *
 * Modified to keep track of number of arguments passed to a function.
 * This is done by using t_num_args.
 *  If it is even, and a value is to be pushed, it is increased by 1
 *  If we stumble on a ',' (TOKEN_SEPARATOR) we increase it by 1 if
 *  it is odd. If it is even we've stumbled on a syntax error (two
 *  commas in a row)
 *
 *  Finally, the number of arguments is 1 + (num_args / 2)
 */

static int
parse_shunting_yard(token_t *expr, errorinfo_t *ei, glw_root_t *gr)
{
  tokenqueue_t outq = {NULL, NULL};
  token_t *stack = NULL;
  token_t *t = expr->child, *x;
  token_t *curfunc = NULL;

  int type = TOKEN_PURE_RPN;

  expr->child = NULL;  /* Avoid duplicate free if we bail out */

  while(t != NULL) {

    switch(t->type) {

    case TOKEN_BLOCK:
    case TOKEN_PROPERTY_REF:
    case TOKEN_PROPERTY_NAME:
      type = TOKEN_RPN;
      /* FALLTHRU */
    case TOKEN_RESOLVED_ATTRIBUTE:
    case TOKEN_UNRESOLVED_ATTRIBUTE:
    case TOKEN_RSTRING:
    case TOKEN_CSTRING:
    case TOKEN_FLOAT:
    case TOKEN_EM:
    case TOKEN_INT:
    case TOKEN_IDENTIFIER:
    case TOKEN_VOID:
      t = tokenqueue_enqueue(&outq, t, curfunc);
      continue;

    case TOKEN_SEPARATOR:
      while(stack && (stack->type != TOKEN_LEFT_PARENTHESIS &&
		      stack->type != TOKEN_LEFT_BRACKET))
	tokenqueue_enqueue(&outq, tokenstack_pop(&stack), curfunc);
      if(curfunc != NULL) {
	if(!(curfunc->t_num_args & 1)) {
	  glw_view_seterr(ei, t, "Unexpected separator '',''");
 	  goto err;
	}
	curfunc->t_num_args++;
      }
      break;

    case TOKEN_ADD:
    case TOKEN_SUB:
    case TOKEN_MULTIPLY:
    case TOKEN_DIVIDE:
    case TOKEN_MODULO:
    case TOKEN_BOOLEAN_OR:
    case TOKEN_BOOLEAN_AND:
    case TOKEN_BOOLEAN_XOR:
    case TOKEN_ASSIGNMENT:
    case TOKEN_COND_ASSIGNMENT:
    case TOKEN_REF_ASSIGNMENT:
    case TOKEN_DEBUG_ASSIGNMENT:
    case TOKEN_LINK_ASSIGNMENT:
    case TOKEN_EQ:
    case TOKEN_NULL_COALESCE:
    case TOKEN_NEQ:
    case TOKEN_LT:
    case TOKEN_GT:
    case TOKEN_BOOLEAN_NOT:
      while(stack && tokenprecedence[t->type] <= tokenprecedence[stack->type])
	tokenqueue_enqueue(&outq, tokenstack_pop(&stack), NULL);
      /* FALLTHRU */
    case TOKEN_LEFT_PARENTHESIS:
      t = tokenstack_push(&stack, t);
      continue;

    case TOKEN_QUESTIONMARK:
      while(stack && tokenprecedence[t->type] < tokenprecedence[stack->type])
	tokenqueue_enqueue(&outq, tokenstack_pop(&stack), NULL);

      t = tokenstack_push(&stack, t);
      continue;

    case TOKEN_COLON:
      while(stack && stack->type != TOKEN_QUESTIONMARK)
	tokenqueue_enqueue(&outq, tokenstack_pop(&stack), curfunc);

      if(stack == NULL) {
	glw_view_seterr(ei, t, "Unbalanced ?: operator");
	goto err;
      }

      glw_view_token_free(gr, tokenstack_pop(&stack));
      t->type = TOKEN_TENARY;
      t = tokenstack_push(&stack, t);
      continue;

    case TOKEN_FUNCTION:
      type = TOKEN_RPN;
      /* FALLTHRU */
    case TOKEN_LEFT_BRACKET:
      t->tmp = curfunc;
      curfunc = t;

      t = tokenstack_push(&stack, t);
      continue;

    case TOKEN_RIGHT_PARENTHESIS:
      while(stack && stack->type != TOKEN_LEFT_PARENTHESIS)
	tokenqueue_enqueue(&outq, tokenstack_pop(&stack), curfunc);

      if(stack == NULL) {
	glw_view_seterr(ei, t, "Unbalanced parentheses");
	goto err;
      }
      glw_view_token_free(gr, tokenstack_pop(&stack));

      if(stack && stack->type == TOKEN_FUNCTION) {
	assert(stack == curfunc);

	if(stack->t_num_args && !(stack->t_num_args & 1)) {
	  glw_view_seterr(ei, t, "Unexpected separator '',''");
 	  goto err;
	}

	stack->t_num_args = (1 + stack->t_num_args) / 2;
	curfunc = stack->tmp;
	tokenqueue_enqueue(&outq, tokenstack_pop(&stack), curfunc);
      }
      break;
      
    case TOKEN_RIGHT_BRACKET:
      while(stack && stack->type != TOKEN_LEFT_BRACKET)
	tokenqueue_enqueue(&outq, tokenstack_pop(&stack), curfunc);

      if(stack == NULL) {
	glw_view_seterr(ei, t, "Unbalanced brackets");
	goto err;
      }
      assert(stack == curfunc);

      if(stack->t_num_args && !(stack->t_num_args & 1)) {
	glw_view_seterr(ei, t, "Unexpected separator '',''");
	goto err;
      }

      stack->t_num_args = (1 + stack->t_num_args) / 2;
      curfunc = stack->tmp;
      tokenqueue_enqueue(&outq, tokenstack_pop(&stack), curfunc);
      break;


    default:
      glw_view_seterr(ei, t, "Unexpected symbol in RPN processor");
      goto err;
    }
    x = t;
    t = t->next;
    glw_view_token_free(gr, x);
  }

  while(stack) {
    if(stack->type == TOKEN_LEFT_PARENTHESIS || 
       stack->type == TOKEN_RIGHT_PARENTHESIS) {
      glw_view_seterr(ei, stack, "Unbalanced parentheses");
      goto err;
    }
    tokenqueue_enqueue(&outq, tokenstack_pop(&stack), curfunc);
  }


  expr->child = outq.head;
  /*
   * Assignments to the 'style' property are always pure because
   * delegating that to the target of the style will result in a cycle.
   *
   * The check ifself is a bit ugly though
   */
  if(expr->child->type == TOKEN_RESOLVED_ATTRIBUTE &&
     !strcmp(expr->child->t_attrib->name, "style"))
    type = TOKEN_PURE_RPN;

  expr->type = type;
  return 0;

 err:

  while(stack != NULL)
    glw_view_token_free(gr, tokenstack_pop(&stack));

  while(outq.head != NULL)
    glw_view_token_free(gr, tokenstack_pop(&outq.head));

  return -1;
}


/**
 * Optimize the common case of
 *
 *   attribute: static-value;
 *
 * which is represented as
 *
 *  RPN -> next
 *   |
 *   +-- [attribute] -- [value] -- [assignment operator]
 *
 * into
 *
 *  [value] -> next
 *
 *  and we tag value with pointer to the attribute
 *
 * This special form is understood by the evaluator (see glw_view_eval_block())
 *
 */
static void
optimize_attribute_assignment(token_t *t, token_t *prev, glw_root_t *gr)
{
  if(t->type == TOKEN_PURE_RPN) {
    token_t *att = t->child;
    if(att->type == TOKEN_RESOLVED_ATTRIBUTE &&
       att->next != NULL && att->next->next != NULL &&
       att->next->next->next == NULL &&
       att->next->next->type == TOKEN_ASSIGNMENT) {

      token_t *val = att->next;
      token_t *ass = att->next->next;
      if(val->type == TOKEN_FLOAT ||
         val->type == TOKEN_INT ||
         val->type == TOKEN_VECTOR_FLOAT ||
         val->type == TOKEN_VOID ||
         val->type == TOKEN_CSTRING ||
         val->type == TOKEN_RSTRING ||
         val->type == TOKEN_IDENTIFIER) {

        assert(att->t_attrib != NULL);
        val->t_attrib = att->t_attrib;

        val->next = t->next;
        prev->next = val;
        
        glw_view_token_free(gr, ass);
        glw_view_token_free(gr, att);
        glw_view_token_free(gr, t);
      }
    }
  }
}


/**
 *
 */
static int
propnamecmp(const char *a[16], const char *b[16])
{
  for(int i = 0; i < 16; i++) {
    if(a[i] == NULL && b[i] == NULL)
      return 1;
    if(a[i] == NULL || b[i] == NULL)
      return 0;

    if(strcmp(a[i], b[i]))
      return 0;
  }
  abort();
}



/**
 * Within each RPN, assign an local id to each distinct property name
 *
 * This is so we later can merge subscriptions referring to same property
 * name within the same RPN. see subscribe_prop() in glw_view_eval.c
 */
static void
scan_prop_names(token_t *rpn, token_t *prev, glw_root_t *gr)
{
  int idcnt = 0;
  token_t *t, *u;
  if(rpn->type != TOKEN_RPN)
    return;

  for(t = rpn->child; t != NULL; t = t->next) {
    if(t->type == TOKEN_PROPERTY_NAME) {
      const char *tname[16];
      int tname_is_set = 0; // Call propname_to_array lazy

      for(u = rpn->child; u != t; u = u->next) {
        if(u->type == TOKEN_PROPERTY_NAME) {
          if(!tname_is_set) {
            glw_propname_to_array(tname, t);
            tname_is_set = 1;
          }

          const char *uname[16];
          glw_propname_to_array(uname, u);
          if(propnamecmp(tname, uname))
            break;
        }
      }
      if(u == t) {

        t->t_prop_name_id = ++idcnt;
      } else {
        t->t_prop_name_id = u->t_prop_name_id;
      }
    }
  }
}


/**
 *
 */
static int
parse_prep_expression(token_t *expr, errorinfo_t *ei, glw_root_t *gr)

{
  token_t *t = expr->child, *t0, *t1, *t2;

  while(t != NULL) {

    t1 = t->next;

    /**
     * Transform [$&]foo.bar.etc into a property chain
     */
    if((t->type == TOKEN_DOLLAR ||
	t->type == TOKEN_AMPERSAND) 
       && t1 != NULL && t1->type == TOKEN_IDENTIFIER) {
      t0 = t2 = t;
      if(t->type == TOKEN_AMPERSAND) {
        t0->t_flags |= TOKEN_F_CANONICAL_PATH;
        TRACE(TRACE_INFO, "GLW", "%s:%d: Form &property is deprecated",
               rstr_get(t->file), t->line);
      }
      t0->type = TOKEN_PROPERTY_NAME;

      t0->next = t1->next;
      t0->t_elements = 1;
      t0->t_pnvec[0] = t1->t_rstring;
      t1->t_rstring = NULL;

      glw_view_token_free(gr, t1);

      t = t0->next;
      while(t != NULL && t->type == TOKEN_DOT) {
	t1 = t->next;
	if(t1 == NULL || t1->type != TOKEN_IDENTIFIER) {
	  glw_view_seterr(ei, t, "Invalid object dereference");
	  return -1;
	}

	t0->next = t1->next;

        if(t2->t_elements < TOKEN_PROPERTY_NAME_VEC_SIZE) {
          // Can still fit stuff in previous token
          t2->t_pnvec[t2->t_elements++] = t1->t_rstring;
          t1->t_rstring = NULL;
          glw_view_token_free(gr, t);
          glw_view_token_free(gr, t1);
        } else {
          t1->next = NULL;
          t1->type = TOKEN_PROPERTY_NAME;
          t1->t_elements = 1;
          glw_view_token_free(gr, t);
          t2->child = t1;
          t2 = t1;
        }
	t = t0->next;
      }
      continue;
    }


    /**
     * Transform int/float "em" into TOKEN_EM
     */

    if((t->type == TOKEN_FLOAT || t->type == TOKEN_INT) &&
       t1 != NULL && t1->type == TOKEN_IDENTIFIER &&
       !strcmp(rstr_get(t1->t_rstring), "em")) {

      if(t->type == TOKEN_INT)
        t->t_float = t->t_int;

      t->type = TOKEN_EM;

      t->next = t1->next;
      t = t1->next;
      glw_view_token_free(gr, t1);
      continue;
    }

    /**
     * Transform '.name' into just 'name' and set its type to
     * object attribute
     */
    if(t->type == TOKEN_DOT) {
      if(t1 == NULL || t1->type != TOKEN_IDENTIFIER) {
	glw_view_seterr(ei, t, "Invalid object attribute reference");
	return -1;
      }
      
      t->t_rstring = t1->t_rstring;
      t1->t_rstring = NULL;

      glw_view_attrib_resolve(t);
      t->next = t1->next;
      t = t1->next;
      glw_view_token_free(gr, t1);
      continue;
    }


    /**
     * Transform 'name: ' into a attribute assignment
     */
    if(t->type == TOKEN_IDENTIFIER &&
       t1 != NULL && t1->type == TOKEN_COLON) {
      glw_view_attrib_resolve(t);
      t1->type = TOKEN_ASSIGNMENT;
      continue;
    }

    if(t->type == TOKEN_IDENTIFIER && t1 != NULL) {
      
      /**
       * Check if identifer is a function (i.e, it is followed by a
       * parenthesis)
       */
      if(t1->type == TOKEN_LEFT_PARENTHESIS) {
	/* Yep, try to resolve the identifier into a function */


	if(!strcmp(rstr_get(t->t_rstring), "_") &&
           // The _() function is special as it refers to i18n translations
	   t1->next->type == TOKEN_RSTRING &&
	   t1->next->next->type == TOKEN_RIGHT_PARENTHESIS) {

	  glw_view_nls_string(t, rstr_get(t1->next->t_rstring));

	  t->next = t1->next->next->next;
	  glw_view_token_free(gr, t1->next->next);
	  glw_view_token_free(gr, t1->next);
	  glw_view_token_free(gr, t1);

	  t = t->next;

	} else {
          // Resolve as ordinary function
          token_t *n = glw_view_function_resolve(gr, ei, t);

          if(n == NULL)
            return -1;
          t = n;
	}
	continue;
      }
    }
    t = t1;
  }
  return parse_shunting_yard(expr, ei, gr);
}





/**
 *
 */
static int
parse_one_expression(token_t *prev, token_t *first, errorinfo_t *ei,
		     glw_root_t *gr)
{
  token_t *t = first, *l = NULL;
  int balance = 0;

  while(t != NULL) {

    switch(t->type) {
    case TOKEN_END:
      glw_view_seterr(ei, first, "Unexpected end of file");
      return -1;

    case TOKEN_BLOCK_OPEN:
      if(parse_block(t, ei, TOKEN_BLOCK_CLOSE, gr))
	return -1;
      break;

    case TOKEN_END_OF_EXPR:
      t->type = TOKEN_EXPR;
      if(l == NULL) {
	t->type = TOKEN_NOP;
	/* Empty expression */
	return 0;
      }

      l->next = NULL;
      prev->next = t;
      t->child = first;
      if(parse_prep_expression(t, ei, gr))
        return -1;

      scan_prop_names(t, prev, gr);
      optimize_attribute_assignment(t, prev, gr);
      return 0;

    case TOKEN_BLOCK_CLOSE:
      glw_view_seterr(ei, t, "Unexpected '}'");
      return -1;

    case TOKEN_LEFT_PARENTHESIS:
      balance++;
      break;

    case TOKEN_RIGHT_PARENTHESIS:
      balance--;
      break;

    default:
      break;
    }

    l = t;
    t = t->next;
  }
  return -1;
}

/**
 * A block is a set of {} (TOKEN_BLOCK_OPEN & TOKEN_BLOCK_CLOSE)
 * or it might be the entire file (TOKEN_START_OF_FILE & TOKEN_END_OF_FILE)
 *
 * A block consists of N expressions inside the block
 */
static int
parse_block(token_t *first, errorinfo_t *ei, token_type_t term,
	    glw_root_t *gr)
{
  token_t *p;

  first->type = TOKEN_BLOCK;
  if(first->next->type == term) {
    // Empty block
    p = first->next;
    first->next = p->next;
    glw_view_token_free(gr, p);
    return 0;
  }

  p = first;

  while(p != NULL && p->next != NULL) {

    if(p->next->type == term) {

      first->child = first->next;
      first->next = p->next->next;
      glw_view_token_free(gr, p->next);
      p->next = NULL;
      glw_view_attrib_optimize(first->child, gr);
      return 0;
    }

    if(parse_one_expression(p, p->next, ei, gr))
      return -1;

    p = p->next;
  }

  glw_view_seterr(ei, first, "Unbalanced block");
  return -1;
}


/**
 *
 */
int
glw_view_parse(token_t *sof, errorinfo_t *ei, glw_root_t *gr)
{
  return parse_block(sof, ei, TOKEN_END, gr);
}


/**
 * Transform a string token into a translated property
 */
static void
glw_view_nls_string(token_t *t, const char *str)
{
  prop_t *p = nls_get_prop(str);
  rstr_release(t->t_rstring);
  t->type = TOKEN_PROPERTY_REF;
  t->t_prop = prop_ref_inc(p);
}
