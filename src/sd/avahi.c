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
#include <avahi-client/publish.h>

#include <avahi-common/alternative.h>

#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>

#include "showtime.h"
#include "misc/queue.h"
#include "arch/threads.h"
#include "prop/prop.h"
#include "avahi.h"
#include "sd.h"


static struct service_instance_list services;


typedef struct service_aux {
  AvahiClient *sa_c;
  service_class_t sa_class;
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

    switch(sa->sa_class) {
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
service_type_add(const char *name, service_class_t class, AvahiClient *c)
{
  service_aux_t *sa = malloc(sizeof(service_aux_t));

  sa->sa_c = c;
  sa->sa_class = class;

  avahi_service_browser_new(c, AVAHI_IF_UNSPEC, AVAHI_PROTO_INET,
			    name, NULL, 0, browser, sa);
}



















#if ENABLE_AIRPLAY

static AvahiEntryGroup *group = NULL;
static char *name = NULL;

static void create_services(AvahiClient *c);

static void
entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, 
		     void *userdata)
{
  char *n;

  assert(g == group || group == NULL);
  group = g;

  /* Called whenever the entry group state changes */

  switch (state) {
  case AVAHI_ENTRY_GROUP_ESTABLISHED :
    /* The entry group has been established successfully */
    TRACE(TRACE_INFO, "AVAHI",
	   "Service '%s' successfully established.", name);
    break;

  case AVAHI_ENTRY_GROUP_COLLISION:

    /* A service name collision with a remote service
     * happened. Let's pick a new name */
    n = avahi_alternative_service_name(name);
    avahi_free(name);
    name = n;
    
    TRACE(TRACE_ERROR, "AVAHI",
	   "Service name collision, renaming service to '%s'", name);

    /* And recreate the services */
    create_services(avahi_entry_group_get_client(g));
    break;

  case AVAHI_ENTRY_GROUP_FAILURE:
     TRACE(TRACE_ERROR, "AVAHI",
	    "Entry group failure: %s", 
	    avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g))));
    break;

  case AVAHI_ENTRY_GROUP_UNCOMMITED:
  case AVAHI_ENTRY_GROUP_REGISTERING:
    ;
  }
}


/**
 *
 */
static void 
create_services(AvahiClient *c) 
{
  char *n;
  int ret;
  assert(c);

  /* If this is the first time we're called, let's create a new
   * entry group if necessary */

  if (!group)
    if (!(group = avahi_entry_group_new(c, entry_group_callback, NULL))) {
      TRACE(TRACE_ERROR, "AVAHI",
	     "avahi_enty_group_new() failed: %s", 
	     avahi_strerror(avahi_client_errno(c)));
      goto fail;
    }

  /* If the group is empty (either because it was just created, or
   * because it was reset previously, add our entries.  */

  if (avahi_entry_group_is_empty(group)) {
     TRACE(TRACE_DEBUG, "AVAHI", "Adding service '%s'", name);

    /* Add the service for HTSP */
    if ((ret = avahi_entry_group_add_service(group, AVAHI_IF_UNSPEC, 
					     AVAHI_PROTO_UNSPEC, 0, name, 
					     "_airplay._tcp", NULL, NULL, 42000,
					     NULL)) < 0) {

      if (ret == AVAHI_ERR_COLLISION)
	goto collision;

      TRACE(TRACE_ERROR, "AVAHI",
	     "Failed to add _airplay._tcp service: %s", 
	     avahi_strerror(ret));
      goto fail;
    }


    /* Tell the server to register the service */
    if ((ret = avahi_entry_group_commit(group)) < 0) {
      TRACE(TRACE_ERROR, "AVAHI",
	     "Failed to commit entry group: %s", 
	     avahi_strerror(ret));
      goto fail;
    }
  }

  return;

 collision:

  /* A service name collision with a local service happened. Let's
   * pick a new name */
  n = avahi_alternative_service_name(name);
  avahi_free(name);
  name = n;

  TRACE(TRACE_ERROR, "AVAHI",
	 "Service name collision, renaming service to '%s'", name);

  avahi_entry_group_reset(group);

  create_services(c);
  return;

 fail:
  return;
}


/**
 *
 */
static void
client_callback(AvahiClient *c, AvahiClientState state, void *userdata)
{
  assert(c);

  /* Called whenever the client or server state changes */

  switch (state) {
  case AVAHI_CLIENT_S_RUNNING:

    /* The server has startup successfully and registered its host
     * name on the network, so it's time to create our services */
    create_services(c);
    break;

  case AVAHI_CLIENT_FAILURE:
    TRACE(TRACE_ERROR, "AVAHI", "Client failure: %s", 
	   avahi_strerror(avahi_client_errno(c)));
    break;

  case AVAHI_CLIENT_S_COLLISION:

    /* Let's drop our registered services. When the server is back
     * in AVAHI_SERVER_RUNNING state we will register them
     * again with the new host name. */

  case AVAHI_CLIENT_S_REGISTERING:

    /* The server records are now being established. This
     * might be caused by a host name change. We need to wait
     * for our own records to register until the host name is
     * properly esatblished. */

    if(group)
      avahi_entry_group_reset(group);

    break;

  case AVAHI_CLIENT_CONNECTING:
    ;
  }
}
#endif

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

#if ENABLE_AIRPLAY
  name = strdup("Showtime");
  avahi_client_new(ap, AVAHI_CLIENT_NO_FAIL, client_callback, NULL, NULL);
#endif

  while((avahi_simple_poll_iterate(asp, -1)) != -1) {}

  return NULL;
}


/**
 *
 */
void
avahi_init(void)
{
  hts_thread_create_detached("AVAHI", avahi_thread, NULL,
			     THREAD_PRIO_BGTASK);
}
