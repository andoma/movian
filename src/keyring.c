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
#include <htsmsg/htsmsg_store.h>

#include "showtime.h"
#include "event.h"
#include "keyring.h"
#include "prop/prop.h"
#include "notifications.h"

static htsmsg_t *keyring;
static hts_mutex_t keyring_mutex;


/**
 *
 */
void
keyring_init(void)
{
  hts_mutex_init(&keyring_mutex);
  keyring = htsmsg_store_load("keyring");
  if(keyring == NULL)
    keyring = htsmsg_create_map();
}


/**
 *
 */
static void
keyring_store(void)
{
  htsmsg_store_save(keyring, "keyring");
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
int
keyring_lookup(const char *id, char **username, char **password,
	       char **domain, int query, const char *source,
	       const char *reason)
{
  htsmsg_t *m;

  hts_mutex_lock(&keyring_mutex);

  if(query) {
    char buf[128];

    prop_t *p = prop_create(NULL, NULL);

    prop_set_string(prop_create(p, "type"), "auth");
    prop_set_string(prop_create(p, "id"), id);
    prop_set_string(prop_create(p, "source"), source);
    prop_set_string(prop_create(p, "reason"), reason);
    
    prop_t *user = prop_create(p, "username");
    prop_t *pass = prop_create(p, "password");
 
    event_t *e = popup_display(p);

    htsmsg_delete_field(keyring, id);

    if(event_is_action(e, ACTION_OK)) {
      /* OK */

      m = htsmsg_create_map();

      if(prop_get_string(user, buf, sizeof(buf)))
	buf[0] = 0;
      htsmsg_add_str(m, "username", buf);

      if(prop_get_string(pass, buf, sizeof(buf)))
	buf[0] = 0;

      htsmsg_add_str(m, "password", buf);

      htsmsg_add_msg(keyring, id, m);

      keyring_store();

    } else {
      /* CANCEL, store without adding anything */
      keyring_store();
    }

    prop_destroy(p);

    if(event_is_action(e, ACTION_CANCEL)) {
      /* return CANCEL to caller */
      hts_mutex_unlock(&keyring_mutex);
      event_release(e);
      return -1;
    }
    event_release(e);
  }

  if((m = htsmsg_get_map(keyring, id)) == NULL) {
    hts_mutex_unlock(&keyring_mutex);
    return 1;
  }

  setstr(username, m, "username");
  setstr(password, m, "password");
  setstr(domain, m, "domain");

  hts_mutex_unlock(&keyring_mutex);
  return 0;
}
