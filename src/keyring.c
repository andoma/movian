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
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include "htsmsg/htsmsg_store.h"

#include "main.h"
#include "event.h"
#include "keyring.h"
#include "prop/prop.h"
#include "notifications.h"
#include "settings.h"

static htsmsg_t *persistent_keyring, *temporary_keyring;
static hts_mutex_t keyring_mutex;

static void
keyring_clear(void *opaque, prop_event_t event, ...)
{
  hts_mutex_lock(&keyring_mutex);
  htsmsg_release(persistent_keyring);
  htsmsg_release(temporary_keyring);

  persistent_keyring = htsmsg_create_map();
  temporary_keyring = htsmsg_create_map();

  htsmsg_store_save(persistent_keyring, "keyring");
  hts_mutex_unlock(&keyring_mutex);

  notify_add(NULL, NOTIFY_WARNING, NULL, 3, _("Rembered passwords erased"));
}


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

  prop_t *dir = setting_get_dir("general:resets");
  settings_create_action(dir,
			 _p("Forget remembered passwords"), NULL,
			 keyring_clear, NULL, 0, NULL);
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
    char tmpbuf[64];
    htsmsg_t *parent;
    prop_t *p = prop_ref_inc(prop_create_root(NULL));

    prop_set_string(prop_create(p, "type"), "auth");
    prop_set_string(prop_create(p, "id"), id);
    prop_set_string(prop_create(p, "source"), source);
    prop_set_string(prop_create(p, "reason"), reason);
    prop_set_int(prop_create(p, "disableUsername"), username == NULL);
    prop_set_int(prop_create(p, "disablePassword"), password == NULL);
    prop_set_int(prop_create(p, "disableDomain"),   domain   == NULL);

    prop_set_int(prop_create(p, "canRemember"),
		 !!(flags & KEYRING_SHOW_REMEMBER_ME));
    prop_t *rememberMe = prop_create_r(p, "rememberMe");
    prop_set_int(rememberMe, remember);

    prop_sub_t *remember_sub = 
	prop_subscribe(0,
		   PROP_TAG_CALLBACK_INT, set_remember, &remember,
		   PROP_TAG_ROOT, rememberMe,
		   NULL);

    prop_t *user = prop_create_r(p, "username");
    prop_t *pass = prop_create_r(p, "password");
    prop_t *dom = prop_create_r(p, "domain");
    if(domain != NULL)
      prop_set_string(dom, *domain);

    TRACE(TRACE_INFO, "keyring", "Requesting credentials for %s : %s : %s (%s)",
	  id, source, reason, hts_thread_name(tmpbuf, sizeof(tmpbuf)));


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
	r = prop_get_string(user, NULL);
	htsmsg_add_str(m, "username", r ? rstr_get(r) : "");
	*username = strdup(r ? rstr_get(r) : "");
	rstr_release(r);
      }

      if(domain != NULL) {
	r = prop_get_string(dom, NULL);
	htsmsg_add_str(m, "domain", r ? rstr_get(r) : "");
	*domain = strdup(r ? rstr_get(r) : "");
	rstr_release(r);
      }

      if(password != NULL) {
        r = prop_get_string(pass, NULL);
        htsmsg_add_str(m, "password", r ? rstr_get(r) : "");
        *password = strdup(r ? rstr_get(r) : "");
        rstr_release(r);
      }

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
    prop_ref_dec(p);
    prop_ref_dec(user);
    prop_ref_dec(pass);
    prop_ref_dec(dom);
    prop_ref_dec(rememberMe);

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
