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
#include "prop/prop.h"
#include "ecmascript.h"
#include "htsmsg/htsmsg.h"
#include "video/video_playback.h"
#include "task.h"

typedef struct video_scrobble_aux {
  vpi_op_t op;
  struct htsmsg *info;
  struct prop *p;
  struct prop *origin;
} video_scrobble_aux_t;


/**
 *
 */
static int
video_scrobble_push_args(duk_context *duk, void *opaque)
{
  video_scrobble_aux_t *vsa = opaque;

  const char *op;
  switch(vsa->op) {
  case VPI_START: op = "start"; break;
  case VPI_STOP:  op = "stop"; break;
  default:
    return 0;
  }

  duk_push_string(duk, op);

  duk_push_object(duk);
  htsmsg_field_t *f;
  HTSMSG_FOREACH(f, vsa->info) {
    es_push_htsmsg_field(duk, f);
    duk_put_prop_string(duk, -2, f->hmf_name);
  }

  es_stprop_push(duk, vsa->p);
  es_stprop_push(duk, vsa->origin);
  return 4;
}


static void
scrobble_video_task(void *aux)
{
  video_scrobble_aux_t *vsa = aux;
  es_hook_invoke("videoscrobble", video_scrobble_push_args, vsa);
  htsmsg_release(vsa->info);
  prop_ref_dec(vsa->p);
  prop_ref_dec(vsa->origin);
  free(vsa);
}


/**
 *
 */
static void
es_scrobble_video(vpi_op_t op, struct htsmsg *info, struct prop *p,
                  struct prop *origin)
{
  video_scrobble_aux_t *vsa = malloc(sizeof(video_scrobble_aux_t));
  vsa->op = op;
  vsa->info = htsmsg_copy(info);
  vsa->p = prop_ref_inc(p);
  vsa->origin = prop_follow(origin);

  task_run(scrobble_video_task, vsa);
}

VPI_REGISTER(es_scrobble_video)
