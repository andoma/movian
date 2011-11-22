/*
 *  Keyring
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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include "htsmsg/htsmsg_store.h"

#include "showtime.h"
#include "event.h"
#include "keyring.h"
#include "prop/prop.h"
#include "notifications.h"

static htsmsg_t *persistent_keyring, *temporary_keyring;
static hts_mutex_t keyring_mutex;


/**
 *
 */
void
keyring_init(void)
{
  hts_mutex_init(&keyring_mutex);
  if((persistent_keyring = htsmsg_store_load("keyring")) == NULL)
    persistent_keyring = htsmsg_create_map();
  temporary_keyring = htsmsg_create_map();
}


/**
 *
 */
static void
keyring_store(void)
{
  htsmsg_store_save(persistent_keyring, "keyring");
}


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
static void
set_remember(void *opaque, int v)
{
  int *rp = (int *)opaque;
  *rp = v;
}


/**
 *
 */
int
keyring_lookup(const char *id, char **username, char **password,
	       char **domain, int *remember_me, const char *source,
	       const char *reason, int flags)
{
  htsmsg_t *m;
  rstr_t *r;
  int remember = !!(flags & KEYRING_REMEMBER_ME_SET);

  hts_mutex_lock(&keyring_mutex);

  if(flags & KEYRING_QUERY_USER) {
    htsmsg_t *parent;
    prop_t *p = prop_create_root(NULL);

    prop_set_string(prop_create(p, "type"), "auth");
    prop_set_string(prop_create(p, "id"), id);
    prop_set_string(prop_create(p, "source"), source);
    prop_set_string(prop_create(p, "reason"), reason);
    prop_set_int(prop_create(p, "disableUsername"), username == NULL);


    prop_set_int(prop_create(p, "canRemember"),
		 !!(flags & KEYRING_SHOW_REMEMBER_ME));
    prop_t *rememberMe = prop_create(p, "rememberMe");
    prop_set_int(rememberMe, remember);

    prop_sub_t *remember_sub = 
	prop_subscribe(0,
		   PROP_TAG_CALLBACK_INT, set_remember, &remember,
		   PROP_TAG_ROOT, rememberMe,
		   NULL);

    prop_t *user = prop_create(p, "username");
    prop_t *pass = prop_create(p, "password");
 
    TRACE(TRACE_INFO, "keyring", "Requesting credentials for %s : %s : %s",
	  id, source, reason);


    event_t *e = popup_display(p);

    prop_unsubscribe(remember_sub);

    if(flags & KEYRING_ONE_SHOT)
      parent = NULL;
    else if(remember)
      parent = persistent_keyring;
    else
      parent = temporary_keyring;

    if(parent != NULL)
      htsmsg_delete_field(parent, id);

    if(event_is_action(e, ACTION_OK)) {
      /* OK */

      m = htsmsg_create_map();

      if(username != NULL) {
	r = prop_get_string(user);
	htsmsg_add_str(m, "username", r ? rstr_get(r) : "");
	*username = strdup(r ? rstr_get(r) : "");
	rstr_release(r);
      }

      r = prop_get_string(pass);
      htsmsg_add_str(m, "password", r ? rstr_get(r) : "");
      *password = strdup(r ? rstr_get(r) : "");
      rstr_release(r);

      if(parent != NULL) {
	htsmsg_add_msg(parent, id, m);

	if(parent == persistent_keyring)
	  keyring_store();
      }

    } else {
      /* CANCEL */
      if(parent == persistent_keyring)
	keyring_store();
    }

    if(remember_me != NULL)
      *remember_me = remember;

    prop_destroy(p);

    if(event_is_action(e, ACTION_CANCEL)) {
      /* return CANCEL to caller */
      hts_mutex_unlock(&keyring_mutex);
      event_release(e);
      return -1;
    }
    event_release(e);

  } else {

    if((m = htsmsg_get_map(temporary_keyring, id)) == NULL &&
       (m = htsmsg_get_map(persistent_keyring, id)) == NULL) {
      hts_mutex_unlock(&keyring_mutex);
      return 1;
    }
    
    setstr(username, m, "username");
    setstr(password, m, "password");
    setstr(domain, m, "domain");
  }

  hts_mutex_unlock(&keyring_mutex);
  return 0;
}
