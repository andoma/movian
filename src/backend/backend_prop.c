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

#include <stdio.h>

#include "showtime.h"
#include "backend.h"
#include "backend_prop.h"
#include "prop/prop.h"
#include "navigator.h"


LIST_HEAD(proppage_list, proppage);
LIST_HEAD(openpage_list, openpage);

static struct proppage_list proppages;
static HTS_MUTEX_DECL(pp_mutex);
static int pp_tally;


/**
 *
 */
typedef struct proppage {
  LIST_ENTRY(proppage) pp_link;

  prop_sub_t *pp_model_sub;
  prop_t *pp_model;

  rstr_t *pp_url;

  struct openpage_list pp_pages;

} proppage_t;



/**
 *
 */
typedef struct openpage {
  LIST_ENTRY(openpage) op_link;
  prop_sub_t *op_page_sub;
  prop_t *op_root;
  proppage_t *op_pp;
} openpage_t;



/**
 *
 */
static void
pp_cb(void *opaque, prop_event_t event, ...)
{
  proppage_t *pp = opaque;
  openpage_t *op;

  if(event != PROP_DESTROYED) 
    return;

  while((op = LIST_FIRST(&pp->pp_pages)) != NULL) {
    LIST_REMOVE(op, op_link);
    op->op_pp = NULL;
    prop_set_int(prop_create(op->op_root, "close"), 1);
  }

  LIST_REMOVE(pp, pp_link);
  prop_ref_dec(pp->pp_model);
  prop_unsubscribe(pp->pp_model_sub);
  rstr_release(pp->pp_url);
  free(pp);
}


/**
 *
 */
rstr_t *
backend_prop_make(prop_t *model, const char *suggest)
{
  proppage_t *pp;
  rstr_t *r;
  hts_mutex_lock(&pp_mutex);

  pp = calloc(1, sizeof(proppage_t));

  if(suggest == NULL) {
    char url[50];
    pp_tally++;
    snprintf(url, sizeof(url), "prop:%d", pp_tally);
    r = rstr_alloc(url);
  } else {
    r = rstr_alloc(suggest);
  }
  pp->pp_url = rstr_dup(r);

  pp->pp_model = prop_ref_inc(model);

  pp->pp_model_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, pp_cb, pp,
		   PROP_TAG_MUTEX, &pp_mutex,
		   PROP_TAG_ROOT, model,
		   NULL);

  LIST_INSERT_HEAD(&proppages, pp, pp_link);
  hts_mutex_unlock(&pp_mutex);
  return r;
}


/**
 *
 */
static void
op_cb(void *opaque, prop_event_t event, ...)
{
  openpage_t *op = opaque;

  if(event != PROP_DESTROYED) 
    return;

  if(op->op_pp != NULL)
    LIST_REMOVE(op, op_link);
  prop_unsubscribe(op->op_page_sub);
  prop_ref_dec(op->op_root);
  free(op);
}


/**
 *
 */
static int
be_prop_open(prop_t *page, const char *url, int sync)
{
  proppage_t *pp;
  openpage_t *op;

  hts_mutex_lock(&pp_mutex);

  LIST_FOREACH(pp, &proppages, pp_link)
    if(!strcmp(rstr_get(pp->pp_url), url))
      break;

  if(pp == NULL) {
    hts_mutex_unlock(&pp_mutex);
    return 1;
  }

  op = calloc(1, sizeof(openpage_t));
  LIST_INSERT_HEAD(&pp->pp_pages, op, op_link);
  op->op_pp = pp;

  op->op_root = prop_ref_inc(page);
  op->op_page_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, op_cb, op,
		   PROP_TAG_MUTEX, &pp_mutex,
		   PROP_TAG_ROOT, page,
		   NULL);

  prop_set(page, "directClose", PROP_SET_INT, 1);

  prop_link(pp->pp_model, prop_create(page, "model"));
  hts_mutex_unlock(&pp_mutex);
  return 0;
}



/**
 *
 */
static backend_t be_prop = {
  .be_flags = BACKEND_OPEN_CHECKS_URI,
  .be_open = be_prop_open,
};

BE_REGISTER(prop);
