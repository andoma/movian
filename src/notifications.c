/*
 *  Notifications
 *  Copyright (C) 2009 Andreas Öman
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

#include <stdarg.h>
#include <stdio.h>

#include "showtime.h"
#include "prop/prop.h"
#include "notifications.h"
#include "misc/callout.h"
#include "event.h"
#include "keyring.h"

static prop_t *notify_prop_entries;

/**
 *
 */
static void
setstr(char **p, htsmsg_t *m, const char *fname)
{
  const char *s;

  if(p == NULL)
    return;

  if((s = htsmsg_get_str(m, fname)) != NULL)
    *p = strdup(s);
  else
    *p = NULL;
}

/**
 *
 */
void
notifications_init(void)
{
  prop_t *root = prop_create(prop_get_global(), "notifications");
  
  notify_prop_entries = prop_create(root, "nodes");

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
    callout_arm(NULL, notify_timeout, p, delay);
    return NULL;
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

  if(event != PROP_EXT_EVENT)
    return;

  if(*ep)
    event_release(*ep);
  e = va_arg(ap, event_t *);
  atomic_add(&e->e_refcount, 1);
  *ep = e;
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
  prop_sub_t *s = prop_subscribe(0, 
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
  return e;
}



/**
 *
 */
int
message_popup(const char *message, int flags)
{
  prop_t *p;
  int rval;

  p = prop_create_root(NULL);

  prop_set_string(prop_create(p, "type"), "message");
  prop_set_string_ex(prop_create(p, "message"), NULL, message,
		     flags & MESSAGE_POPUP_RICH_TEXT ?
		     PROP_STR_RICH : PROP_STR_UTF8);
  if(flags & MESSAGE_POPUP_CANCEL)
    prop_set_int(prop_create(p, "cancel"), 1);
  if(flags & MESSAGE_POPUP_OK)
    prop_set_int(prop_create(p, "ok"), 1);

  event_t *e = popup_display(p);
  prop_destroy(p);
  
  if(event_is_action(e, ACTION_OK))
    rval = MESSAGE_POPUP_OK;
  else if(event_is_action(e, ACTION_CANCEL))
    rval = MESSAGE_POPUP_CANCEL;
  else
    rval = 0;

  event_release(e);
  return rval;
}



/**
 *
 */
int
text_dialog(const char *message, char** answer, int flags)
{
  htsmsg_t *m;
  rstr_t *r;

  prop_t *p = prop_create_root(NULL);

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
    m = htsmsg_create_map();  
      
    r = prop_get_string(string);
    htsmsg_add_str(m, "input", r ? rstr_get(r) : "");
    rstr_release(r);
      
    htsmsg_get_str(m, "input");
    
    setstr(answer, m, "input");
  }
  
  prop_destroy(p);
  
  if(event_is_action(e, ACTION_CANCEL)){
      event_release(e);
      return -1;
  } 

  event_release(e);
  
  return 0;
}
