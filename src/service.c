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

#include "main.h"
#include "service.h"
#include "misc/strtab.h"
#include "misc/str.h"
#include "prop/prop_nodefilter.h"
#include "prop/prop_concat.h"
#include "prop/prop_reorder.h"
#include "backend/backend.h"
#include "htsmsg/htsmsg_store.h"
#include "settings.h"
#include "usage.h"

LIST_HEAD(service_type_list, service_type);

static prop_t *all_services;

struct service_list services;
hts_mutex_t service_mutex;
static hts_cond_t service_cond;
static prop_t *discovered_nodes;

/**
 *
 */
static struct strtab status_tab[] = {
  {"ok",        SVC_STATUS_OK},
  {"auth",      SVC_STATUS_AUTH_NEEDED},
  {"nohandler", SVC_STATUS_NO_HANDLER},
  {"fail",      SVC_STATUS_FAIL},
  {"scanning",  SVC_STATUS_SCANNING},
};

/**
 *
 */
static struct strtab origin_tab[] = {

  { "system",     SVC_ORIGIN_SYSTEM },
  { "bookmark",   SVC_ORIGIN_BOOKMARK },
  { "discovered", SVC_ORIGIN_DISCOVERED },
  { "app",        SVC_ORIGIN_APP },
  { "media",      SVC_ORIGIN_MEDIA },
};

static service_t *service_create0(const char *id,
				  const char *title,
				  prop_t *ptitle,
				  const char *url,
				  const char *type,
				  const char *icon,
				  int probe,
				  int enabled,
				  service_origin_t origin);

static void *service_probe_loop(void *aux);


/**
 *
 *  $global.services. ..
 *
 *     all - All services
 *     enabled - All enabled services
 *     stable - All services that are not auto-discovered
 *     discovered - All auto-discovered services
 *
 */

void
service_init(void)
{
  struct prop_nf *pnf;
  prop_t *gs = prop_create(prop_get_global(), "services");

  hts_mutex_init(&service_mutex);
  hts_cond_init(&service_cond, &service_mutex);

  hts_thread_create_detached("service probe", service_probe_loop, NULL,
			     THREAD_PRIO_BGTASK);

  // $global.service.all

  all_services = prop_create(gs, "all");

  service_create0("showtime:discovered",
		  NULL, _p("Local network"), "discovered:",
		  "network", NULL, 0, 1, SVC_ORIGIN_SYSTEM);

  service_create0("showtime:settings",
		  NULL, _p("Settings"), "settings:",
		  "setting", NULL, 0, 1, SVC_ORIGIN_SYSTEM);


  // $global.service.enabled

  prop_t *enabled = prop_create(gs, "enabled");

  pnf = prop_nf_create(enabled, all_services, NULL, 0);
  prop_nf_pred_int_add(pnf, "node.enabled",
		       PROP_NF_CMP_EQ, 0, NULL,
		       PROP_NF_MODE_EXCLUDE);

  // $global.service.stable

  prop_t *tmp = prop_create_root(NULL);

  pnf = prop_nf_create(tmp, all_services, NULL, 0);
  prop_nf_pred_int_add(pnf, "node.enabled",
		       PROP_NF_CMP_EQ, 0, NULL,
		       PROP_NF_MODE_EXCLUDE);

  prop_t *stable = prop_create(gs, "stable");
  prop_reorder_create(stable, tmp, 0, "allSourcesOrder");

  // $global.service.discovered

  discovered_nodes = prop_create(gs, "discovered");

  pnf = prop_nf_create(discovered_nodes, all_services, NULL, 0);

  prop_nf_pred_str_add(pnf, "node.origin",
		       PROP_NF_CMP_NEQ, "discovered", NULL,
		       PROP_NF_MODE_EXCLUDE);
}


/**
 *
 */
void
service_destroy(service_t *s)
{
  hts_mutex_lock(&service_mutex);
  prop_destroy(s->s_root);
  free(s->s_url);
  free(s->s_title);

  if(s->s_setting_enabled != NULL)
    setting_destroy(s->s_setting_enabled);
  if(s->s_setting_title != NULL)
    setting_destroy(s->s_setting_title);
  if(s->s_setting_type != NULL)
    setting_destroy(s->s_setting_type);

  prop_destroy(s->s_settings);

  if(s->s_req_del_sub != NULL)
    prop_unsubscribe(s->s_req_del_sub);

  LIST_REMOVE(s, s_link);
  s->s_zombie = 1;
  if(--s->s_ref == 0)
    free(s);
  hts_mutex_unlock(&service_mutex);
}


/**
 *
 */
