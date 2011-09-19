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

// #define PROP_DEBUG

typedef struct prop_courier prop_courier_t;
typedef struct prop prop_t;
typedef struct prop_sub prop_sub_t;
struct pixmap;
TAILQ_HEAD(prop_notify_queue, prop_notify);


/**
 *
 */
typedef struct prop_vec {
  int pv_refcount;
  int pv_capacity;
  int pv_length;
  struct prop *pv_vec[0];
} prop_vec_t;




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
  PROP_ADD_CHILD_VECTOR,
  PROP_ADD_CHILD_VECTOR_BEFORE,
  PROP_DEL_CHILD,
  PROP_MOVE_CHILD,
  PROP_SELECT_CHILD,
  PROP_REQ_NEW_CHILD,
  PROP_REQ_DELETE_VECTOR,
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
#define PROP_SUB_DONTLOCK             0x100

enum {
  PROP_TAG_END = 0,
  PROP_TAG_NAME_VECTOR,
  PROP_TAG_CALLBACK,
  PROP_TAG_CALLBACK_STRING,
  PROP_TAG_CALLBACK_INT,
  PROP_TAG_CALLBACK_FLOAT,
  PROP_TAG_SET_INT,
  PROP_TAG_SET_FLOAT,
  PROP_TAG_COURIER,
  PROP_TAG_ROOT,
  PROP_TAG_NAMED_ROOT,
  PROP_TAG_MUTEX,
  PROP_TAG_EXTERNAL_LOCK,
  PROP_TAG_NAMESTR,
};

#define PROP_TAG_NAME(name...) \
 PROP_TAG_NAME_VECTOR, (const char *[]){name, NULL}

prop_sub_t *prop_subscribe(int flags, ...) __attribute__((__sentinel__(0)));

void prop_unsubscribe(prop_sub_t *s);

prop_t *prop_create_ex(prop_t *parent, const char *name,
		       prop_sub_t *skipme, int noalloc)
     __attribute__ ((malloc)) __attribute__((nonnull (1)));

#define prop_create(parent, name) \
  prop_create_ex(parent, name, NULL, __builtin_constant_p(name))

prop_t *prop_create_root_ex(const char *name, int noalloc)
  __attribute__ ((malloc));

#define prop_create_root(name) \
  prop_create_root_ex(name, __builtin_constant_p(name))

void prop_destroy(prop_t *p);

void prop_destroy_by_name(prop_t *parent, const char *name);

prop_t *prop_follow(prop_t *p);

int prop_compare(const prop_t *a, const prop_t *b);

void prop_move(prop_t *p, prop_t *before);

void prop_set_string_ex(prop_t *p, prop_sub_t *skipme, const char *str,
			prop_str_type_t type);

void prop_set_rstring_ex(prop_t *p, prop_sub_t *skipme, rstr_t *rstr,
			 int noupdate);

void prop_set_stringf_ex(prop_t *p, prop_sub_t *skipme, const char *fmt, ...);

#define PROP_SET_NORMAL    0
#define PROP_SET_TENTATIVE 1
#define PROP_SET_COMMIT    2

void prop_set_float_ex(prop_t *p, prop_sub_t *skipme, float v, int how);

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

#define prop_set_float(p, v) prop_set_float_ex(p, NULL, v, 0)

#define prop_add_float(p, v) prop_add_float_ex(p, NULL, v)

#define prop_set_int(p, v) prop_set_int_ex(p, NULL, v)

#define prop_add_int(p, v) prop_add_int_ex(p, NULL, v)

#define prop_toggle_int(p) prop_toggle_int_ex(p, NULL)

#define prop_set_void(p) prop_set_void_ex(p, NULL)

#define prop_set_pixmap(p, pp) prop_set_pixmap_ex(p, NULL, pp)

#define prop_set_link(p, title, link) prop_set_link_ex(p, NULL, title, link)

#define prop_set_rstring(p, rstr) prop_set_rstring_ex(p, NULL, rstr, 0)

rstr_t *prop_get_string(prop_t *p);

char **prop_get_name_of_childs(prop_t *p);

#ifdef PROP_DEBUG

void prop_ref_dec_traced(prop_t *p, const char *file, int line);

#define prop_ref_dec(p) prop_ref_dec_traced(p, __FILE__, __LINE__)

void prop_ref_dec_nullchk_traced(prop_t *p, const char *file, int line);

#define prop_ref_dec_nullchk(p) prop_ref_dec_nullchk_traced(p, __FILE__, __LINE__)

prop_t *prop_ref_inc_traced(prop_t *p, const char *file, int line);

