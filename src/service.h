
#ifndef SERVICE_H__
#define SERVICE_H__

#include "misc/queue.h"
#include "prop.h"

typedef struct service service_t;

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


/**
 *
 */
typedef enum {
  SVC_STATUS_OK,
  SVC_STATUS_PROBLEM,
  SVC_STATUS_FAIL,
} service_status_t;


/**
 *
 */
void service_destroy(service_t *s);

service_t *service_create(const char *id,
			  const char *title,
			  const char *url,
			  service_type_t type,
			  const char *icon);

void service_set_type(service_t *svc, service_type_t type);

void service_set_id(service_t *svc, const char *id);

void service_set_title(service_t *svc, const char *title);

void service_set_icon(service_t *svc, const char *icon);

void service_set_url(service_t *svc, const char *url);

void service_set_status(service_t *svc, service_status_t status);

void service_init(void);

#endif // SERVICE_H__
