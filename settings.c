/*
 *  Settings framework
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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "showtime.h"
#include "settings.h"
#include "event.h"
#include "navigator.h"

#define SETTINGS_URI "settings:"

nav_backend_t be_settings;
static prop_t *settings_root;


/**
 *
 */
struct setting {
  void *s_opaque;
  void *s_callback;
  prop_sub_t *s_sub;
};



/**
 *
 */
static prop_t *
settings_add(const char *id, const char *title, const char *type)
{
  prop_t *r = prop_create(NULL, id);

  prop_set_string(prop_create(r, "id"), id);
  prop_set_string(prop_create(r, "title"), title);
  prop_set_string(prop_create(r, "type"), type);
  return r;
}


/**
 *
 */
static void
settings_set_url(prop_t *p, prop_t *parent)
{
  char url[200];
  prop_t **a;
  int i;

  if(parent != NULL) {

    abort();

    a = prop_get_ancestors(parent);
    i = 0;
    while(a[i] != NULL) {
      printf("ancestor: %s\n", a[i++]->hp_name);
    }

    url[0] = 0;


    prop_ancestors_unref(a);

  } else {
    snprintf(url, sizeof(url), SETTINGS_URI"%s", p->hp_name);
  }

  prop_set_string(prop_create(p, "url"), url);
}



/**
 *
 */
static void
settings_set_parent(prop_t *p, prop_t *parent)
{
  parent = parent ? prop_create(parent, "nodes") : settings_root;
  prop_set_parent(p, parent);
}




/**
 *
 */
prop_t *
settings_add_dir(prop_t *parent, const char *id, const char *title)
{
  prop_t *r = settings_add(id, title, "directory");
  settings_set_url(r, parent);
  settings_set_parent(r, parent);
  return r;
}


/**
 *
 */
static void 
callback_bool(struct prop_sub *sub, prop_event_t event, ...)
{
  setting_t *s = sub->hps_opaque;
  setting_callback_bool_t *cb;

  va_list ap;
  va_start(ap, event);

  if(event == PROP_SET_INT) {
    cb = s->s_callback;
    cb(s->s_opaque, va_arg(ap, int));
  }
}


/**
 *
 */
setting_t *
settings_add_bool(prop_t *parent, const char *id, const char *title,
		  int initial, htsmsg_t *store,
		  setting_callback_bool_t *cb, void *opaque)
{
  prop_t *r = settings_add(id, title, "bool");
  prop_t *v = prop_create(r, "value");
  setting_t *s = malloc(sizeof(setting_t));
  prop_sub_t *sub;

  if(store != NULL)
    initial = htsmsg_get_u32_or_default(store, id, initial);

  prop_set_int(v, !!initial);

  s->s_callback = cb;
  s->s_opaque = opaque;
  
  sub = prop_subscribe(v, NULL, callback_bool, s);
  s->s_sub = sub;
  
  settings_set_parent(r, parent);
  return s;
}



/**
 *
 */
void
settings_init(void)
{
  settings_root = prop_create(prop_get_global(), "settings");
}




/**
 *
 */
static int
be_settings_canhandle(const char *url)
{
  return !strncmp(url, SETTINGS_URI, strlen(SETTINGS_URI));
}





/**
 *
 */
static nav_page_t *
be_settings_open(const char *url0, char *errbuf, size_t errlen)
{
  nav_page_t *n;
  prop_t *type, *nodes, *p;
  const char *url = url0 + strlen(SETTINGS_URI);
  char buf[100];
  int l;

  printf("Decomposing %s\n", url);

  p = settings_root;
  /* Decompose URL and try to find representative node */

  while(*url) {
    l = 0;
    while(*url && *url != '/' && l < sizeof(buf) - 2) {
      buf[l++] = *url++;
    }
    buf[l] = 0;
    if(*url == '/')
      url++;
    
    printf("component = %s\n", buf);

    if(p->hp_type != PROP_DIR) {
      snprintf(errbuf, errlen, "Settings property is not a directory");
      return NULL;
    }

    printf("creating %s in prop %s\n", buf, propname(p));

    p = prop_create(p, buf);

    printf("creating %s in prop %s\n", "nodes", propname(p));

    p = prop_create(p, "nodes");
  }



  n = nav_page_create(&be_settings, url0, sizeof(nav_page_t));

  type  = prop_create(n->np_prop_root, "type");
  nodes = prop_create(n->np_prop_root, "nodes");

  prop_set_string(type, "settings");

  prop_link(p, nodes);
  return n;
}


/**
 *
 */
nav_backend_t be_settings = {
  .nb_canhandle = be_settings_canhandle,
  .nb_open = be_settings_open,
};


