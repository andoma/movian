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
#include <assert.h>

#include "misc/rstr.h"

#include "ecmascript.h"
#include "metadata/metadata.h"
#include "metadata/metadata_str.h"
#include "metadata/playinfo.h"
#include "prop/prop.h"


typedef struct es_mlv {
  es_resource_t super;
  metadata_lazy_video_t *mlv;
} es_mlv_t;




/**
 *
 */
static void
es_mlv_destroy(es_resource_t *eres)
{
  es_mlv_t *em = (es_mlv_t *)eres;
  mlv_unbind(em->mlv, 0);
  es_resource_unlink(eres);
}



/**
 *
 */
static const es_resource_class_t es_resource_mlv = {
  .erc_name = "mlv",
  .erc_size = sizeof(es_mlv_t),
  .erc_destroy = es_mlv_destroy,
};


/**
 *
 */
static int
es_video_metadata_bind_duk(duk_context *ctx)
{
  prop_t *root = es_stprop_get(ctx, 0);
  const char *urlstr = duk_safe_to_string(ctx, 1);
  es_context_t *ec = es_get(ctx);
  es_mlv_t *em = es_resource_create(ec, &es_resource_mlv, 0);

  rstr_t *url = rstr_alloc(urlstr);
  rstr_t *title;
  rstr_t *filename = es_prop_to_rstr(ctx, 2, "filename");
  int year         = es_prop_to_int(ctx, 2, "year", -1);

  if(filename != NULL) {
    // Raw filename case
    title = metadata_remove_postfix_rstr(filename);
    rstr_release(filename);
  } else {
    title = es_prop_to_rstr(ctx, 2, "title");
  }

  int season    = es_prop_to_int(ctx,  2, "season", -1);
  int episode   = es_prop_to_int(ctx,  2, "episode", -1);
  rstr_t *imdb  = es_prop_to_rstr(ctx, 2, "imdb");
  int duration  = es_prop_to_int(ctx,  2, "duration", -1);

  em->mlv =
    metadata_bind_video_info(url, title, imdb, duration, root, NULL, 0, 0,
                             year, season, episode, 0, ec->ec_id);
  rstr_release(title);
  rstr_release(url);
  es_resource_push(ctx, &em->super);
  return 1;
}




/**
 *
 */
static int
es_bind_play_info(duk_context *ctx)
{
  struct prop *p = es_stprop_get(ctx, 0);
  const char *url = duk_to_string(ctx, 1);
  playinfo_bind_url_to_prop(url, p);
  return 0;
}


/**
 * Showtime object exposed functions
 */
static const duk_function_list_entry fnlist_metadata[] = {
  { "videoMetadataBind",     es_video_metadata_bind_duk,       3 },
  { "bindPlayInfo",          es_bind_play_info, 2},
  { NULL, NULL, 0}
};

ES_MODULE("metadata", fnlist_metadata);
