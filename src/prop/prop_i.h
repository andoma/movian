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

#ifndef PROP_I_H__
#define PROP_I_H__


#include "prop.h"

extern hts_mutex_t prop_mutex;
extern hts_mutex_t prop_tag_mutex;



TAILQ_HEAD(prop_queue, prop);
LIST_HEAD(prop_list, prop);
LIST_HEAD(prop_sub_list, prop_sub);



/**
 *
 */
struct prop_courier {

  struct prop_notify_queue pc_queue_nor;
  struct prop_notify_queue pc_queue_exp;

  void *pc_entry_lock;
  prop_lockmgr_t *pc_lockmgr;

  hts_cond_t pc_cond;
  int pc_has_cond;

  hts_thread_t pc_thread;
  int pc_run;
  int pc_detached;

  void (*pc_notify)(void *opaque);
  void *pc_opaque;

  void (*pc_prologue)(void);
  void (*pc_epilogue)(void);

};



/**
 * Property types
 */
typedef enum {
  PROP_VOID,
  PROP_DIR,
  PROP_RSTRING,
  PROP_CSTRING,
  PROP_FLOAT,
  PROP_INT,
  PROP_PIXMAP,
  PROP_LINK,
  PROP_ZOMBIE, /* Destroyed can never be changed again */
} prop_type_t;


/**
 *
 */
struct prop {

  /**
   * Refcount. Not protected by mutex. Modification needs to be issued
   * using atomic ops. This refcount only protects the memory allocated
   * for this property, or in other words you can assume that a pointer
   * to a prop_t is valid as long as you own a reference to it.
   *
   * Note: hp_xref which is another refcount protecting contents of the
   * entire property
   */
  int hp_refcount;

  /**
   * Property name. Protected by mutex
   */
  const char *hp_name;

  /**
   * Parent linkage. Protected by mutex
   */
  struct prop *hp_parent;
  TAILQ_ENTRY(prop) hp_parent_link;


  /**
   * Originating property. Used when reflecting properties
   * in the tree (aka symlinks). Protected by mutex
   */
  struct prop *hp_originator;
  LIST_ENTRY(prop) hp_originator_link;

  /**
   * Properties receiving our values. Protected by mutex
   */
  struct prop_list hp_targets;


  /**
   * Subscriptions. Protected by mutex
   */
  struct prop_sub_list hp_value_subscriptions;
  struct prop_sub_list hp_canonical_subscriptions;

  /**
   * Payload type
   * Protected by mutex
   */
  uint8_t hp_type;

  /**
   * Various flags
   * Protected by mutex
   */
  uint8_t hp_flags;


  /**
   * The float/int prop should be clipped according to min/max
   */
#define PROP_CLIPPED_VALUE         0x1

  /**
   * hp_name is not malloc()ed but rather points to a compile const string
   * that should not be free()d upon prop finalization
   */
#define PROP_NAME_NOT_ALLOCATED    0x2

  /**
   * We hold an xref to prop pointed to by hp_originator.
   * So do a prop_destroy0() when we unlink/destroy this prop
   */
#define PROP_XREFED_ORIGINATOR     0x4

  /**
   * This property is monitored by one or more of its subscribers
   */
#define PROP_MONITORED             0x8

  /**
   * This property have a PROB_SUB_MULTI subscription attached to it
   */
#define PROP_MULTI_SUB             0x10

  /**
   * This property have a PROB_MULTI_SUB property above it in the hierarchy
   */
#define PROP_MULTI_NOTIFY          0x20


#define PROP_REF_TRACED            0x40



  /**
   * Extended refcount. Used to keep contents of the property alive
   * We limit this to 255, should never be a problem. And it's checked
   * in the code as well
   * Protected by mutex
   */
  uint8_t hp_xref;

  /**
   * Tags. Protected by prop_tag_mutex
   */
  struct prop_tag *hp_tags;


