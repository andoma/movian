/*
 *  Property trees
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

#ifndef PROP_H__
#define PROP_H__

#include <stdlib.h>

#include "event.h"
#include "arch/threads.h"
#include "misc/queue.h"
#include "htsmsg/htsmsg.h"
#include "misc/rstr.h"


  /**
   * The float/int prop should be clipped according to min/max
   */
#define PROP_CLIPPED_VALUE         0x1

  /**
   * hp_name is not malloc()ed but rather points to a compile const string
   * that should not be free()d upon prop finalization
   */
#define PROP_NAME_NOT_ALLOCATED    0x8

  /**
   * We hold an xref to prop pointed to by hp_originator.
   * So do a prop_destroy0() when we unlink/destroy this prop
   */
#define PROP_XREFED_ORIGINATOR     0x10

  /**
   * This property is monitored by one or more of its subscribers
   */
#define PROP_MONITORED             0x20

  /**
   * This property have a PROB_SUB_MULTI subscription attached to it
   */
#define PROP_MULTI_SUB             0x40

  /**
   * This property have a PROB_MULTI_SUB property above it in the hierarchy
   */
#define PROP_MULTI_NOTIFY          0x80




typedef struct prop_courier prop_courier_t;
typedef struct prop prop_t;
typedef struct prop_sub prop_sub_t;
struct pixmap;

#define PROP_ADD_SELECTED 0x1

typedef enum {
  PROP_SET_VOID,
  PROP_SET_RSTRING,
  PROP_SET_INT,
  PROP_SET_FLOAT,
  PROP_SET_DIR,
  PROP_SET_PIXMAP,
  PROP_SET_RLINK,

  PROP_ADD_CHILD,
  PROP_ADD_CHILD_BEFORE,
  PROP_ADD_CHILD_MULTI,
  PROP_DEL_CHILD,
  PROP_MOVE_CHILD,
  PROP_SELECT_CHILD,
  PROP_REQ_NEW_CHILD,
  PROP_REQ_DELETE_MULTI,
  PROP_DESTROYED,
  PROP_EXT_EVENT,
  PROP_SUBSCRIPTION_MONITOR_ACTIVE,
  PROP_HAVE_MORE_CHILDS,
  PROP_WANT_MORE_CHILDS,
} prop_event_t;


typedef enum {
  PROP_STR_UTF8,
  PROP_STR_RICH,
} prop_str_type_t;



typedef void (prop_callback_t)(void *opaque, prop_event_t event, ...);
typedef void (prop_callback_string_t)(void *opaque, const char *str);
typedef void (prop_callback_int_t)(void *opaque, int value);
typedef void (prop_callback_float_t)(void *opaque, float value);

struct prop_sub;
typedef void (prop_trampoline_t)(struct prop_sub *s, prop_event_t event, ...);

typedef void (prop_lockmgr_t)(void *ptr, int lock);


/**
 *
 */

prop_t *prop_get_global(void);

void prop_init(void);

/**
 * Use with PROP_TAG_NAME_VECTOR
 */
#define PNVEC(name...) (const char *[]){name, NULL}

#define PROP_SUB_DIRECT_UPDATE 0x1
#define PROP_SUB_NO_INITIAL_UPDATE 0x2
#define PROP_SUB_TRACK_DESTROY 0x4
#define PROP_SUB_DEBUG         0x8 // TRACE(TRACE_DEBUG, ...) changes
#define PROP_SUB_SUBSCRIPTION_MONITOR 0x10
#define PROP_SUB_EXPEDITE             0x20
#define PROP_SUB_MULTI                0x40
#define PROP_SUB_INTERNAL             0x80
#define PROP_SUB_NOLOCK               0x100

enum {
  PROP_TAG_END = 0,
  PROP_TAG_NAME_VECTOR,
  PROP_TAG_CALLBACK,
  PROP_TAG_CALLBACK_STRING,
  PROP_TAG_CALLBACK_INT,
  PROP_TAG_CALLBACK_FLOAT,
  PROP_TAG_COURIER,
  PROP_TAG_ROOT,
  PROP_TAG_NAMED_ROOT,
  PROP_TAG_MUTEX,
  PROP_TAG_EXTERNAL_LOCK,
};

#define PROP_TAG_NAME(name...) \
 PROP_TAG_NAME_VECTOR, (const char *[]){name, NULL}

prop_sub_t *prop_subscribe(int flags, ...) __attribute__((__sentinel__(0)));

void prop_unsubscribe(prop_sub_t *s);

prop_t *prop_create_ex(prop_t *parent, const char *name,
		       prop_sub_t *skipme, int flags)
     __attribute__ ((malloc));

#define prop_create(parent, name) \
 prop_create_ex(parent, name, NULL, __builtin_constant_p(name) ? \
 PROP_NAME_NOT_ALLOCATED : 0)

void prop_destroy(prop_t *p);

void prop_destroy_by_name(prop_t *parent, const char *name);

prop_t *prop_follow(prop_t *p);

void prop_move(prop_t *p, prop_t *before);

void prop_set_string_ex(prop_t *p, prop_sub_t *skipme, const char *str,
			prop_str_type_t type);

