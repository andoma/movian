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
#include <libhts/htssettings.h>

#include "showtime.h"
#include "event.h"
#include "keyring.h"
#include "prop.h"

static htsmsg_t *keyring;
static hts_mutex_t keyring_mutex;


/**
 *
 */
void
keyring_init(void)
{
  hts_mutex_init(&keyring_mutex);
  keyring = hts_settings_load("keyring");
  if(keyring == NULL)
    keyring = htsmsg_create();
}


/**
 *
 */
static void
keyring_store(void)
{
  hts_settings_save(keyring, "keyring");
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
typedef struct keyring_popup {
  int result;
  char *username;
  char *password;

  hts_cond_t cond;
  hts_mutex_t mutex;

} keyring_popup_t;



/**
 *
 */
static void 
result_set(struct prop_sub *sub, prop_event_t event, ...)
{
  const char *str;
  keyring_popup_t *kp = sub->hps_opaque;

  va_list ap;
  va_start(ap, event);

  if(event != PROP_SET_STRING)
    return;

  str = va_arg(ap, const char *);

  if(!strcmp(str, "ok")) 
    kp->result = 1;
  else if(!strcmp(str, "cancel")) 
    kp->result = -1;
  else
    return;
  hts_cond_signal(&kp->cond);
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
  prop_t *p, *popuproot, *r, *user, *pass;
  prop_sub_t *s;
  keyring_popup_t kp;
  char buf[128];

  hts_mutex_lock(&keyring_mutex);

  if(query) {
    kp.result = 0;
    kp.username = NULL;
    kp.password = NULL;
    hts_mutex_init(&kp.mutex);
    hts_cond_init(&kp.cond);

    popuproot = prop_create(prop_get_global(), "popups");

    p = prop_create(NULL, NULL);

    prop_set_string(prop_create(p, "type"), "auth");
    prop_set_string(prop_create(p, "id"), id);
    prop_set_string(prop_create(p, "reason"), reason);
    
    user = prop_create(p, "username");
    pass = prop_create(p, "password");
 
    r = prop_create(p, "result");
    s = prop_subscribe(r, NULL, result_set, &kp, NULL, 0);

    /* Will show the popup */
    prop_set_parent(p, popuproot);

    while(kp.result == 0)
      hts_cond_wait(&kp.cond, &kp.mutex);

    prop_unsubscribe(s);

    htsmsg_delete_field(keyring, id);

    if(kp.result == 1) {
      /* OK */

      m = htsmsg_create();

      if(prop_get_string(user, buf, sizeof(buf)))
	buf[0] = 0;
      htsmsg_add_str(m, "username", buf);

      printf("username = %s\n", buf);

      if(prop_get_string(pass, buf, sizeof(buf)))
	buf[0] = 0;

      htsmsg_add_str(m, "password", buf);

      printf("password = %s\n", buf);

      htsmsg_add_msg(keyring, id, m);

      keyring_store();

    } else {
      /* CANCEL, store without adding anything */
      keyring_store();
    }

    prop_destroy(p);

    hts_cond_destroy(&kp.cond);
    hts_mutex_destroy(&kp.mutex);

    if(kp.result != 1) {
      /* return CANCEL to caller */
      hts_mutex_unlock(&keyring_mutex);
      return -1;
    }
  }

  if((m = htsmsg_get_msg(keyring, id)) == NULL) {
    hts_mutex_unlock(&keyring_mutex);
    return 1;
  }

  setstr(username, m, "username");
  setstr(password, m, "password");
  setstr(domain, m, "domain");

  hts_mutex_unlock(&keyring_mutex);
  return 0;
}
