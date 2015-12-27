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
#ifndef PROP_I_H__
#define PROP_I_H__


#include "prop.h"
#include "misc/pool.h"
#include "misc/redblack.h"
#include "misc/lockmgr.h"

extern hts_mutex_t prop_mutex;
extern hts_mutex_t prop_tag_mutex;
extern pool_t *prop_pool;
extern pool_t *notify_pool;
extern pool_t *sub_pool;



TAILQ_HEAD(prop_queue, prop);
LIST_HEAD(prop_list, prop);
RB_HEAD_NFL(prop_tree, prop);
LIST_HEAD(prop_sub_list, prop_sub);
TAILQ_HEAD(prop_sub_dispatch_queue, prop_sub_dispatch);



/**
 *
 */
struct prop_courier {

  struct prop_notify_queue pc_queue_nor;
  struct prop_notify_queue pc_queue_exp;

  struct prop_notify_queue pc_dispatch_queue;
  struct prop_notify_queue pc_free_queue;

  void *pc_entry_lock;
  lockmgr_fn_t *pc_lockmgr;

  hts_cond_t pc_cond;
  int pc_has_cond;

  hts_thread_t pc_thread;
  int pc_run;
  int pc_detached;
  int pc_flags;

  void (*pc_notify)(void *opaque);
  void *pc_opaque;

  void (*pc_prologue)(void);
  void (*pc_epilogue)(void);

  int pc_refcount;
  char *pc_name;
};



/**
 *
 */
typedef struct prop_notify {
  TAILQ_ENTRY(prop_notify) hpn_link;
  prop_sub_t *hpn_sub;
  prop_event_t hpn_event;

  union {
    prop_t *p;
    prop_vec_t *pv;
    struct {
      float f;
      int how;
    } f;
    int i;
    struct {
      rstr_t *rstr;
      prop_str_type_t type;
    } rstr;
    struct event *e;
    struct {
      rstr_t *title;
      rstr_t *uri;
    } uri;
    const char *str;

  } u;

#define hpn_prop   u.p
#define hpn_propv  u.pv
#define hpn_float  u.f.f
#define hpn_int    u.i
#define hpn_rstring u.rstr.rstr
#define hpn_rstrtype u.rstr.type
#define hpn_cstring u.str
#define hpn_ext_event  u.e
#define hpn_uri_title u.uri.title
#define hpn_uri       u.uri.uri

  prop_t *hpn_prop_extra;
  int hpn_flags;

} prop_notify_t;


prop_notify_t *prop_get_notify(prop_sub_t *s);



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
  PROP_URI,
  PROP_PROP,   /* A simple reference to a prop */
  PROP_ZOMBIE, /* Destroyed can never be changed again */
  PROP_PROXY,  /* Proxy property, real property is remote */
} prop_type_t;


/**
 *
 */
struct prop {
#ifdef PROP_DEBUG
  uint32_t hp_magic;
#endif
  /**
   * Refcount. Not protected by mutex. Modification needs to be issued
   * using atomic ops. This refcount only protects the memory allocated
   * for this property, or in other words you can assume that a pointer
   * to a prop_t is valid as long as you own a reference to it.
   *
   * Note: hp_xref which is another refcount protecting contents of the
   * entire property
   */
  atomic_t hp_refcount;

  /**
   * Property name. Protected by mutex
   */
  const char *hp_name;

  union {
    struct {
      /**
       * Parent linkage. Protected by mutex
       */
      struct prop *hp_parent;
      TAILQ_ENTRY(prop) hp_parent_link;

      /**
       * Subscriptions. Protected by mutex
       */
      struct prop_sub_list hp_value_subscriptions;
      struct prop_sub_list hp_canonical_subscriptions;
    };


    // When hp_type == PROP_PROXY
    struct {
      struct prop_list hp_owned;

      union {
      // if PROP_PROXY_OWNED_BY_PROP is NOT set
        struct {
          RB_ENTRY(prop) hp_owner_sub_link;
          struct prop_sub *hp_owner_sub;
        };

