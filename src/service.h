
#ifndef SERVICE_H__
#define SERVICE_H__

#include "misc/queue.h"
#include "prop/prop.h"

LIST_HEAD(service_list, service);
extern struct service_list services;

extern hts_mutex_t service_mutex;


/**
 *
 */
typedef struct service {
  int s_ref;
  int s_zombie;

  LIST_ENTRY(service) s_link;
  prop_t *s_root;

  prop_t *s_prop_type;
  prop_t *s_prop_status;
  prop_t *s_prop_status_txt;

  char *s_url;

  int s_do_probe;
  int s_need_probe;
} service_t;




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

service_t *service_create(const char *title,
			  const char *url,
			  const char *type,
			  const char *icon,
			  int probe,
			  int enabled);

void service_set_type(service_t *svc, rstr_t *type);

void service_set_title(service_t *svc, rstr_t *title);

void service_set_icon(service_t *svc, rstr_t *icon);

void service_set_url(service_t *svc, rstr_t *url);

void service_set_enabled(service_t *svc, int v);

void service_set_status(service_t *svc, service_status_t status);

prop_t *service_get_status_prop(service_t *s);

prop_t *service_get_statustxt_prop(service_t *s);

void service_init(void);

#endif // SERVICE_H__
