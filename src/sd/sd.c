/*
 *  Service discovery
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

#include <stdio.h>
#include <string.h>

#include "prop/prop.h"
#include "sd.h"
#include "arch/arch.h"
#include "showtime.h"
#include "service.h"
#include "misc/strtab.h"

#ifdef CONFIG_AVAHI
#include "avahi.h"
#endif
#ifdef CONFIG_BONJOUR
#include "bonjour.h"
#endif


extern prop_t *global_sources;


service_instance_t *
si_find(struct service_instance_list *services,
        const char *id)
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
  int i;
  for(i = 0; i < SI_MAX_SERVICES; i++)
    if(si->si_services[i] != NULL)
      service_destroy(si->si_services[i]);

  free(si->si_id);
  LIST_REMOVE(si, si_link);
  free(si);
}


/**
 * HTSP service creator
 */
void
sd_add_service_htsp(service_instance_t *si, const char *name, 
		    const char *host, int port)
{
  char url[URL_MAX];


  if(si->si_services[0] != NULL)
    return;

  snprintf(url, sizeof(url), "htsp://%s:%d", host, port);
  si->si_services[0] = service_create(name, url, "tv", NULL, 0);
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

  if(si->si_services[0] != NULL)
    return;

  snprintf(url, sizeof(url), "webdav://%s:%d%s%s",
	   host, port, path == NULL || path[0] != '/' ? "/" : "",
	   path ? path : "");

  si->si_services[0] = service_create(name, url, contents, NULL, 1);
}

/**
 *
 */
void
sd_init(void)
{
  arch_sd_init();

#ifdef CONFIG_AVAHI
  avahi_init();
#endif
#ifdef CONFIG_BONJOUR
  bonjour_init();
#endif
}
