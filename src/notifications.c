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
#include <stdarg.h>
#include <stdio.h>

#include "main.h"
#include "prop/prop.h"
#include "notifications.h"
#include "misc/callout.h"
#include "event.h"
#include "keyring.h"
#include "htsmsg/htsmsg_store.h"
#include "fileaccess/fileaccess.h"
#include "htsmsg/htsmsg_json.h"
#include "misc/time.h"
#include "settings.h"


static prop_t *notify_prop_entries;
static hts_mutex_t news_mutex;
static htsmsg_t *dismissed_news_in;
static htsmsg_t *dismissed_news_out;

/**
 *
 */
void
notifications_init(void)
{
  hts_mutex_init(&news_mutex);
  prop_t *root = prop_create(prop_get_global(), "notifications");

  if((dismissed_news_in = htsmsg_store_load("dismissed_news")) == NULL)
    dismissed_news_in = htsmsg_create_map();
  dismissed_news_out = htsmsg_create_map();

  notify_prop_entries = prop_create(root, "nodes");
}


/**
 *
 */
void
notifications_fini(void)
{
  hts_mutex_lock(&news_mutex);
  htsmsg_store_save(dismissed_news_out, "dismissed_news");
  htsmsg_release(dismissed_news_out);
  dismissed_news_out = NULL;
  hts_mutex_unlock(&news_mutex);
}


/**
 *
 */
static void
notify_timeout(callout_t *c, void *aux)
{
  prop_t *p = aux;
  prop_destroy(p);
  prop_ref_dec(p);
  free(c);
}

/**
 *
 */
void *
notify_add(prop_t *root, notify_type_t type, const char *icon, int delay,
	   rstr_t *fmt, ...)
{
  char msg[256];
  prop_t *p;
  const char *typestr;
  int tl;
  va_list ap, apx;

  switch(type) {
  case NOTIFY_INFO:    typestr = "info";    tl = TRACE_INFO;  break;
  case NOTIFY_WARNING: typestr = "warning"; tl = TRACE_INFO;  break;
  case NOTIFY_ERROR:   typestr = "error";   tl = TRACE_ERROR; break;
  default: return NULL;
  }
  
  va_start(ap, fmt);
  va_copy(apx, ap);

  tracev(0, tl, "notify", rstr_get(fmt), ap);

  vsnprintf(msg, sizeof(msg), rstr_get(fmt), apx);

  va_end(ap);
  va_end(apx);

  rstr_release(fmt);

  p = prop_create_root(NULL);

  prop_set_string(prop_create(p, "text"), msg);
  prop_set_string(prop_create(p, "type"), typestr);

  if(icon != NULL)
    prop_set_string(prop_create(p, "icon"), icon);

  p = prop_ref_inc(p);

  if(prop_set_parent(p, root ?: notify_prop_entries))
    prop_destroy(p);

  if(delay != 0) {
    prop_t *r = NULL;
    if(delay < 0) {
      r = prop_ref_inc(p);
      delay = -delay;
    }
    callout_arm(NULL, notify_timeout, p, delay);
    return r;
  }
  return p;
}

/**
 *
 */
void
notify_destroy(void *p)
{
  prop_destroy(p);
  prop_ref_dec(p);
}







/**
 *
 */
static void 
eventsink(void *opaque, prop_event_t event, ...)
{
  event_t *e, **ep = opaque;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  default:
    break;
  case PROP_EXT_EVENT:
    if(*ep)
      event_release(*ep);
    e = va_arg(ap, event_t *);
    atomic_inc(&e->e_refcount);
    *ep = e;
    break;

  case PROP_DESTROYED:
    if(*ep)
      event_release(*ep);
    *ep = event_create_action(ACTION_CANCEL);
    break;
  }
}


/**
 *
 */
event_t *
popup_display(prop_t *p)
{
  prop_courier_t *pc = prop_courier_create_waitable();
  event_t *e = NULL;

  prop_t *r = prop_create(p, "eventSink");
  prop_sub_t *s = prop_subscribe(PROP_SUB_TRACK_DESTROY,
				 PROP_TAG_CALLBACK, eventsink, &e,
				 PROP_TAG_ROOT, r,
				 PROP_TAG_COURIER, pc,
				 NULL);

  /* Will show the popup */
  if(prop_set_parent(p, prop_create(prop_get_global(), "popups"))) {
    /* popuproot is a zombie, this is an error */
    abort();
  }

  while(e == NULL)
    prop_courier_wait_and_dispatch(pc);

  prop_unsubscribe(s);
  prop_courier_destroy(pc);
  return e;
}



/**
 *
 */
