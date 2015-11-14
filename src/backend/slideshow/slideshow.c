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
#include <unistd.h>
#include <assert.h>
#include "main.h"
#include "prop/prop_nodefilter.h"
#include "prop/prop_concat.h"
#include "navigator.h"
#include "misc/lockmgr.h"
#include "misc/callout.h"
#include "backend/backend.h"

TAILQ_HEAD(slideshow_item_queue, slideshow_item);


/**
 *
 */
typedef struct slideshow {
  lockmgr_t ss_lockmgr;
  prop_sub_t *ss_node_sub;
  prop_sub_t *ss_event_sub;

  prop_t *ss_model;
  prop_t *ss_nodes;

  struct slideshow_item_queue ss_items;

  struct slideshow_item *ss_start;

  struct slideshow_item *ss_current;

  int ss_loaded; // If we don't get any updates for 1 second we set this

  callout_t ss_callout;

} slideshow_t;


/**
 *
 */
typedef struct slideshow_item {
  TAILQ_ENTRY(slideshow_item) ssi_link;

  prop_t *ssi_output_root;

  int ssi_is_image;
  rstr_t *ssi_url;

  prop_t *ssi_source;

  prop_sub_t *ssi_sub_type;
  prop_sub_t *ssi_sub_url;

  slideshow_t *ssi_ss;

} slideshow_item_t;


static void slideshow_arm(slideshow_t *ss);



/**
 *
 */
static void
slideshow_release(void *aux)
{
  slideshow_t *ss = aux;
  if(lockmgr_release(&ss->ss_lockmgr))
    return;

  free(ss);
}


/**
 *
 */
static void
slideshow_advance(slideshow_t *ss, int reverse)
{
  slideshow_item_t *ssi = ss->ss_current;

  if(ssi == NULL) {

    if(reverse)
      ssi = TAILQ_LAST(&ss->ss_items, slideshow_item_queue);
    else
      ssi = TAILQ_FIRST(&ss->ss_items);

    ss->ss_current =  ssi;
    if(ssi == NULL)
      return;
  }

  while(1) {
    if(reverse) {
      ssi = TAILQ_PREV(ssi, slideshow_item_queue, ssi_link);
      if(ssi == NULL)
        ssi = TAILQ_LAST(&ss->ss_items, slideshow_item_queue);

    } else {
      ssi = TAILQ_NEXT(ssi, ssi_link);
      if(ssi == NULL)
        ssi = TAILQ_FIRST(&ss->ss_items);
    }

    if(ssi == ss->ss_current)
      return; // Wrapped around, don't advance

    if(ssi->ssi_output_root != NULL) {
      ss->ss_current = ssi;
      prop_select(ssi->ssi_output_root);
      prop_suggest_focus(ssi->ssi_source);
      return;
    }
  }
}


/**
 *
 */
static void
slideshow_item_destroy(slideshow_item_t *ssi, int no_advance)
{
  slideshow_t *ss = ssi->ssi_ss;
  if(ss->ss_start == ssi)
    ss->ss_start = NULL;

  if(ss->ss_current == ssi) {
    if(!no_advance)
      slideshow_advance(ss, 0);
    ss->ss_current = NULL;
  }

  TAILQ_REMOVE(&ss->ss_items, ssi, ssi_link);
  prop_destroy(ssi->ssi_output_root);
  rstr_release(ssi->ssi_url);
  prop_unsubscribe(ssi->ssi_sub_type);
  prop_unsubscribe(ssi->ssi_sub_url);
  prop_ref_dec(ssi->ssi_output_root);
  prop_ref_dec(ssi->ssi_source);
  free(ssi);
}


/**
 * Remove all items, if 'all' is set we also remove the start item
 */
static void
slideshow_clear(slideshow_t *ss, int all)
{
  slideshow_item_t *ssi, *next;

  for(ssi = TAILQ_FIRST(&ss->ss_items); ssi != NULL; ssi = next) {
    next = TAILQ_NEXT(ssi, ssi_link);
    if(!all && ssi == ss->ss_start)
      continue;
    prop_tag_clear(ssi->ssi_source, ss);
    slideshow_item_destroy(ssi, all);
  }
}


/**
 *
 */
static void
slideshow_destroy(slideshow_t *ss)
{
  slideshow_clear(ss, 1);
  prop_unsubscribe(ss->ss_node_sub);
  prop_unsubscribe(ss->ss_event_sub);
  prop_ref_dec(ss->ss_model);
  prop_ref_dec(ss->ss_nodes);
  callout_disarm(&ss->ss_callout);
  assert(ss->ss_start == NULL);
}


/**
 *
 */
static void
ss_timer(callout_t *c, void *opaque)
{
  slideshow_t *ss = opaque;
  if(!ss->ss_loaded) {
    ss->ss_loaded = 1;
  } else {
    slideshow_advance(ss, 0);
  }
  slideshow_arm(ss);
}



/**
 *
 */
static void
slideshow_arm(slideshow_t *ss)
{
  callout_arm_managed(&ss->ss_callout, ss_timer, ss, 5000000, lockmgr_handler);
}

