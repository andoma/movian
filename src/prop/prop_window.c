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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <unistd.h>
#include <math.h>

#include "arch/atomic.h"

#include "prop_i.h"
#include "prop_window.h"
#include "misc/str.h"

TAILQ_HEAD(prop_window_node_queue, prop_window_node);

/**
 *
 */
struct prop_window {
  prop_sub_t *pw_srcsub;
  prop_t *pw_dst;
  int pw_win_start;
  int pw_win_length;

  int pw_count;

  struct prop_window_node_queue pw_queue;

  int pw_pos_valid;

};


/**
 *
 */
typedef struct prop_window_node {
  TAILQ_ENTRY(prop_window_node) pwn_link;
  prop_t *pwn_in;
  prop_t *pwn_out;
  int pwn_pos;
} prop_window_node_t;



/**
 *
 */
static void 
pw_clear(prop_window_t *pw)
{
  prop_window_node_t *pwn;

  while((pwn = TAILQ_FIRST(&pw->pw_queue)) != NULL) {
    TAILQ_REMOVE(&pw->pw_queue, pwn, pwn_link);
    if(pwn->pwn_out != NULL)
      prop_destroy0(pwn->pwn_out);
    prop_tag_clear(pwn->pwn_in, pw);
    free(pwn);
  }
}


/**
 *
 */
static void
add_child(prop_window_t *pw, prop_t *p, prop_t *before)
{
  prop_window_node_t *pwn = malloc(sizeof(prop_window_node_t));
  TAILQ_INSERT_TAIL(&pw->pw_queue, pwn, pwn_link);
  pwn->pwn_in = p;

  pwn->pwn_pos = pw->pw_count;
  prop_tag_set(p, pw, pwn);

  if(pw->pw_count >= pw->pw_win_start &&
     pw->pw_count < pw->pw_win_start + pw->pw_win_length) {

    pwn->pwn_out = prop_make(NULL, 0, NULL);
    prop_link0(p, pwn->pwn_out, NULL, 0, 0);
    prop_set_parent0(pwn->pwn_out, pw->pw_dst, NULL, NULL);

  } else {
    pwn->pwn_out = NULL;
  }
  pw->pw_count++;
}


/**
 *
 */
static void
move_child(prop_window_t *pw, prop_window_node_t *pwn,
	   prop_window_node_t *before)
{
  

}


/**
 *
 */
static void
del_child(prop_window_t *pw, prop_window_node_t *pwn)
{
  TAILQ_REMOVE(&pw->pw_queue, pwn, pwn_link);
  if(pwn->pwn_out != NULL)
    prop_destroy0(pwn->pwn_out);
  free(pwn);
}

/**
 *
 */
static void
src_cb(void *opaque, prop_event_t event, ...)
{
  prop_window_t *pw = opaque;
  va_list ap;
  prop_vec_t *pv;
  prop_t *p, *q;
  int i;

  va_start(ap, event);

  switch(event) {
  case PROP_ADD_CHILD:
    add_child(pw, va_arg(ap, prop_t *), NULL);
    break;

  case PROP_ADD_CHILD_VECTOR:
  case PROP_ADD_CHILD_VECTOR_DIRECT:
    pv = va_arg(ap, prop_vec_t *);
    for(i = 0; i < prop_vec_len(pv); i++)
      add_child(pw, prop_vec_get(pv, i), NULL);
    break;

  case PROP_MOVE_CHILD:
    p = va_arg(ap, prop_t *);
    q = va_arg(ap, prop_t *);
    move_child(pw, prop_tag_get(p, pw), q ? prop_tag_get(q, pw) : NULL);
    break;

  case PROP_SET_DIR:
    break;

  case PROP_DEL_CHILD:
    del_child(pw, prop_tag_clear(va_arg(ap, prop_t *), pw));
    break;

  case PROP_SET_VOID:
    pw_clear(pw);
    break;

  case PROP_HAVE_MORE_CHILDS_YES:
  case PROP_HAVE_MORE_CHILDS_NO:
  case PROP_WANT_MORE_CHILDS:
    break;

  case PROP_SELECT_CHILD:
    break;

  default:
    fprintf(stderr, "prop_window can't handle event %d\n", event);
    abort();
  }
}


/**
 *
 */
prop_window_t *
prop_window_create(prop_t *dst, prop_t *src,
		   unsigned int start, unsigned int length,
		   int flags)
{
  prop_window_t *pw = calloc(1, sizeof(prop_window_t));

  pw->pw_dst = flags & PROP_WINDOW_TAKE_DST_OWNERSHIP
    ? dst : prop_xref_addref(dst);

  pw->pw_win_start = start;
  pw->pw_win_length = length;
  TAILQ_INIT(&pw->pw_queue);

  hts_mutex_lock(&prop_mutex);

  pw->pw_srcsub = prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_DONTLOCK,
				 PROP_TAG_CALLBACK, src_cb, pw,
				 PROP_TAG_ROOT, src,
				 NULL);
  hts_mutex_unlock(&prop_mutex);
  return pw;
}



/**
 *
 */
void
prop_window_destroy(prop_window_t *pw)
{
  hts_mutex_lock(&prop_mutex);

  pw_clear(pw);
  prop_unsubscribe0(pw->pw_srcsub);
  prop_destroy0(pw->pw_dst);

  hts_mutex_unlock(&prop_mutex);

  free(pw);
}