int
message_popup(const char *message, int flags, const char **extra)
{
  prop_t *p;
  int rval;

  p = prop_ref_inc(prop_create_root(NULL));

  TRACE(TRACE_DEBUG, "Notification", "%s", message);

  prop_set_string(prop_create(p, "type"), "message");
  prop_set_string_ex(prop_create(p, "message"), NULL, message,
		     flags & MESSAGE_POPUP_RICH_TEXT ?
		     PROP_STR_RICH : PROP_STR_UTF8);

  if(extra) {
    int cnt = 1;
    prop_t *btns = prop_create(p, "buttons");
    while(*extra) {
      prop_t *b = prop_create_root(NULL);
      prop_set_string(prop_create(b, "title"), *extra);
      char action[16];
      snprintf(action, sizeof(action), "btn%d", cnt);
      prop_set_string(prop_create(b, "action"), action);
      if(prop_set_parent(b, btns))
	abort();
      cnt++;
      extra++;
    }
  }

  if(flags & MESSAGE_POPUP_CANCEL)
    prop_set_int(prop_create(p, "cancel"), 1);
  if(flags & MESSAGE_POPUP_OK)
    prop_set_int(prop_create(p, "ok"), 1);

  event_t *e = popup_display(p);
  prop_destroy(p);
  prop_ref_dec(p);

  const event_payload_t *ep = (const event_payload_t *)e;

  if(event_is_action(e, ACTION_OK))
    rval = MESSAGE_POPUP_OK;
  else if(event_is_action(e, ACTION_CANCEL))
    rval = MESSAGE_POPUP_CANCEL;
  else if(event_is_type(e, EVENT_DYNAMIC_ACTION) &&
	  !strncmp(ep->payload, "btn", 3))
    rval = atoi(ep->payload + 3);
  else
    rval = 0;

  event_release(e);
  return rval;
}


/**
 *
 */
int
text_dialog(const char *message, char **answer, int flags)
{
  rstr_t *r;
  *answer = NULL;
  prop_t *p = prop_ref_inc(prop_create_root(NULL));

  prop_set_string(prop_create(p, "type"), "textDialog");
  prop_set_string_ex(prop_create(p, "message"), NULL, message,
		     flags & MESSAGE_POPUP_RICH_TEXT ?
		     PROP_STR_RICH : PROP_STR_UTF8);
  prop_t *string = prop_create(p, "input");
  if(flags & MESSAGE_POPUP_CANCEL)
    prop_set_int(prop_create(p, "cancel"), 1);
  if(flags & MESSAGE_POPUP_OK)
    prop_set_int(prop_create(p, "ok"), 1);
  
  event_t *e = popup_display(p);
  
  if(event_is_action(e, ACTION_OK)) {
    r = prop_get_string(string, NULL);

    if(r)
      *answer = strdup(rstr_get(r));
    rstr_release(r);
  }
  
  prop_destroy(p);
  prop_ref_dec(p);
  if(event_is_action(e, ACTION_CANCEL)) {
    event_release(e);
    return -1;
  } 

  event_release(e);
  
  return 0;
}



/**
 *
 */
static void
dismis_news(const char *id)
{
  htsmsg_add_u32(dismissed_news_out, id, 1);
  htsmsg_store_save(dismissed_news_out, "dismissed_news");
  prop_t *root = prop_create(prop_get_global(), "news");
  prop_destroy_by_name(root, id);
}


/**
 *
 */
static void
news_sink(void *opaque, prop_event_t event, ...)
{
  prop_t *p = opaque;
  event_t *e;
  va_list ap;

  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    prop_unsubscribe(va_arg(ap, prop_sub_t *));
    prop_ref_dec(p);
    break;

  case PROP_EXT_EVENT:
    e = va_arg(ap, event_t *);
    if(event_is_type(e, EVENT_DYNAMIC_ACTION)) {
      const event_payload_t *ep = (const event_payload_t *)e;
      if(!strcmp(ep->payload, "dismiss")) {
	rstr_t *id = prop_get_string(p, "id", NULL);
	dismis_news(rstr_get(id));
	rstr_release(id);
	prop_destroy(opaque);
      }
    }
    break;

  default:
    break;
  }
  va_end(ap);
}



/**
 *
 */
static prop_t *
add_news_locked(const char *id, const char *message,
                const char *location, const char *caption,
                const char *action)
{
  prop_t *p, *ret = NULL;
  prop_t *root = prop_create(prop_get_global(), "news");

  if(dismissed_news_out != NULL) {

    if(htsmsg_get_u32_or_default(dismissed_news_in, id, 0)) {
      dismis_news(id);
    } else {

      p = prop_create_root(id);
      prop_set(p, "message",  PROP_SET_STRING, message);
      prop_set(p, "id",       PROP_SET_STRING, id);
      prop_set(p, "location", PROP_SET_STRING, location);
      prop_set(p, "caption",  PROP_SET_STRING, caption);
      prop_set(p, "action",   PROP_SET_STRING, action);

      prop_subscribe(PROP_SUB_TRACK_DESTROY,
		     PROP_TAG_CALLBACK, news_sink, prop_ref_inc(p),
		     PROP_TAG_ROOT, prop_create(p, "eventSink"),
		     PROP_TAG_MUTEX, &news_mutex,
		     NULL);
      ret = prop_ref_inc(p);
      if(prop_set_parent(p, root))
	prop_destroy(p);
    }
  }
  return ret;
}


/**
 *
 */
prop_t *
add_news(const char *id, const char *message,
	 const char *location, const char *caption)
{
  hts_mutex_lock(&news_mutex);
  prop_t *p = add_news_locked(id, message, location, caption, NULL);
  hts_mutex_unlock(&news_mutex);
  return p;
}
