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
#include "main.h"
#include "fileaccess/http_client.h"
#include "asyncio.h"

LIST_HEAD(asyncio_http_req_list, asyncio_http_req);

static hts_mutex_t asyncio_http_mutex;
static int asyncio_http_worker;
static struct asyncio_http_req_list asyncio_http_completed;



/*************************************************************************
 * HTTP(S)
 *************************************************************************/

struct asyncio_http_req {
  LIST_ENTRY(asyncio_http_req) ahr_link;
  int ahr_cancelled;

  http_req_aux_t *ahr_req;

  void (*ahr_cb)(http_req_aux_t *hra, void *opaque);
  void *ahr_opaque;
};


/**
 *
 */
static void
asyncio_http_cb(http_req_aux_t *hra, void *opaque, int error)
{
  asyncio_http_req_t *ahr = opaque;
  ahr->ahr_req = http_req_retain(hra);

  // This arrives on a different thread so we need to reschedule

  hts_mutex_lock(&asyncio_http_mutex);
  LIST_INSERT_HEAD(&asyncio_http_completed, ahr, ahr_link);
  hts_mutex_unlock(&asyncio_http_mutex);
  asyncio_wakeup_worker(asyncio_http_worker);
}


/**
 *
 */
asyncio_http_req_t *
asyncio_http_req(const char *url,
                 void (*cb)(http_req_aux_t *req, void *opaque),
                 void *opaque,
                 ...)
{
  asyncio_http_req_t *ahr = calloc(1, sizeof(asyncio_http_req_t));
  va_list ap;

  ahr->ahr_cb = cb;
  ahr->ahr_opaque = opaque;

  va_start(ap, opaque);
  http_reqv(url, ap, asyncio_http_cb, ahr);
  va_end(ap);
  return ahr;
}


/**
 *
 */
void
asyncio_http_cancel(asyncio_http_req_t *ahr)
{
  ahr->ahr_cancelled = 1;
}


/**
 *
 */
static void
ahr_deliver_cb(void)
{
  asyncio_http_req_t *ahr;

  hts_mutex_lock(&asyncio_http_mutex);

  while((ahr = LIST_FIRST(&asyncio_http_completed)) != NULL) {
    LIST_REMOVE(ahr, ahr_link);
    hts_mutex_unlock(&asyncio_http_mutex);

    if(!ahr->ahr_cancelled)
      ahr->ahr_cb(ahr->ahr_req, ahr->ahr_opaque);

    http_req_release(ahr->ahr_req);
    free(ahr);


    hts_mutex_lock(&asyncio_http_mutex);
  }

  hts_mutex_unlock(&asyncio_http_mutex);
}


/**
 *
 */
static void
asyncio_http_init(void)
{
  hts_mutex_init(&asyncio_http_mutex);
  asyncio_http_worker = asyncio_add_worker(ahr_deliver_cb);
}


INITME(INIT_GROUP_ASYNCIO, asyncio_http_init, NULL, 0);