void prop_set_rstring_ex(prop_t *p, prop_sub_t *skipme, rstr_t *rstr);

void prop_set_stringf_ex(prop_t *p, prop_sub_t *skipme, const char *fmt, ...);

void prop_set_float_ex(prop_t *p, prop_sub_t *skipme, float v);

void prop_set_float_clipping_range(prop_t *p, float min, float max);

void prop_add_float_ex(prop_t *p, prop_sub_t *skipme, float v);

void prop_set_int_ex(prop_t *p, prop_sub_t *skipme, int v);

void prop_toggle_int_ex(prop_t *p, prop_sub_t *skipme);

void prop_add_int_ex(prop_t *p, prop_sub_t *skipme, int v);

void prop_set_int_clipping_range(prop_t *p, int min, int max);

void prop_set_void_ex(prop_t *p, prop_sub_t *skipme);

void prop_set_pixmap_ex(prop_t *p, prop_sub_t *skipme, struct pixmap *pm);

void prop_set_link_ex(prop_t *p, prop_sub_t *skipme, const char *title,
		      const char *url);

#define prop_set_string(p, str) prop_set_string_ex(p, NULL, str, 0)

#define prop_set_stringf(p, fmt...) prop_set_stringf_ex(p, NULL, fmt)

#define prop_set_float(p, v) prop_set_float_ex(p, NULL, v)

#define prop_add_float(p, v) prop_add_float_ex(p, NULL, v)

#define prop_set_int(p, v) prop_set_int_ex(p, NULL, v)

#define prop_add_int(p, v) prop_add_int_ex(p, NULL, v)

#define prop_toggle_int(p) prop_toggle_int_ex(p, NULL)

#define prop_set_void(p) prop_set_void_ex(p, NULL)

#define prop_set_pixmap(p, pp) prop_set_pixmap_ex(p, NULL, pp)

#define prop_set_link(p, title, link) prop_set_link_ex(p, NULL, title, link)

#define prop_set_rstring(p, rstr) prop_set_rstring_ex(p, NULL, rstr)

int prop_get_string(prop_t *p, char *buf, size_t bufsize)
     __attribute__ ((warn_unused_result));

void prop_ref_dec(prop_t *p);

void prop_ref_inc(prop_t *p);

prop_t *prop_xref_addref(prop_t *p) __attribute__ ((warn_unused_result));

int prop_set_parent_ex(prop_t *p, prop_t *parent, prop_t *before, 
		       prop_sub_t *skipme)
     __attribute__ ((warn_unused_result));
     
#define prop_set_parent(p, parent) prop_set_parent_ex(p, parent, NULL, NULL)

void prop_set_parent_multi(prop_t **pv, prop_t *parent);

void prop_unparent_ex(prop_t *p, prop_sub_t *skipme);

#define prop_unparent(p) prop_unparent_ex(p, NULL)

void prop_link_ex(prop_t *src, prop_t *dst, prop_sub_t *skipme, int hard);

#define prop_link(src, dst) prop_link_ex(src, dst, NULL, 0)

void prop_unlink_ex(prop_t *p, prop_sub_t *skipme);

#define prop_unlink(p) prop_unlink_ex(p, NULL)

void prop_select_ex(prop_t *p, int advisory, prop_sub_t *skipme);

#define prop_select(p, advisory) prop_select_ex(p, advisory, NULL)

void prop_unselect_ex(prop_t *parent, prop_sub_t *skipme);

#define prop_unselect(parent) prop_unselect_ex(parent, NULL)

void prop_destroy_childs(prop_t *parent);

prop_t **prop_get_ancestors(prop_t *p);

prop_t **prop_get_childs(prop_t *p, int *num);

prop_t *prop_get_by_name(const char **name, int follow_symlinks, ...)
     __attribute__((__sentinel__(0)));

void prop_request_new_child(prop_t *p);

void prop_request_delete(prop_t *p);

void prop_request_delete_multi(prop_t **vec);

prop_courier_t *prop_courier_create_thread(hts_mutex_t *entrymutex,
					   const char *name);

prop_courier_t *prop_courier_create_passive(void);

prop_courier_t *prop_courier_create_notify(void (*notify)(void *opaque),
					   void *opaque);

prop_courier_t *prop_courier_create_waitable(void);

void prop_courier_wait(prop_courier_t *pc);

void prop_courier_poll(prop_courier_t *pc);

void prop_courier_destroy(prop_courier_t *pc);

void prop_courier_stop(prop_courier_t *pc);

prop_t *prop_get_by_names(prop_t *parent, ...) 
     __attribute__((__sentinel__(0)));

htsmsg_t *prop_tree_to_htsmsg(prop_t *p);

void prop_send_ext_event(prop_t *p, event_t *e);

void prop_pvec_free(prop_t **a);

int prop_pvec_len(prop_t **src);

prop_t **prop_pvec_clone(prop_t **src);

const char *prop_get_name(prop_t *p);

void prop_want_more_childs(prop_sub_t *s);

void prop_have_more_childs(prop_t *p);


/* DEBUGish */
const char *propname(prop_t *p);

void prop_print_tree(prop_t *p, int followlinks);

void prop_test(void);

#endif /* PROP_H__ */
