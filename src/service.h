
#ifndef SERVICE_H__
#define SERVICE_H__

#include "misc/queue.h"
#include "prop.h"


/**
 *
 */
typedef enum {
  SVC_TYPE_MUSIC,
  SVC_TYPE_IMAGE,
  SVC_TYPE_VIDEO,
  SVC_TYPE_TV,
  SVC_TYPE_OTHER,
  SVC_num,
} service_type_t;


typedef struct service {
  int s_ref;
  int s_zombie;

  LIST_ENTRY(service) s_link;
  prop_t *s_global_root;
  prop_t *s_type_root;
  prop_t *s_prop_status;
  prop_t *s_prop_status_txt;

  char *s_url;

  service_type_t s_type;

  int s_do_probe;
  int s_need_probe;
} service_t;

LIST_HEAD(service_list, service);
extern struct service_list services;

extern hts_mutex_t service_mutex;



/**
 * Kept in sync with backend_probe_result_t
 */
typedef enum {
  SVC_STATUS_OK,
  SVC_STATUS_AUTH_NEEDED,
  SVC_STATUS_NO_HANDLER,
  SVC_STATUS_FAIL,
  SVC_STATUS_SCANNING,
} service_status_t;


/**
 *
 */
void service_destroy(service_t *s);

service_t *service_create(const char *id,
			  const char *title,
			  const char *url,
			  service_type_t type,
			  const char *icon,
			  int probe);

void service_set_type(service_t *svc, service_type_t type);

void service_set_id(service_t *svc, const char *id);

void service_set_title(service_t *svc, const char *title);

void service_set_icon(service_t *svc, const char *icon);

void service_set_url(service_t *svc, const char *url);

void service_set_status(service_t *svc, service_status_t status);

prop_t *service_get_status_prop(service_t *s);

prop_t *service_get_statustxt_prop(service_t *s);

void service_init(void);

#endif // SERVICE_H__
