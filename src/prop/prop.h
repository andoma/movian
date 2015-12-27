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
#ifndef PROP_H__
#define PROP_H__

#include <stdlib.h>

#include "config.h"
#include "compiler.h"
#include "event.h"
#include "arch/threads.h"
#include "misc/queue.h"
#include "misc/rstr.h"
#include "prop_defs.h"
#include "compiler.h"



typedef struct prop_courier prop_courier_t;
typedef struct prop prop_t;
typedef struct prop_sub prop_sub_t;
TAILQ_HEAD(prop_notify_queue, prop_notify);


/**
 *
 */
typedef struct prop_vec {
  atomic_t pv_refcount;
  int pv_capacity;
  int pv_length;
  struct prop *pv_vec[0];
} prop_vec_t;




#define PROP_ADD_SELECTED 0x1

typedef enum {
  PROP_SET_VOID,
  PROP_SET_STRING,
  PROP_SET_RSTRING,
  PROP_SET_CSTRING,
  PROP_SET_INT,
  PROP_SET_FLOAT,
  PROP_SET_DIR,
  PROP_SET_URI,
  PROP_SET_PROP,
  PROP_ADOPT_RSTRING,
  PROP_ADD_CHILD,
  PROP_ADD_CHILD_BEFORE,
  PROP_ADD_CHILD_VECTOR,
  PROP_ADD_CHILD_VECTOR_BEFORE,
  PROP_ADD_CHILD_VECTOR_DIRECT,
  PROP_DEL_CHILD,
  PROP_MOVE_CHILD,
  PROP_SELECT_CHILD,
  PROP_REQ_NEW_CHILD,
  PROP_REQ_DELETE_VECTOR,
  PROP_DESTROYED,
  PROP_VALUE_PROP,
  PROP_EXT_EVENT,
  PROP_SUBSCRIPTION_MONITOR_ACTIVE,
  PROP_HAVE_MORE_CHILDS_YES,
  PROP_HAVE_MORE_CHILDS_NO,
  PROP_WANT_MORE_CHILDS,
  PROP_SUGGEST_FOCUS,
  PROP_REQ_MOVE_CHILD,
} prop_event_t;


typedef enum {
  PROP_STR_UTF8,
  PROP_STR_RICH,
} prop_str_type_t;


struct prop_sub;


typedef void (prop_callback_t)(void *opaque, prop_event_t event, ...);
typedef void (prop_callback_ui_t)(void *opaque, int user_int, 
				  prop_event_t event, ...);
typedef void (prop_callback_string_t)(void *opaque, const char *str);
typedef void (prop_callback_rstr_t)(void *opaque, rstr_t *rstr);
typedef void (prop_callback_int_t)(void *opaque, int value);
typedef void (prop_callback_float_t)(void *opaque, float value);
typedef void (prop_callback_event_t)(void *opaque, event_t *e);
typedef void (prop_callback_destroyed_t)(void *opaque, struct prop_sub *s);

typedef void (prop_trampoline_t)(struct prop_sub *s, prop_event_t event, ...);

/**
 *
 */
prop_t *prop_get_global(void);

void prop_init(void);

void prop_init_late(void);

/**
 * Use with PROP_TAG_NAME_VECTOR
 */
#define PNVEC(name, ...) (const char *[]){name, ##__VA_ARGS__, NULL}

/**
 * Prop flags. These are sent over the wire so nothing can be removed
 */
#define PROP_SUB_TRACK_DESTROY        0x1
#define PROP_SUB_DEBUG                0x2
#define PROP_SUB_SUBSCRIPTION_MONITOR 0x4
#define PROP_SUB_EXPEDITE             0x8
#define PROP_SUB_MULTI                0x10
#define PROP_SUB_INTERNAL             0x20
#define PROP_SUB_IGNORE_VOID          0x40
#define PROP_SUB_TRACK_DESTROY_EXP    0x80
#define PROP_SUB_SEND_VALUE_PROP      0x100
#define PROP_SUB_NO_INITIAL_UPDATE    0x200
// Remember that flags field is uint16_t in prop_i.h so don't go above 0x8000
// for persistent flags

#define PROP_SUB_DIRECT_UPDATE        0x10000
#define PROP_SUB_DONTLOCK             0x20000
#define PROP_SUB_SINGLETON            0x40000
#define PROP_SUB_ALT_PATH             0x80000



enum {
  PROP_TAG_END = 0,
  PROP_TAG_NAME_VECTOR,
  PROP_TAG_CALLBACK,
  PROP_TAG_CALLBACK_USER_INT,
  PROP_TAG_CALLBACK_STRING,
  PROP_TAG_CALLBACK_RSTR,
  PROP_TAG_CALLBACK_INT,
  PROP_TAG_CALLBACK_FLOAT,
  PROP_TAG_CALLBACK_EVENT,
  PROP_TAG_CALLBACK_DESTROYED,
  PROP_TAG_SET_INT,
  PROP_TAG_SET_FLOAT,
  PROP_TAG_COURIER,
  PROP_TAG_ROOT,
  PROP_TAG_NAMED_ROOT,
  PROP_TAG_MUTEX,
  PROP_TAG_LOCKMGR,
  PROP_TAG_NAMESTR,
#ifdef PROP_SUB_RECORD_SOURCE
  PROP_TAG_SOURCE,
#endif
};