static void
service_reprobe(service_t *s)
{
  if(!s->s_do_probe)
    return;
  s->s_need_probe = 1;
  hts_cond_signal(&service_cond);
}


/**
 *
 */
static void
seturl(service_t *s, const char *url)
{
  char urlbuf[URL_MAX];
  backend_t *be;

  be = url ? backend_canhandle(url) : NULL;
  if(be != NULL && be->be_normalize != NULL &&
     !be->be_normalize(url, urlbuf, sizeof(urlbuf)))
    mystrset(&s->s_url, urlbuf);
  else
    mystrset(&s->s_url, url);
}


/**
 *
 */
static void
service_req_del_callback(void *opaque, prop_event_t event)
{
  service_t *s = opaque;
  if(event == PROP_REQ_DELETE)
    setting_set(s->s_setting_enabled, SETTING_BOOL, 0);
}


/**
 *
 */
static service_t *
service_create0(const char *id,
		const char *title,
		prop_t *ptitle,
		const char *url,
		const char *type,
		const char *icon,
		int probe,
		int enabled,
		service_origin_t origin)
{
  service_t *s = calloc(1, sizeof(service_t));
  prop_t *p;
  s->s_ref = 1;

  p = s->s_root = prop_create_root(id);
  seturl(s, url);

  prop_t *t = prop_create(p, "title");
  if(ptitle)
    prop_link(ptitle, t);
  else
    prop_set_string(t, title);

  prop_t *metadata = prop_create(p, "metadata");
  prop_link(t, prop_create(metadata, "title"));

  prop_set(p, "icon", PROP_SET_STRING, icon);
  prop_set(p, "url", PROP_SET_STRING, url);
  prop_set(p, "enabled", PROP_SET_INT, enabled);

  s->s_prop_type = prop_create(p, "type");
  prop_set_string(s->s_prop_type, type);

  s->s_prop_status = prop_create(p, "status");
  prop_set_string(s->s_prop_status, "ok");

  s->s_prop_status_txt = prop_create(p, "statustxt");

  prop_set(p, "origin", PROP_SET_STRING, val2str(origin, origin_tab));

  if(prop_set_parent(s->s_root, all_services))
    abort();

  hts_mutex_lock(&service_mutex);
  LIST_INSERT_HEAD(&services, s, s_link);
  s->s_need_probe = s->s_do_probe = probe;
  hts_cond_signal(&service_cond);
  hts_mutex_unlock(&service_mutex);
  return s;
}

/**
 *
 */
service_t *
service_create(const char *id,
	       const char *title,
	       const char *url,
	       const char *type,
	       const char *icon,
	       int probe,
	       int enabled,
	       service_origin_t origin)
{
  return service_create0(id, title, NULL, url, type,
			 icon, probe, enabled, origin);
}


/**
 *
 */
service_t *
service_createp(const char *id,
                prop_t *ptitle,
                const char *url,
                const char *type,
                const char *icon,
                int probe,
                int enabled,
                service_origin_t origin)
{
  return service_create0(id, NULL, ptitle, url, type,
			 icon, probe, enabled, origin);
}


/**
 *
 */
service_t *
service_create_managed(const char *id0,
		       const char *title,
		       const char *url,
		       const char *type,
		       const char *icon,
		       int probe,
		       int enabled,
		       service_origin_t origin)
{

  char *id = mystrdupa(id0);
  str_cleanup(id , "/:.");

  service_t *s = service_create0(id, NULL, NULL, url, NULL,
				 icon, probe, enabled, origin);

  s->s_title = strdup(title);

  char store[100];

  snprintf(store, sizeof(store), "managed_service2/%s", id);

  s->s_settings = settings_add_dir(gconf.settings_sd,
                                   prop_create(s->s_root, "title"),
                                   type, icon, NULL, NULL);

  s->s_setting_enabled =
    setting_create(SETTING_BOOL, s->s_settings, SETTINGS_INITIAL_UPDATE,
		   SETTING_TITLE(_p("Enabled on home screen")),
		   SETTING_VALUE(enabled),
		   SETTING_WRITE_PROP(prop_create(s->s_root, "enabled")),
                   SETTING_STORE(store, "enabled"),
		   NULL);

  s->s_setting_title =
    setting_create(SETTING_STRING, s->s_settings,
		   SETTINGS_INITIAL_UPDATE,
		   SETTING_TITLE(_p("Name")),
		   SETTING_VALUE(title),
		   SETTING_WRITE_PROP(prop_create(s->s_root, "title")),
                   SETTING_STORE(store, "title"),
		   NULL);


  s->s_setting_type =
      setting_create(SETTING_STRING, s->s_settings, SETTINGS_INITIAL_UPDATE,
                     SETTING_TITLE(_p("Type")),
		     SETTING_VALUE(type),
		     SETTING_WRITE_PROP(prop_create(s->s_root, "type")),
                     SETTING_STORE(store, "type"),
                     NULL);

  prop_set(s->s_root, "deleteText",
           PROP_SET_LINK, _p("Remove from homepage"));

  hts_mutex_lock(&service_mutex);
  s->s_req_del_sub =
    prop_subscribe(0,
                   PROP_TAG_CALLBACK, service_req_del_callback, s,
                   PROP_TAG_MUTEX, &service_mutex,
                   PROP_TAG_ROOT, s->s_root,
                   NULL);
  hts_mutex_unlock(&service_mutex);
  return s;
}


