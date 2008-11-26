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

#include <string.h>
#include <inttypes.h>
#include <libhts/htssettings.h>

#include "showtime.h"
#include "event.h"
#include "keyring.h"
#if 0
static htsmsg_t *keyring;

pthread_mutex_t keyring_mutex = PTHREAD_MUTEX_INITIALIZER;


/**
 *
 */
static void
keyring_init(void)
{
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
#endif


/**
 *
 */
int
keyring_lookup(const char *id, char **username, char **password,
	       char **domain, int query, const char *source,
	       const char *reason)
{
  return 1;
#if 0
  htsmsg_t *m;
  glw_t *p, *w;
  prop_t *props;
  event_t *e;
  //  char buf[128];

  pthread_mutex_lock(&keyring_mutex);

  if(keyring == NULL)
    keyring_init();

  if(query && 
     (p = glw_find_by_id(NULL, "auth_query_container", 0)) != NULL) {

    props = prop_create(NULL, "auth");
    prop_set_string(prop_create(props, "resource"), id);
    prop_set_string(prop_create(props, "reason"), reason);
    prop_set_string(prop_create(props, "source"), source);
    prop_set_int(prop_create(props, "domainReq"), !!domain);


    w = glw_model_create(NULL, "theme://authenticate.model", p, 0, props);

    abort(); //glw_select(w); /* Grab focus */

    abort(); //e = glw_wait_form(w);

    prop_destroy(props);

    htsmsg_delete_field(keyring, id);

    if(e->e_type == EVENT_OK) {

      m = htsmsg_create();
#if 0
      glw_get_caption(w, "username", buf, sizeof(buf));
      htsmsg_add_str(m, "username", buf);

      glw_get_caption(w, "password", buf, sizeof(buf));
      htsmsg_add_str(m, "password", buf);

      glw_get_caption(w, "domain", buf, sizeof(buf));
      htsmsg_add_str(m, "domain", buf);
#endif

      htsmsg_add_msg(keyring, id, m);

      keyring_store();
      glw_detach(w);

    } else if(e->e_type == EVENT_CANCEL) {
      keyring_store();
      glw_detach(w);
      pthread_mutex_unlock(&keyring_mutex);
      return -1;
    }
  }


  if((m = htsmsg_get_msg(keyring, id)) == NULL) {
    pthread_mutex_unlock(&keyring_mutex);
    return 1;
  }

  setstr(username, m, "username");
  setstr(password, m, "password");
  setstr(domain, m, "domain");

  pthread_mutex_unlock(&keyring_mutex);
  return 0;
#endif
}