      // if PROP_PROXY_OWNED_BY_PROP is set
        struct {
          LIST_ENTRY(prop) hp_owned_prop_link;
        };
      };
    };
  };


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
   * Payload type
   * Protected by mutex
   */
#ifdef PROP_DEBUG
  prop_type_t hp_type;
#else
  uint8_t hp_type;
#endif

  /**
   * Extended refcount. Used to keep contents of the property alive
   * We limit this to 255, should never be a problem. And it's checked
   * in the code as well
   * Protected by mutex
   */
  uint8_t hp_xref;


  /**
   * Various flags
   * Protected by mutex
   */
  uint16_t hp_flags;


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
   * For mark and sweep
   */
#define PROP_MARKED                0x80

  /**
   * For unlink mark and sweep
   */
#define PROP_INT_MARKED            0x100

  /**
   * Special debug
   */
#define PROP_DEBUG_THIS            0x200

  /**
   * Indicates that this is a proxy property that should follow symbolic
   * links when referenced on remote end.
   */
#define PROP_PROXY_FOLLOW_SYMLINK 0x400

  /**
   * Set if a prop proxy is owned by a property. This basically means
   * that when the owning proprty is destroyed, this property should be
   * destroyed as well
   */
#define PROP_PROXY_OWNED_BY_PROP  0x800


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
      rstr_t *title;
      rstr_t *uri;
    } uri;
    struct {
      struct prop_proxy_connection *ppc;
      char **pfx;
      uint32_t id;
    } proxy;
    struct prop *prop;
  } u;

#define hp_cstring   u.cstr
#define hp_rstring   u.rstr.rstr
#define hp_rstrtype  u.rstr.type
#define hp_float    u.f.val
#define hp_int      u.i.val
#define hp_childs   u.c.childs
#define hp_selected u.c.selected
#define hp_pixmap   u.pixmap
#define hp_uri_title u.uri.title
#define hp_uri       u.uri.uri
#define hp_prop      u.prop

#define hp_proxy_ppc u.proxy.ppc
#define hp_proxy_id  u.proxy.id
#define hp_proxy_pfx u.proxy.pfx

#ifdef PROP_DEBUG
  SIMPLEQ_HEAD(, prop_ref_trace) hp_ref_trace;
  const char *hp_file;
  int hp_line;
#endif

};


/**
 * This struct is used in the global dispatch (ie, where we don't
 * have a appointed courier) to maintain partial ordering of
 * notifications.
 *
 * Basically we need to make sure that we don't deliver notifications
 * out of order to subscriptions which could happen if we just
 * spawn a bunch of thread that dequeues notifications without
 * any control.
 *
 * With this struct we make sure that a single subscription can only
 * get served from a thread at a time.
 */
typedef struct prop_sub_dispatch {

  struct prop_notify_queue psd_notifications;

  TAILQ_ENTRY(prop_sub_dispatch) psd_link;

  struct prop_sub_dispatch_queue psd_wait_queue;
} prop_sub_dispatch_t;



/**
 *
 */
typedef struct prop_originator_tracking {
  prop_t *pot_p;
  struct prop_originator_tracking *pot_next;
} prop_originator_tracking_t;


/**
 *
 */
struct prop_sub {
#ifdef PROP_SUB_STATS
  LIST_ENTRY(prop_sub) hps_all_sub_link;
#endif

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
   * Pointer to dispatch structure
   *
   * If hps_global_dispatch is set this points to prop_sub_dispatch when
   *  there are active notifications on this subscription. If notifications
   *  are pendning it will be NULL
   *
   * If hps_global_dispatch is not set this points to a prop_courier
   *
   */
  void *hps_dispatch;

  /**
   * Lock to be held when invoking callback. It must also be held
   * when destroying the subscription.
   */
  void *hps_lock;

  /**
   * Function to call to obtain / release the lock.
   */
  lockmgr_fn_t *hps_lockmgr;

