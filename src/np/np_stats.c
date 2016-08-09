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

#include "np.h"

#if ENABLE_HTTPSERVER


typedef struct heap_walker_aux {
  htsbuf_queue_t *out;
  uint32_t inuse;

} heap_walker_aux_t;


static void
heap_printer(void *opaque, uint32_t addr, uint32_t size, int inuse)
{
  heap_walker_aux_t *aux = opaque;
  htsbuf_qprintf(aux->out, "    0x%08x + 0x%08x %s\n",
                 addr, size, inuse ? "Used" : "Free");
  if(inuse)
    aux->inuse += size;
}



static void
fd_printer(void *opaque, int fd, int type)
{
  htsbuf_qprintf(opaque, "   fd: %4d  type: %d\n", fd, type);
}


/**
 *
 */
static void
dump_context(htsbuf_queue_t *out, np_context_t *np)
{
  np_lock(np);

  htsbuf_qprintf(out, "\n--- %s ------------------------\n",
                 np->np_path);

  htsbuf_qprintf(out, "  Loaded from %s\n", np->np_path);
  htsbuf_qprintf(out, "\n  Heap\n");

  heap_walker_aux_t heap = {out};
  vmir_walk_heap(np->np_unit, heap_printer, &heap);
  htsbuf_qprintf(out, "  Heap total inuse: %d bytes\n", heap.inuse);

  htsbuf_qprintf(out, "\n  Open descriptors\n");

  vmir_walk_fds(np->np_unit, fd_printer, out);

  const vmir_stats_t *s = vmir_get_stats(np->np_unit);

  htsbuf_qprintf(out, "\n");
  htsbuf_qprintf(out, "  Memory usage stats\n");
  htsbuf_qprintf(out, "         VM code size: %d\n", s->vm_code_size);
  htsbuf_qprintf(out, "        JIT code size: %d\n", s->jit_code_size);
  htsbuf_qprintf(out, "            Data size: %d\n", s->data_size);
  htsbuf_qprintf(out, "       Peak heap size: %d\n", s->peak_heap_size);
  htsbuf_qprintf(out, "     Peak stack usage: %d\n", s->peak_stack_size);
  htsbuf_qprintf(out, "\n");
  htsbuf_qprintf(out, "  Code transformation stats\n");
  htsbuf_qprintf(out, "         Moves killed: %d\n", s->moves_killed);
  htsbuf_qprintf(out, "    Lea+Load combined: %d\n", s->lea_load_combined);
  htsbuf_qprintf(out, "   Lea+Load comb-fail: %d\n", s->lea_load_combined_failed);
  htsbuf_qprintf(out, "  Cmp+Branch combined: %d\n", s->cmp_branch_combine);
  htsbuf_qprintf(out, "  Cmp+Select combined: %d\n", s->cmp_select_combine);
  htsbuf_qprintf(out, "     Mul+Add combined: %d\n", s->mla_combine);
  htsbuf_qprintf(out, "   Load+Cast combined: %d\n", s->load_cast_combine);
  htsbuf_qprintf(out, "\n");

  np_unlock(np);
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

  np_context_t **vec = np_get_all_contexts();

  for(i = 0; vec[i] != NULL; i++)
    dump_context(&out, vec[i]);

  np_context_vec_free(vec);

  htsbuf_qprintf(&out, "\n");

  return http_send_reply(hc, 0,
                         "text/plain; charset=utf-8", NULL, NULL, 0, &out);
}




/**
 *
 */
static void
np_stats_init(void)
{
  http_path_add("/api/np/stats", NULL, dumpstats, 1);
}

INITME(INIT_GROUP_API, np_stats_init, NULL, 0);

#endif // ENABLE_HTTPSERVER