/**
 *
 */
static prop_t *
ssi_get_before(slideshow_item_t *ssi)
{
  slideshow_item_t *before = TAILQ_NEXT(ssi, ssi_link);

  while(before != NULL && before->ssi_output_root == NULL) {
    before = TAILQ_NEXT(before, ssi_link);
  }
  return before ? before->ssi_output_root : NULL;
}


/**
 *
 */
static void
ssi_update_order(slideshow_item_t *ssi)
{
  prop_move(ssi->ssi_output_root, ssi_get_before(ssi));
}


/**
 *
 */
static void
ssi_update_output(slideshow_item_t *ssi)
{
  slideshow_t *ss = ssi->ssi_ss;
  if(ssi->ssi_is_image && ssi->ssi_url) {

    if(ss->ss_start != NULL && rstr_eq(ss->ss_start->ssi_url, ssi->ssi_url)) {

      // Got the initial item, steal it
      ssi->ssi_output_root = ss->ss_start->ssi_output_root;
      assert(ss->ss_start->ssi_output_root != NULL);
      ss->ss_start->ssi_output_root = NULL;

      if(ss->ss_current == ss->ss_start)
        ss->ss_current = ssi;

      slideshow_item_destroy(ss->ss_start, 0);
    }

    if(ssi->ssi_output_root == NULL) {
      ssi->ssi_output_root = prop_ref_inc(prop_create_root(NULL));
      prop_set(ssi->ssi_output_root, "type", PROP_SET_STRING, "image");
      prop_set(ssi->ssi_output_root, "url", PROP_SET_RSTRING, ssi->ssi_url);
      if(prop_set_parent_ex(ssi->ssi_output_root, ss->ss_nodes,
                            ssi_get_before(ssi), NULL)) {
        prop_ref_dec(ssi->ssi_output_root);
        ssi->ssi_output_root = NULL;
      }

    } else {

      prop_set(ssi->ssi_output_root, "url", PROP_SET_RSTRING, ssi->ssi_url);
    }

  } else {

    if(ssi->ssi_output_root == NULL)
      return;

    prop_destroy(ssi->ssi_output_root);
    ssi->ssi_output_root = NULL;
  }

}

/**
 *
 */
static void
ssi_set_url(void *opaque, rstr_t *rstr)
{
  slideshow_item_t *ssi = opaque;
  if(rstr_eq(ssi->ssi_url, rstr))
    return;
  rstr_set(&ssi->ssi_url, rstr);
  ssi_update_output(ssi);
}


/**
 *
 */
static void
ssi_set_type(void *opaque, rstr_t *rstr)
{
  slideshow_item_t *ssi = opaque;
  int is_image = !strcmp(rstr_get(rstr) ?: "", "image");
  if(ssi->ssi_is_image == is_image)
    return;
  ssi->ssi_is_image = is_image;
  ssi_update_output(ssi);
}


/**
 *
 */
static void
slideshow_item_add(slideshow_t *ss, prop_t *p, slideshow_item_t *before)
{
  slideshow_item_t *ssi = calloc(1, sizeof(slideshow_item_t));
  ssi->ssi_ss = ss;

  prop_tag_set(p, ss, ssi);

  ssi->ssi_sub_url =
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "url"),
		   PROP_TAG_CALLBACK_RSTR, ssi_set_url, ssi,
                   PROP_TAG_LOCKMGR, lockmgr_handler,
                   PROP_TAG_MUTEX, ss,
		   PROP_TAG_NAMED_ROOT, p, "self",
		   NULL);

  ssi->ssi_sub_type =
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "type"),
		   PROP_TAG_CALLBACK_RSTR, ssi_set_type, ssi,
                   PROP_TAG_LOCKMGR, lockmgr_handler,
                   PROP_TAG_MUTEX, ss,
		   PROP_TAG_NAMED_ROOT, p, "self",
		   NULL);

  if(before != NULL) {
    TAILQ_INSERT_BEFORE(before, ssi, ssi_link);
  } else {
    TAILQ_INSERT_TAIL(&ss->ss_items, ssi, ssi_link);
  }

  ssi->ssi_source = prop_ref_inc(p);

  if(!ss->ss_loaded) {
    callout_arm_managed(&ss->ss_callout, ss_timer, ss,
                        1000000, lockmgr_handler);
  }
}


/**
 *
 */
static void
slideshow_item_addv(slideshow_t *ss, prop_vec_t *pv, slideshow_item_t *before)
{
  int i;
  for(i = 0; i < prop_vec_len(pv); i++)
    slideshow_item_add(ss, prop_vec_get(pv, i), before);
}


/**
 *
 */
static void
slideshow_item_del(slideshow_t *ss, slideshow_item_t *ssi)
{
  slideshow_item_destroy(ssi, 0);
}


/**
 *
 */
static void
slideshow_item_move(slideshow_t *ss, slideshow_item_t *ssi,
                    slideshow_item_t *before)
{
  TAILQ_REMOVE(&ss->ss_items, ssi, ssi_link);
  if(before != NULL) {
    TAILQ_INSERT_BEFORE(before, ssi, ssi_link);
  } else {
    TAILQ_INSERT_TAIL(&ss->ss_items, ssi, ssi_link);
  }
  if(ssi->ssi_output_root != NULL)
    ssi_update_order(ssi);
}