  /**
   * Linkage to property or proxy connection. Protected by global mutex
   */
  LIST_ENTRY(prop_sub) hps_value_prop_link;


  /**
   * Property backing this subscription.
   *
   * For non-proxied properties this points to the property with the value
   * and hps_value_prop_link is linked to that propertys list.
   *
   * For proxied properties this is only set if we are subscribing to the
   * value prop (PROP_SUB_SEND_VALUE_PROP) and if set we own the property
   * and must destroy it via prop_destroy0() when subscription dies.
   */
  prop_t *hps_value_prop;

  union {
    struct {
      // If hps_proxy is not set, these are the "active" members
      prop_t *hps_canonical_prop;
      LIST_ENTRY(prop_sub) hps_canonical_prop_link;
      union {
        prop_originator_tracking_t *hps_pots;
        prop_t *hps_origin;
      };

    };
    // If hps_proxy is set, these are the "active" members
    struct {
      struct prop_proxy_connection *hps_ppc;
      struct prop_tree hps_prop_tree;
      int hps_proxy_subid;
    };
  };


  /**
   * Refcount. Not protected by mutex. Modification needs to be issued
   * using atomic ops.
   */
  atomic_t hps_refcount;


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
  uint8_t hps_pending_unlink : 1;
  uint8_t hps_multiple_origins : 1;
  uint8_t hps_global_dispatch : 1;
  uint8_t hps_proxy : 1;

  /**
   * Flags as passed to prop_subscribe(). May never be changed
   */
  uint16_t hps_flags;

  /**
   * Extra value for use by caller
   */
  int hps_user_int;


#ifdef PROP_SUB_RECORD_SOURCE
  const char *hps_file;
  int hps_line;
#endif
};

#ifdef PROP_DEBUG
#define prop_ref_dec_locked(p) prop_ref_dec_traced_locked(p, __FILE__, __LINE__)

void prop_ref_dec_traced_locked(prop_t *p, const char *file, int line);

#else
void prop_ref_dec_locked(prop_t *p);
#endif

prop_t *prop_create0(prop_t *parent, const char *name, prop_sub_t *skipme, 
		     int flags);

prop_t *prop_make(const char *name, int noalloc, prop_t *parent);

void prop_make_dir(prop_t *p, prop_sub_t *skipme, const char *origin);

void prop_move0(prop_t *p, prop_t *before, prop_sub_t *skipme);

void prop_req_move0(prop_t *p, prop_t *before, prop_sub_t *skipme);

void prop_link0(prop_t *src, prop_t *dst, prop_sub_t *skipme, int hard,
                int debug);

int prop_set_parent0(prop_t *p, prop_t *parent, prop_t *before, 
		     prop_sub_t *skipme);

void prop_unparent0(prop_t *p, prop_sub_t *skipme);

int prop_destroy0(prop_t *p);

void prop_suggest_focus0(prop_t *p);

void prop_unsubscribe0(prop_sub_t *s);

rstr_t *prop_get_name0(prop_t *p);

void prop_notify_child2(prop_t *child, prop_t *parent, prop_t *sibling,
			prop_event_t event, prop_sub_t *skipme, int flags);

void prop_notify_childv(prop_vec_t *childv, prop_t *parent, prop_event_t event,
			prop_sub_t *skipme, prop_t *p2);

void prop_print_tree0(prop_t *p, int indent, int followlinks);

void prop_have_more_childs0(prop_t *p, int yes);

void prop_want_more_childs0(prop_sub_t *s);


void prop_set_string_exl(prop_t *p, prop_sub_t *skipme, const char *str,
			 prop_str_type_t type);

void prop_sub_ref_dec_locked(prop_sub_t *s);

int prop_dispatch_one(prop_notify_t *n, int lockmode);

void prop_courier_enqueue(prop_sub_t *s, prop_notify_t *n);

const char *prop_get_DN(prop_t *p, int compact);

#endif // PROP_I_H__