/**
 *
 */
prop_t *
service_get_status_prop(service_t *s)
{
  return s->s_prop_status;
}


/**
 *
 */
prop_t *
service_get_statustxt_prop(service_t *s)
{
  return s->s_prop_status_txt;
}


/**
 *
 */
void
service_set_type(service_t *s, rstr_t *type)
{
  if(s->s_setting_type != NULL) {
    // TODO, propagate thru setting
  } else {
    prop_set_rstring(s->s_prop_type, type);
  }
}


/**
 *
 */
void
service_set_title(service_t *s, rstr_t *title)
{
  if(s->s_setting_title != NULL) {
    // TODO, propagate thru setting
  } else {
    prop_set(s->s_root, "title", PROP_SET_RSTRING, title);
  }
}


/**
 *
 */
void
service_set_icon(service_t *s, rstr_t *icon)
{
  prop_set(s->s_root, "icon", PROP_SET_RSTRING, icon);
}


/**
 *
 */
void
service_set_enabled(service_t *s, int v)
{
  if(s->s_setting_enabled != NULL) {
    // TODO, propagate thru setting
  } else {
    prop_set(s->s_root, "enabled", PROP_SET_INT, v);
  }
}


/**
 *
 */
void
service_set_url(service_t *s, rstr_t *url)
{
  prop_set(s->s_root, "url", PROP_SET_RSTRING, url);

  hts_mutex_lock(&service_mutex);
  seturl(s, rstr_get(url));
  service_reprobe(s);
  hts_mutex_unlock(&service_mutex);
}


/**
 *
 */
void
service_set_status(service_t *s, service_status_t status)
{

}


/**
 *
 */
static void *
service_probe_loop(void *aux)
{
  service_t *s;
  char *url;
  service_status_t st;
  char txt[256];

  hts_mutex_lock(&service_mutex);

  while(1) {

    LIST_FOREACH(s, &services, s_link) {
      if(s->s_need_probe)
	break;
    }

    if(s == NULL) {
      hts_cond_wait(&service_cond, &service_mutex);
      continue;
    }
    s->s_need_probe = 0;
    // Will release lock, so reference and copy URL
    s->s_ref++;

    prop_set_string(s->s_prop_status, val2str(SVC_STATUS_SCANNING, status_tab));

    if(s->s_url == NULL) {
      st = SVC_STATUS_FAIL;
    } else {
      url = strdup(s->s_url);

       // backend_probe() can take a lot of time so we unlock
      hts_mutex_unlock(&service_mutex);
      st = (service_status_t)backend_probe(url, txt, sizeof(txt), 0);
      free(url);
      hts_mutex_lock(&service_mutex);
    }

    if(!s->s_zombie) {
      prop_set_string(s->s_prop_status, val2str(st, status_tab));
      if(st != SVC_STATUS_OK)
	prop_set_string(s->s_prop_status_txt, txt);
      else
	prop_set_void(s->s_prop_status_txt);
    }
    if(--s->s_ref == 0)
      free(s);
  }
  return NULL;
}




/**
 *
 */
static int
discovered_open_url(prop_t *page, const char *url, int sync)
{
  usage_page_open(sync, "Discovered");

  prop_t *model = prop_create_r(page, "model");

  prop_setv(model, "metadata", "title", NULL,
            PROP_SET_LINK, _p("Local network"));

  prop_set(model, "type",     PROP_SET_STRING, "directory");

  prop_t *nodes = prop_create_r(model, "nodes");

  prop_link(discovered_nodes, nodes);

  prop_ref_dec(nodes);
  prop_ref_dec(model);
  return 0;
}


/**
 *
 */
static int
discovered_canhandle(const char *url)
{
  return !strcmp(url, "discovered:");
}



/**
 *
 */
static backend_t be_discovered = {
  .be_canhandle = discovered_canhandle,
  .be_open = discovered_open_url,
};

BE_REGISTER(discovered);
