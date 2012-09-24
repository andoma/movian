/*
 *  Services
 *  Copyright (C) 2010 Andreas Öman
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

#include "showtime.h"
#include "service.h"
#include "misc/strtab.h"
#include "prop/prop_nodefilter.h"
#include "prop/prop_concat.h"
#include "prop/prop_reorder.h"
#include "backend/backend.h"

LIST_HEAD(service_type_list, service_type);

static prop_t *all_services;

struct service_list services;
hts_mutex_t service_mutex;
static hts_cond_t service_cond;

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
			     THREAD_PRIO_LOW);

  // $global.service.all

  all_services = prop_create(gs, "all");

  service_create0("showtime:plugin",
		  NULL, _p("Plugins"), "plugin:start",
		  "plugin", NULL, 0, 1, SVC_ORIGIN_SYSTEM);

  service_create0("showtime:discovered",
		  NULL, _p("Local network"), "page:discovered",
		  "network", NULL, 0, 1, SVC_ORIGIN_SYSTEM);

  service_create0("showtime:settings",
		  NULL, _p("Settings"), "settings:",
		  "setting", NULL, 0, 1, SVC_ORIGIN_SYSTEM);

  service_create0("showtime:about",
		  NULL, _p("About Showtime"), "page:about",
		  "about", NULL, 0, 1, SVC_ORIGIN_SYSTEM);


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

  prop_nf_pred_str_add(pnf, "node.origin",
		       PROP_NF_CMP_EQ, "discovered", NULL, 
		       PROP_NF_MODE_EXCLUDE);

  prop_t *stable = prop_create(gs, "stable");
  prop_reorder_create(stable, tmp, 0, "allSourcesOrder");

  // $global.service.discovered

  prop_t *discovered = prop_create(gs, "discovered");

  pnf = prop_nf_create(discovered, all_services, NULL, 0);

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

  prop_set_string(prop_create(p, "icon"), icon);
  prop_set_string(prop_create(p, "url"), url);
  prop_set_int(prop_create(p, "enabled"), enabled);

  s->s_prop_type = prop_create(p, "type");
  prop_set_string(s->s_prop_type, type);

  s->s_prop_status = prop_create(p, "status");
  prop_set_string(s->s_prop_status, "ok");

  s->s_prop_status_txt = prop_create(p, "statustxt");

  prop_set_string(prop_create(p, "origin"), val2str(origin, origin_tab));

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
  prop_set_rstring(s->s_prop_type, type);
}


/**
 *
 */
void
service_set_title(service_t *s, rstr_t *title)
{
  prop_set_rstring(prop_create(s->s_root, "title"), title);
}


/**
 *
 */
void
service_set_icon(service_t *s, rstr_t *icon)
{
  prop_set_rstring(prop_create(s->s_root, "icon"), icon);
}


/**
 *
 */
void
service_set_enabled(service_t *s, int v)
{
  prop_set_int(prop_create(s->s_root, "enabled"), v);
}


/**
 *
 */
void
service_set_url(service_t *s, rstr_t *url)
{
  prop_set_rstring(prop_create(s->s_root, "url"), url);

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
      st = (service_status_t)backend_probe(url, txt, sizeof(txt));
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
