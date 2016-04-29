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
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "main.h"
#include "navigator.h"
#include "backend/backend.h"
#include "misc/str.h"
#include "networking/http_server.h"

#include "ecmascript.h"

#if ENABLE_HTTPSERVER

/**
 *
 */
static void
dump_resource_list(htsbuf_queue_t *out, struct es_resource_list *list)
{
  char buf[512];
  es_resource_t *er;
  LIST_FOREACH(er, list, er_link) {
    if(er->er_class->erc_info != NULL) {
      er->er_class->erc_info(er, buf, sizeof(buf));
      htsbuf_qprintf(out, "\t%s: %s\n", er->er_class->erc_name, buf);
    } else {
      htsbuf_qprintf(out, "\t%s\n", er->er_class->erc_name);
    }
  }
}

/**
 *
 */
static void
dump_context(htsbuf_queue_t *out, es_context_t *ec)
{
  hts_mutex_lock(&ec->ec_mutex);

  htsbuf_qprintf(out, "\n--- %s ------------------------\n",
                 rstr_get(ec->ec_id));

  htsbuf_qprintf(out, "  Loaded from %s\n", ec->ec_path);

  htsbuf_qprintf(out, "  Memory usage, current: %zd bytes, peak: %zd\n",
                 ec->ec_mem_active, ec->ec_mem_peak);

  htsbuf_qprintf(out, "  Attached permanent resources:\n");
  dump_resource_list(out, &ec->ec_resources_permanent);

  hts_mutex_unlock(&ec->ec_mutex);
}


/**
 *
 */
static int
dumpstats(http_connection_t *hc, const char *remain, void *opaque,
          http_cmd_t method)
{
  int i;

  htsbuf_queue_t out;
  htsbuf_queue_init(&out, 0);

  es_context_t **vec = ecmascript_get_all_contexts();

  for(i = 0; vec[i] != NULL; i++)
    dump_context(&out, vec[i]);

  ecmascript_release_context_vector(vec);

  htsbuf_qprintf(&out, "\n");

  return http_send_reply(hc, 0,
                         "text/plain; charset=utf-8", NULL, NULL, 0, &out);
}


/**
 *
 */
static void
ecmascript_stats_init(void)
{
  http_path_add("/api/ecmascript/stats", NULL, dumpstats, 1);
}

INITME(INIT_GROUP_API, ecmascript_stats_init, NULL, 0);

#endif // ENABLE_HTTPSERVER
