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
#ifndef SD_H__
#define SD_H__

#include "misc/queue.h"
#include "prop/prop.h"

LIST_HEAD(service_instance_list, service_instance);

typedef enum {
  SERVICE_HTSP,
  SERVICE_WEBDAV,
} service_class_t;

typedef struct service_instance {
  LIST_ENTRY(service_instance) si_link;

  char *si_id;
  void *si_opaque;

  char *si_url;

  prop_t *si_settings;

  struct setting *si_setting_enabled;
  struct setting *si_setting_title;
  struct setting *si_setting_type;

  int si_probe;
  int si_enabled;

  struct service *si_service;

} service_instance_t;


service_instance_t *si_find(struct service_instance_list *services, 
			    const char *id);

void si_destroy(service_instance_t *si);

void sd_add_service_htsp(service_instance_t *si, const char *name,
                         const char *host, int port);

void sd_add_service_webdav(service_instance_t *si, const char *name, 
                           const char *host, int port, const char *path,
			   const char *contents);

void sd_init(void);

#endif