/**
 *
 */
static void
slideshow_nodes(void *opaque, prop_event_t event, ...)
{
  slideshow_t *ss = opaque;

  prop_t *p1, *p2;
  prop_vec_t *pv;

  va_list ap;
  va_start(ap, event);

  switch(event) {

  case PROP_ADD_CHILD:
    slideshow_item_add(ss, va_arg(ap, prop_t *), NULL);
    break;

  case PROP_ADD_CHILD_BEFORE:
    p1 = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    slideshow_item_add(ss, p1, prop_tag_get(p2, ss));
    break;

  case PROP_ADD_CHILD_VECTOR:
    slideshow_item_addv(ss, va_arg(ap, prop_vec_t *), NULL);
    break;

  case PROP_ADD_CHILD_VECTOR_BEFORE:
    pv = va_arg(ap, prop_vec_t *);
    slideshow_item_addv(ss, pv, prop_tag_get(va_arg(ap, prop_t *), ss));
    break;

  case PROP_DEL_CHILD:
    p1 = va_arg(ap, prop_t *);
    slideshow_item_del(ss, prop_tag_clear(p1, ss));
    break;

  case PROP_MOVE_CHILD:
    p1 = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    slideshow_item_move(ss, prop_tag_get(p1, ss),
                        p2 ? prop_tag_get(p2, ss) : NULL);
    break;

  case PROP_SET_DIR:
  case PROP_WANT_MORE_CHILDS:
    break;

  case PROP_SET_VOID:
    slideshow_clear(ss, 0);
    break;

  case PROP_DESTROYED:
    slideshow_destroy(ss);
    break;

  case PROP_HAVE_MORE_CHILDS_YES:
  case PROP_HAVE_MORE_CHILDS_NO:
  case PROP_SUGGEST_FOCUS:
    break;

  default:
    abort();
  }
  va_end(ap);
}


/**
 *
 */
static void
slideshow_eventsink(void *opaque, event_t *e)
{
  slideshow_t *ss = opaque;

  if(event_is_action(e, ACTION_LEFT) ||
     event_is_action(e, ACTION_SEEK_BACKWARD)) {
    slideshow_advance(ss, 1);
    slideshow_arm(ss);
  }

  if(event_is_action(e, ACTION_RIGHT) ||
     event_is_action(e, ACTION_SEEK_FORWARD)) {
    slideshow_advance(ss, 0);
    slideshow_arm(ss);
  }

}


/**
 *
 */
static int
be_slideshow_open(prop_t *page, const char *url, int sync)
{
  url += strlen("slideshow:");
  slideshow_t *ss = calloc(1, sizeof(slideshow_t));

  ss->ss_model = prop_create_r(page, "model");
  ss->ss_nodes = prop_create_r(ss->ss_model, "nodes");

  prop_set(ss->ss_model, "type", PROP_SET_STRING, "slideshow");

  slideshow_item_t *ssi = calloc(1, sizeof(slideshow_item_t));

  ssi->ssi_output_root = prop_create_r(ss->ss_nodes, NULL);
  prop_set(ssi->ssi_output_root, "type", PROP_SET_STRING, "image");
  prop_set(ssi->ssi_output_root, "url", PROP_SET_STRING, url);
  prop_select(ssi->ssi_output_root);

  ssi->ssi_is_image = 1;
  ssi->ssi_url = rstr_alloc(url);
  ss->ss_start = ssi;
  ss->ss_current = ssi;
  ssi->ssi_ss = ss;
  TAILQ_INIT(&ss->ss_items);
  TAILQ_INSERT_TAIL(&ss->ss_items, ssi, ssi_link);

  lockmgr_init(&ss->ss_lockmgr, &slideshow_release);

  ss->ss_node_sub =
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
                   PROP_TAG_CALLBACK, slideshow_nodes, ss,
                   PROP_TAG_LOCKMGR, lockmgr_handler,
                   PROP_TAG_MUTEX, ss,
                   PROP_TAG_NAMED_ROOT, page, "page",
                   PROP_TAG_NAME("page", "previous", "parentModel", "nodes"),
                   NULL);

  ss->ss_event_sub =
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
                   PROP_TAG_CALLBACK_EVENT, slideshow_eventsink, ss,
                   PROP_TAG_LOCKMGR, lockmgr_handler,
                   PROP_TAG_MUTEX, ss,
                   PROP_TAG_NAMED_ROOT, page, "page",
                   PROP_TAG_NAME("page", "slideshow", "eventSink"),
                   NULL);


  slideshow_release(ss);
  return 0;
}



/**
 *
 */
static int
be_slideshow_canhandle(const char *url)
{
  return !strncmp(url, "slideshow:", strlen("slideshow:"));
}

/**
 *
 */
static backend_t be_slideshow = {
  .be_canhandle  = be_slideshow_canhandle,
  .be_open       = be_slideshow_open,
};

BE_REGISTER(slideshow);