#define PROP_TAG_NAME(name, ...) \
  PROP_TAG_NAME_VECTOR, (const char *[]){ name, ##__VA_ARGS__, NULL }

#ifdef PROP_SUB_RECORD_SOURCE

prop_sub_t *prop_subscribe_ex(const char *file, int line,
                              int flags, ...) attribute_null_sentinel;

#define prop_subscribe(flags...) prop_subscribe_ex(__FILE__, __LINE__, flags)

#else

prop_sub_t *prop_subscribe(int flags, ...) attribute_null_sentinel;

#endif

void prop_unsubscribe(prop_sub_t *s);

void prop_sub_reemit(prop_sub_t *s);

prop_t *prop_create_ex(prop_t *parent, const char *name,
		       prop_sub_t *skipme, int noalloc, int incref)
     attribute_malloc;

#define prop_create(parent, name) \
  prop_create_ex(parent, name, NULL, __builtin_constant_p(name), 0)

#define prop_create_r(parent, name) \
  prop_create_ex(parent, name, NULL, __builtin_constant_p(name), 1)

prop_t *prop_create_root_ex(const char *name, int noalloc)
  attribute_malloc;

#define prop_create_root(name) \
  prop_create_root_ex(name, __builtin_constant_p(name))

prop_t *prop_create_after(prop_t *parent, const char *name, prop_t *after,
			  prop_sub_t *skipme);

prop_t *prop_create_multi(prop_t *p, ...) attribute_null_sentinel;

void prop_destroy(prop_t *p);

void prop_destroy_by_name(prop_t *parent, const char *name);

void prop_destroy_first(prop_t *p);

prop_t *prop_follow(prop_t *p);

 // Resolve a PROP_PROP into what it's pointing to
prop_t *prop_get_prop(prop_t *p);

int prop_compare(const prop_t *a, const prop_t *b);

void prop_move(prop_t *p, prop_t *before);

void prop_req_move(prop_t *p, prop_t *before);

void prop_setv_ex(prop_sub_t *skipme, prop_t *p, ...);

#define prop_setv(p, ...) prop_setv_ex(NULL, p, ##__VA_ARGS__)

void prop_set_ex(prop_t *p, const char *name, int noalloc, ...);

void prop_setdn(prop_sub_t *skipme, prop_t *p, const char *str, ...);

#define prop_set(p, name, type, ...) \
  prop_set_ex(p, name, __builtin_constant_p(name), type, ##__VA_ARGS__)

void prop_set_string_ex(prop_t *p, prop_sub_t *skipme, const char *str,
			prop_str_type_t type);

void prop_set_rstring_ex(prop_t *p, prop_sub_t *skipme, rstr_t *rstr,
                         prop_str_type_t type);

void prop_set_cstring_ex(prop_t *p, prop_sub_t *skipme, const char *str);

void prop_set_stringf_ex(prop_t *p, prop_sub_t *skipme, const char *fmt, ...);

void prop_set_float_ex(prop_t *p, prop_sub_t *skipme, float v);

void prop_set_float_clipping_range(prop_t *p, float min, float max);

void prop_add_float_ex(prop_t *p, prop_sub_t *skipme, float v);

void prop_set_int_ex(prop_t *p, prop_sub_t *skipme, int v);

void prop_toggle_int_ex(prop_t *p, prop_sub_t *skipme);

void prop_add_int_ex(prop_t *p, prop_sub_t *skipme, int v);

void prop_set_int_clipping_range(prop_t *p, int min, int max);

void prop_set_void_ex(prop_t *p, prop_sub_t *skipme);

void prop_set_uri_ex(prop_t *p, prop_sub_t *skipme, const char *title,
                     const char *url);

#define prop_set_string(p, str) do {		\
  if(__builtin_constant_p(str))			\
    prop_set_cstring_ex(p, NULL, str);		\
  else						\
    prop_set_string_ex(p, NULL, str, 0);	\
  } while(0)

#define prop_set_stringf(p, fmt, ...) prop_set_stringf_ex(p, NULL, fmt, ##__VA_ARGS__)

#define prop_set_float(p, v) prop_set_float_ex(p, NULL, v)

#define prop_add_float(p, v) prop_add_float_ex(p, NULL, v)

#define prop_set_int(p, v) prop_set_int_ex(p, NULL, v)

#define prop_add_int(p, v) prop_add_int_ex(p, NULL, v)

#define prop_toggle_int(p) prop_toggle_int_ex(p, NULL)

#define prop_set_void(p) prop_set_void_ex(p, NULL)

#define prop_set_uri(p, title, uri) prop_set_uri_ex(p, NULL, title, uri)

#define prop_set_rstring(p, rstr) prop_set_rstring_ex(p, NULL, rstr, 0)

#define prop_set_cstring(p, cstr) prop_set_cstring_ex(p, NULL, cstr)

void prop_copy_ex(prop_t *dst, prop_sub_t *skipme, prop_t *src);

#define prop_copy(dst, src) prop_copy_ex(dst, NULL, src)

rstr_t *prop_get_string(prop_t *p, ...) attribute_null_sentinel;

int prop_get_int(prop_t *p, ...) attribute_null_sentinel;

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

prop_t *prop_ref_inc(prop_t *p) attribute_unused_result;

#endif

prop_t *prop_xref_addref(prop_t *p) attribute_unused_result;

int prop_set_parent_ex(prop_t *p, prop_t *parent, prop_t *before, 
		       prop_sub_t *skipme)
     attribute_unused_result;

#define prop_set_parent(p, parent) prop_set_parent_ex(p, parent, NULL, NULL)

void prop_set_parent_vector(prop_vec_t *pv, prop_t *parent,
			    prop_t *before, prop_sub_t *skipme);

void prop_unparent_ex(prop_t *p, prop_sub_t *skipme);

#define prop_unparent(p) prop_unparent_ex(p, NULL)

void prop_unparent_childs(prop_t *p);

#define PROP_LINK_NORMAL 0
#define PROP_LINK_XREFED 1
#define PROP_LINK_XREFED_IF_ORPHANED 2

void prop_link_ex(prop_t *src, prop_t *dst, prop_sub_t *skipme, int how,
                  int debug);

#define prop_link(src, dst) prop_link_ex(src, dst, NULL, PROP_LINK_NORMAL, 0)

void prop_unlink_ex(prop_t *p, prop_sub_t *skipme);

#define prop_unlink(p) prop_unlink_ex(p, NULL)

void prop_select_ex(prop_t *p, prop_t *extra, prop_sub_t *skipme);

#define prop_select(p) prop_select_ex(p, NULL, NULL)

void prop_unselect_ex(prop_t *parent, prop_sub_t *skipme);

#define prop_unselect(parent) prop_unselect_ex(parent, NULL)

void prop_select_by_value_ex(prop_t *p, const char *name, prop_sub_t *skipme);

#define prop_select_by_value(p, name) prop_select_by_value_ex(p, name, NULL)

void prop_suggest_focus(prop_t *p);

void prop_destroy_childs(prop_t *parent);

void prop_void_childs(prop_t *parent);

#ifdef PROP_DEBUG
prop_t *prop_get_by_name0(const char *file, int line,
                          const char **name, int follow_symlinks, ...)
  attribute_null_sentinel;

#define prop_get_by_name(a, b, ...) \
  prop_get_by_name0(__FILE__, __LINE__, a, b, ##__VA_ARGS__)
#else
prop_t *prop_get_by_name(const char **name, int follow_symlinks, ...)
  attribute_null_sentinel;

#endif

void prop_request_new_child(prop_t *p);

void prop_request_delete(prop_t *p);

void prop_request_delete_multi(prop_vec_t *pv);

#define PROP_COURIER_TRACE_TIMES 0x1

prop_courier_t *prop_courier_create_thread(hts_mutex_t *entrymutex,
					   const char *name,
                                           int flags);

prop_courier_t *prop_courier_create_passive(void);

prop_courier_t *prop_courier_create_notify(void (*notify)(void *opaque),
					   void *opaque);

prop_courier_t *prop_courier_create_waitable(void);

int prop_courier_wait(prop_courier_t *pc, struct prop_notify_queue *q,
		      int timeout);

void prop_courier_wait_and_dispatch(prop_courier_t *pc);

void prop_courier_poll(prop_courier_t *pc);

void prop_courier_poll_timed(prop_courier_t *pc, int maxtime);

void prop_courier_poll_with_alarm(prop_courier_t *pc, int maxtime);

int prop_courier_check(prop_courier_t *pc);

void prop_courier_destroy(prop_courier_t *pc);

void prop_notify_dispatch(struct prop_notify_queue *q, const char *tracename);

void prop_courier_stop(prop_courier_t *pc);

// Does not create properties, can't be used over remote connections
prop_t *prop_find(prop_t *parent, ...) attribute_null_sentinel;

prop_t *prop_findv(prop_t *p, char **names);

prop_t *prop_first_child(prop_t *p);

void prop_send_ext_event(prop_t *p, event_t *e);


/**
 * Property vectors
 */
prop_vec_t *prop_vec_create(int capacity) attribute_malloc;

prop_vec_t *prop_vec_append(prop_vec_t *pv, prop_t *p) attribute_unused_result;

prop_vec_t *prop_vec_addref(prop_vec_t *pv);

void prop_vec_release(prop_vec_t *pv);

void prop_vec_destroy_entries(prop_vec_t *pv);


#define prop_vec_get(pv, idx) ((pv)->pv_vec[idx])

#define prop_vec_len(pv) ((pv)->pv_length)



rstr_t *prop_get_name(prop_t *p);

void prop_want_more_childs(prop_sub_t *s);

void prop_have_more_childs(prop_t *p, int yes);

void prop_mark_childs(prop_t *p);

void prop_unmark(prop_t *p);

int prop_is_marked(prop_t *p);

void prop_destroy_marked_childs(prop_t *p);

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
void prop_track_sub(prop_sub_t *s);
#endif


#endif /* PROP_H__ */
