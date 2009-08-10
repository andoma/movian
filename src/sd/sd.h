
#ifndef SD_H__
#define SD_H__

#include "misc/queue.h"
#include "prop.h"


LIST_HEAD(service_instance_list, service_instance);

typedef enum {
  SERVICE_HTSP,
  SERVICE_WEBDAV,
} service_type_t;

typedef struct service_instance {
  LIST_ENTRY(service_instance) si_link;
  
  char *si_id;
  prop_t *si_root;
  void *si_opaque;
} service_instance_t;


service_instance_t *si_find(struct service_instance_list *services, const char *id);

void si_destroy(service_instance_t *si);

void sd_add_service_htsp(service_instance_t *si, const char *name,
                         const char *host, int port);

void sd_add_service_webdav(service_instance_t *si, const char *name, 
                           const char *host, int port, const char *path);

prop_t *sd_add_service(const char *id, const char *title,
		       const char *icon, prop_t **status);

prop_t *sd_add_link(prop_t *svc, const char *title, const char *url);

void sd_init(void);

#endif
