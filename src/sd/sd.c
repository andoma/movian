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

#include "prop.h"
#include "sd.h"

extern prop_t *global_sources;

/**
 *
 */
prop_t *
sd_add_service(const char *id, const char *title,
	       const char *icon, prop_t **status)
{
  prop_t *p = prop_create(NULL, id);
  
  prop_set_string(prop_create(p, "title"), title);

  if(status != NULL)
    *status = prop_create(p, "status");

  prop_set_string(prop_create(p, "icon"), icon);

  if(prop_set_parent(p, global_sources))
    abort();

  return p;
}


/**
 *
 */
prop_t *
sd_add_link(prop_t *svc, const char *title, const char *url)
{
  prop_t *links, *link;

  links = prop_create(svc, "links");

  link = prop_create(links, NULL);
  prop_set_string(prop_create(link, "title"), title);
  prop_set_string(prop_create(link, "url"),  url);

  return link;
}
