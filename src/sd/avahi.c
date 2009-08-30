/*
 *  AVAHI based service discovery
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

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>

#include "showtime.h"
#include "misc/queue.h"
#include "arch/threads.h"
#include "prop.h"
#include "avahi.h"
#include "sd.h"


static struct service_instance_list services;


typedef struct service_aux {
  AvahiClient *sa_c;
  service_type_t sa_type;
} service_aux_t;



/**
 *
 */
static void
client_state_change(AvahiClient *c, AvahiClientState state, void *aux)
{
}


/**
 *
 */
static void 
resolve_callback(AvahiServiceResolver *r,
		 AvahiIfIndex interface,
		 AvahiProtocol protocol,
		 AvahiResolverEvent event,
		 const char *name,
		 const char *type,
		 const char *domain,
		 const char *host_name,
		 const AvahiAddress *address,
		 uint16_t port,
		 AvahiStringList *txt,
		 AvahiLookupResultFlags flags,
		 void *userdata)
{
  service_instance_t *si = userdata;
  service_aux_t *sa = si->si_opaque;
  char a[AVAHI_ADDRESS_STR_MAX];
  AvahiStringList *apath;
  char *path;
  AvahiStringList *acontents;
  char *contents;

  switch(event) {
  case AVAHI_RESOLVER_FAILURE:
    TRACE(TRACE_ERROR, "AVAHI",
	  "Failed to resolve service '%s' of type '%s' in domain '%s': %s\n",
	  name, type, domain, 
	  avahi_strerror(avahi_client_errno(sa->sa_c)));
    si_destroy(si);
    break;

  case AVAHI_RESOLVER_FOUND:

    avahi_address_snprint(a, sizeof(a), address);

    TRACE(TRACE_DEBUG, "AVAHI",
	  "Found service '%s' of type '%s' at %s:%d (%s)",
	  name, type, a, port, host_name);

    switch(sa->sa_type) {
    case SERVICE_HTSP:
      sd_add_service_htsp(si, name, a, port);
      break;

    case SERVICE_WEBDAV:
      apath = avahi_string_list_find(txt, "path");
      if(apath == NULL ||
	 avahi_string_list_get_pair(apath, NULL, &path, NULL))
        path = NULL;

      acontents = avahi_string_list_find(txt, "contents");
      if(acontents == NULL ||
	 avahi_string_list_get_pair(acontents, NULL, &contents, NULL))
        contents = NULL;
        
      sd_add_service_webdav(si, name, a, port, path, contents);
        
      if(path)
        avahi_free(path);
      break;
    }
  }
  avahi_service_resolver_free(r);
}


/**
 *
 */
static void
browser(AvahiServiceBrowser *b, AvahiIfIndex interface,
	AvahiProtocol protocol, AvahiBrowserEvent event,
	const char *name, const char *type,
	const char *domain, AvahiLookupResultFlags flags,
	void *userdata)
{
  service_aux_t *sa = userdata;
  service_instance_t *si;
  char fullname[256];

  snprintf(fullname, sizeof(fullname),
	   "%s.%s.%s.%d.%d", name, type, domain, interface, protocol);

  switch(event) {
  case AVAHI_BROWSER_NEW:
    si = calloc(1, sizeof(service_instance_t));
    si->si_opaque = sa;
    si->si_id = strdup(fullname);
    LIST_INSERT_HEAD(&services, si, si_link);

    if(!(avahi_service_resolver_new(sa->sa_c, interface, protocol, 
				    name, type, domain, 
				    AVAHI_PROTO_INET, 0, 
				    resolve_callback, si))) {
      si_destroy(si);
      TRACE(TRACE_ERROR, "AVAHI",
	    "Failed to resolve service '%s': %s\n", 
	    name, avahi_strerror(avahi_client_errno(sa->sa_c)));
    }
    break;

  case AVAHI_BROWSER_REMOVE:
    si = si_find(&services, fullname);
    if(si != NULL)
      si_destroy(si);

    break;
  default:
    break;
  }
}


/**
 *
 */
static void
service_type_add(const char *name, int type, AvahiClient *c)
{
  service_aux_t *sa = malloc(sizeof(service_aux_t));

  sa->sa_c = c;
  sa->sa_type = type;

  avahi_service_browser_new(c, AVAHI_IF_UNSPEC, AVAHI_PROTO_INET,
			    name, NULL, 0, browser, sa);
}


/**
 *
 */
static void *
avahi_thread(void *aux)
{
  AvahiSimplePoll *asp = avahi_simple_poll_new();
  const AvahiPoll *ap = avahi_simple_poll_get(asp);

  AvahiClient *c;

  c = avahi_client_new(ap, AVAHI_CLIENT_NO_FAIL, client_state_change, 
		       NULL, NULL);

  service_type_add("_webdav._tcp", SERVICE_WEBDAV, c);
  service_type_add("_htsp._tcp", SERVICE_HTSP, c);

  while((avahi_simple_poll_iterate(asp, -1)) != -1) {}

  return NULL;
}


/**
 *
 */
void
avahi_init(void)
{
  hts_thread_create_detached(avahi_thread, NULL);
}