#define prop_ref_inc(p) prop_ref_inc_traced(p, __FILE__, __LINE__)

void prop_enable_trace(prop_t *p);

void prop_print_trace(prop_t *p);

#else

void prop_ref_dec(prop_t *p);

prop_t *prop_ref_inc(prop_t *p) __attribute__ ((warn_unused_result));

#endif

prop_t *prop_xref_addref(prop_t *p) __attribute__ ((warn_unused_result));

int prop_set_parent_ex(prop_t *p, prop_t *parent, prop_t *before, 
		       prop_sub_t *skipme)
     __attribute__ ((warn_unused_result));
     
#define prop_set_parent(p, parent) prop_set_parent_ex(p, parent, NULL, NULL)

void prop_set_parent_vector(prop_vec_t *pv, prop_t *parent,
			    prop_t *before, prop_sub_t *skipme);

void prop_unparent_ex(prop_t *p, prop_sub_t *skipme);

#define prop_unparent(p) prop_unparent_ex(p, NULL)

void prop_unparent_childs(prop_t *p);

#define PROP_LINK_NORMAL 0
#define PROP_LINK_XREFED 1
#define PROP_LINK_XREFED_IF_ORPHANED 2

void prop_link_ex(prop_t *src, prop_t *dst, prop_sub_t *skipme, int how);

#define prop_link(src, dst) prop_link_ex(src, dst, NULL, PROP_LINK_NORMAL)

void prop_unlink_ex(prop_t *p, prop_sub_t *skipme);

#define prop_unlink(p) prop_unlink_ex(p, NULL)

void prop_select_ex(prop_t *p, prop_t *extra, prop_sub_t *skipme);

#define prop_select(p) prop_select_ex(p, NULL, NULL)

void prop_unselect_ex(prop_t *parent, prop_sub_t *skipme);

#define prop_unselect(parent) prop_unselect_ex(parent, NULL)

void prop_destroy_childs(prop_t *parent);

prop_t *prop_get_by_name(const char **name, int follow_symlinks, ...)
     __attribute__((__sentinel__(0)));

void prop_request_new_child(prop_t *p);

void prop_request_delete(prop_t *p);

void prop_request_delete_multi(prop_vec_t *pv);

prop_courier_t *prop_courier_create_thread(hts_mutex_t *entrymutex,
					   const char *name);

prop_courier_t *prop_courier_create_passive(void);

prop_courier_t *prop_courier_create_notify(void (*notify)(void *opaque),
					   void *opaque);

prop_courier_t *prop_courier_create_waitable(void);

void prop_courier_wait(prop_courier_t *pc,
		       struct prop_notify_queue *exp,
		       struct prop_notify_queue *nor);

void prop_courier_wait_and_dispatch(prop_courier_t *pc);

void prop_courier_poll(prop_courier_t *pc);

void prop_courier_destroy(prop_courier_t *pc);

void prop_notify_dispatch(struct prop_notify_queue *q);

void prop_courier_stop(prop_courier_t *pc);

prop_t *prop_find(prop_t *parent, ...)  __attribute__((__sentinel__(0)));

htsmsg_t *prop_tree_to_htsmsg(prop_t *p);

void prop_send_ext_event(prop_t *p, event_t *e);


/**
 * Property vectors
 */
prop_vec_t *prop_vec_create(int capacity) 
  __attribute__ ((malloc));

prop_vec_t *prop_vec_append(prop_vec_t *pv, prop_t *p)
  __attribute__ ((warn_unused_result));

prop_vec_t *prop_vec_addref(prop_vec_t *pv);

void prop_vec_release(prop_vec_t *pv);

void prop_vec_destroy_entries(prop_vec_t *pv);


#define prop_vec_get(pv, idx) ((pv)->pv_vec[idx])

#define prop_vec_len(pv) ((pv)->pv_length)



const char *prop_get_name(prop_t *p);

void prop_want_more_childs(prop_sub_t *s);

void prop_have_more_childs(prop_t *p);


/**
 * Property tags
 */
void *prop_tag_get(prop_t *p, void *key);


#ifdef PROP_DEBUG

#define prop_tag_set(p, k, v) prop_tag_set_debug(p, k, v, __FILE__, __LINE__)

void prop_tag_set_debug(prop_t *p, void *key, void *value,
			const char *file, int line);

#else
void prop_tag_set(prop_t *p, void *key, void *value);
#endif

void *prop_tag_clear(prop_t *p, void *key);

const char *propname(prop_t *p);

void prop_print_tree(prop_t *p, int followlinks);

void prop_test(void);

#ifdef PROP_DEBUG
extern int prop_trace;
#endif

#endif /* PROP_H__ */
