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

#include "prop/prop.h"
#include "sd.h"
#include "arch/arch.h"
#include "main.h"
#include "service.h"
#include "misc/strtab.h"
#include "settings.h"
#include "htsmsg/htsmsg_store.h"
#include "misc/str.h"

#ifdef CONFIG_AVAHI
#include "avahi.h"
#endif
#ifdef CONFIG_BONJOUR
#include "bonjour.h"
#endif


/**
 *
 */
service_instance_t *
si_find(struct service_instance_list *services, const char *id)
{
  service_instance_t *si;
  
  LIST_FOREACH(si, services, si_link)
  if(!strcmp(si->si_id, id))
    return si;
  
  return NULL;
}


/**
 *
 */
void
si_destroy(service_instance_t *si)
{
  if(si->si_service != NULL)
    service_destroy(si->si_service);

  setting_destroy(si->si_setting_enabled);
  setting_destroy(si->si_setting_title);
  setting_destroy(si->si_setting_type);

  prop_destroy(si->si_settings);

  LIST_REMOVE(si, si_link);

  free(si->si_id);
  free(si->si_url);
  free(si);
}



/**
 *
 */
static void
sd_add_service(service_instance_t *si, const char *title,
	       const char *url, const char *contents,
	       int probe, const char *description)
{
  si->si_probe = probe;

  if(si->si_settings != NULL)
    return;

  char store[512];

  si->si_url = strdup(url);

  snprintf(store, sizeof(store), "sd/%s", url);
  str_cleanup(store + 3, "/:");

  si->si_settings = settings_add_dir_cstr(gconf.settings_sd,
					  title, NULL, NULL,
					  description, NULL);

  si->si_service = service_create(si->si_id, NULL, si->si_url, NULL, NULL,
				  si->si_probe, 0,
				  SVC_ORIGIN_DISCOVERED);

  prop_t *r = si->si_service->s_root;

  si->si_setting_enabled =
    setting_create(SETTING_BOOL, si->si_settings, SETTINGS_INITIAL_UPDATE,
		   SETTING_TITLE(_p("Enabled on home screen")),
		   SETTING_VALUE(0),
		   SETTING_WRITE_PROP(prop_create(r, "enabled")),
		   SETTING_STORE(store, "enabled"),
		   NULL);

  si->si_setting_title =
    setting_create(SETTING_STRING, si->si_settings,
		   SETTINGS_INITIAL_UPDATE,
		   SETTING_TITLE(_p("Name")),
		   SETTING_VALUE(title),
		   SETTING_WRITE_PROP(prop_create(r, "title")),
		   SETTING_STORE(store, "title"),
		   NULL);


  si->si_setting_type =
    setting_create(SETTING_STRING, si->si_settings, SETTINGS_INITIAL_UPDATE,
		   SETTING_TITLE(_p("Type")),
		   SETTING_VALUE(contents),
		   SETTING_WRITE_PROP(prop_create(r, "type")),
		   SETTING_STORE(store, "type"),
		   NULL);
}

/**
 * HTSP service creator
 */
void
sd_add_service_htsp(service_instance_t *si, const char *name, 
		    const char *host, int port)
{
  char url[URL_MAX];
  char buf[256];
  snprintf(url, sizeof(url), "htsp://%s:%d", host, port);
  snprintf(buf, sizeof(buf), "Tvheadend TV streaming server on %s", host);
  sd_add_service(si, name, url, "tv", 0, buf);
}


/**
 * Webdav service creator
 */
void
sd_add_service_webdav(service_instance_t *si, const char *name, 
                      const char *host, int port, const char *path,
		      const char *contents)
{
  char url[URL_MAX];
  char buf[256];
  snprintf(url, sizeof(url), "webdav://%s:%d%s%s", host, port,
	   path == NULL || path[0] != '/' ? "/" : "", path ? path : "");
  snprintf(buf, sizeof(buf), "WEBDAV share on %s:%d%s%s", host, port,
	   path == NULL || path[0] != '/' ? "/" : "", path ? path : "");
  sd_add_service(si, name, url, contents, 1, buf);
}

/**
 *
 */
void
sd_init(void)
{
#ifdef CONFIG_AVAHI
  avahi_init();
#endif
#ifdef CONFIG_BONJOUR
  bonjour_init();
#endif
}
