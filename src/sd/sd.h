
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
  char *si_settings_path;
  struct htsmsg *si_settings_store;

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