  /**
   * Actual payload
   * Protected by mutex
   */
  union {
    struct {
      float val, min, max;
    } f;
    struct {
      int val, min, max;
    } i;
    struct {
      rstr_t *rstr;
      prop_str_type_t type;
    } rstr;
    const char *cstr;
    struct {
      struct prop_queue childs;
      struct prop *selected;
    } c;
    struct pixmap *pixmap;
    struct {
      rstr_t *rtitle;
      rstr_t *rurl;
    } link;
  } u;

#define hp_cstring   u.cstr
#define hp_rstring   u.rstr.rstr
#define hp_rstrtype  u.rstr.type
#define hp_float    u.f.val
#define hp_int      u.i.val
#define hp_childs   u.c.childs
#define hp_selected u.c.selected
#define hp_pixmap   u.pixmap
#define hp_link_rtitle u.link.rtitle
#define hp_link_rurl   u.link.rurl

#ifdef PROP_DEBUG
  SIMPLEQ_HEAD(, prop_ref_trace) hp_ref_trace;
#endif

};

/**
 *
 */
struct prop_sub {

  /**
   * Refcount. Not protected by mutex. Modification needs to be issued
   * using atomic ops.
   */
  int hps_refcount;

  /**
   * Callback. May never be changed. Not protected by mutex
   */
  void *hps_callback;

  /**
   * Opaque value for callback
   */
  void *hps_opaque;

  /**
   * Trampoline. A tranform function that invokes the actual user
   * supplied callback.
   * May never be changed. Not protected by mutex.
   */
  prop_trampoline_t *hps_trampoline;

  /**
   * Pointer to courier, May never be changed. Not protected by mutex
   */
  prop_courier_t *hps_courier;

  /**
   * Lock to be held when invoking callback. It must also be held
   * when destroying the subscription.
   */
  void *hps_lock;

  /**
   * Function to call to obtain / release the lock.
   */
  prop_lockmgr_t *hps_lockmgr;

  /**
   * Set when a subscription is destroyed. Protected by hps_lock.
   * In other words. It's impossible to destroy a subscription
   * if no lock is specified.
   */
  uint8_t hps_zombie;

  /**
   * Used to avoid sending two notification when relinking
   * to another tree. Protected by global mutex
   */
  uint8_t hps_pending_unlink;

  /**
   * Flags as passed to prop_subscribe(). May never be changed
   */
  uint8_t hps_flags;

  /**
   * Linkage to property. Protected by global mutex
   */
  LIST_ENTRY(prop_sub) hps_value_prop_link;
  prop_t *hps_value_prop;

  /**
   * Linkage to property. Protected by global mutex
   */
  LIST_ENTRY(prop_sub) hps_canonical_prop_link;
  prop_t *hps_canonical_prop;

};

prop_t *prop_create0(prop_t *parent, const char *name, prop_sub_t *skipme, 
		     int flags);

prop_t *prop_make(const char *name, int noalloc, prop_t *parent);

void prop_move0(prop_t *p, prop_t *before, prop_sub_t *skipme);

void prop_link0(prop_t *src, prop_t *dst, prop_sub_t *skipme, int hard);

int prop_set_parent0(prop_t *p, prop_t *parent, prop_t *before, 
		     prop_sub_t *skipme);

void prop_unparent0(prop_t *p, prop_sub_t *skipme);

int prop_destroy0(prop_t *p);

void prop_unsubscribe0(prop_sub_t *s);

void prop_notify_childv(prop_vec_t *childv, prop_t *parent, prop_event_t event,
			prop_sub_t *skipme, prop_t *p2);

void prop_print_tree0(prop_t *p, int indent, int followlinks);

void prop_have_more_childs0(prop_t *p);

void prop_want_more_childs0(prop_sub_t *s);


void prop_set_string_exl(prop_t *p, prop_sub_t *skipme, const char *str,
			 prop_str_type_t type);

#endif // PROP_I_H__
