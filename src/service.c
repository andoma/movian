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
#include "service.h"

static prop_t *global_sources;


struct svc_type_meta {
  prop_t *prop_root;
  prop_t *prop_nodes;
  prop_t *prop_num;

  int num;
  const char *name;
};



static struct svc_type_meta svc_types[SVC_num];


/**
 *
 */
struct service {
  char *s_id;
  prop_t *s_global_root;
  prop_t *s_type_root;

  service_type_t s_type;
};


/**
 *
 */
static void
add_service_type(prop_t *root, service_type_t type, const char *name)
{
  svc_types[type].name = name;
  svc_types[type].prop_root = prop_create(root, name);
  
  svc_types[type].prop_nodes = 
    prop_create_ex(svc_types[type].prop_root, "nodes", NULL, 
		   PROP_SORTED_CHILDS | PROP_SORT_CASE_INSENSITIVE);

  svc_types[type].prop_num = prop_create(svc_types[type].prop_root, "entries");
  prop_set_int(svc_types[type].prop_num, 0);
}


/**
 *
 */
static void
svc_type_mod_num(service_type_t type, int delta)
{
  svc_types[type].num += delta;
  prop_set_int(svc_types[type].prop_num, svc_types[type].num);
}


/**
 *
 */
void
service_init(void)
{
  global_sources =
    prop_create_ex(prop_get_global(), "sources", NULL, 
		   PROP_SORTED_CHILDS | PROP_SORT_CASE_INSENSITIVE);
  prop_t *p = prop_create(prop_get_global(), "services");

  add_service_type(p, SVC_TYPE_MUSIC, "music");
  add_service_type(p, SVC_TYPE_IMAGE, "image");
  add_service_type(p, SVC_TYPE_VIDEO, "video");
  add_service_type(p, SVC_TYPE_TV,    "tv");
  add_service_type(p, SVC_TYPE_OTHER, "other");
}


/**
 *
 */
void 
service_destroy(service_t *s)
{
  svc_type_mod_num(s->s_type, -1);
  prop_destroy(s->s_type_root);
  prop_destroy(s->s_global_root);
  free(s);
}


/**
 *
 */
service_t *
service_create(const char *id,
	       const char *title,
	       const char *url,
	       service_type_t type,
	       const char *icon)
{
  service_t *s = calloc(1, sizeof(service_t));
  prop_t *p;
  s->s_type = type;
  p = s->s_global_root = prop_create(NULL, id);
  s->s_type_root       = prop_create(NULL, id);

  prop_set_string(prop_create(p, "title"), title);
  prop_set_string(prop_create(p, "icon"), icon);
  prop_set_string(prop_create(p, "url"), url);
  prop_set_string(prop_create(p, "type"), svc_types[type].name);

  prop_link(s->s_global_root, s->s_type_root);

  if(prop_set_parent(s->s_global_root, global_sources))
    abort();

  if(prop_set_parent(s->s_type_root, svc_types[type].prop_nodes))
    abort();
  
  svc_type_mod_num(type, 1);
  return s;
}


/**
 *
 */
void 
service_set_type(service_t *s, service_type_t type)
{
  if(s->s_type == type)
    return;
  
  svc_type_mod_num(s->s_type, -1);
  prop_unparent(s->s_type_root);

  s->s_type = type;
  if(prop_set_parent(s->s_type_root, svc_types[s->s_type].prop_nodes))
    abort();

  svc_type_mod_num(s->s_type, 1);
}


/**
 *
 */
void
service_set_id(service_t *s, const char *id)
{
  prop_rename(s->s_global_root, id);
  prop_rename(s->s_type_root, id);
}


/**
 *
 */
void
service_set_title(service_t *s, const char *title)
{
  prop_set_string(prop_create(s->s_global_root, "title"), title);
}


/**
 *
 */
void
service_set_icon(service_t *s, const char *icon)
{

}


/**
 *
 */
void
service_set_url(service_t *s, const char *url)
{
  prop_set_string(prop_create(s->s_global_root, "url"), url);
}


/**
 *
 */
void
service_set_status(service_t *s, service_status_t status)
{

}
