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
#include <sys/types.h>
#include <assert.h>
#include <string.h>

#include "ecmascript.h"
#include "subtitles/subtitles.h"
#include "prop/prop.h"
#include "misc/str.h"
#include "media/media_track.h"
#include "i18n.h"
#include "usage.h"

typedef struct es_sp {
  es_resource_t super;

  char *id;
  subtitle_provider_t sp;
  prop_t *title;
} es_sp_t;




/**
 *
 */
static void
es_sp_destroy(es_resource_t *eres)
{
  es_sp_t *esp = (es_sp_t *)eres;

  es_root_unregister(eres->er_ctx->ec_duk, eres);

  subtitle_provider_unregister(&esp->sp);
  prop_destroy(esp->title);
  free(esp->id);

  es_resource_unlink(eres);
}


/**
 *
 */
static void
es_sp_info(es_resource_t *eres, char *dst, size_t dstsize)
{
  es_sp_t *esp = (es_sp_t *)eres;
  snprintf(dst, dstsize, "subtitleprovider %s", esp->id);
}



/**
 *
 */
static const es_resource_class_t es_resource_sp = {
  .erc_name = "subtitleprovider",
  .erc_size = sizeof(es_sp_t),
  .erc_destroy = es_sp_destroy,
  .erc_info = es_sp_info,
};


/**
 *
 */
static void
esp_retain(subtitle_provider_t *sp)
{
  es_sp_t *esp = sp->sp_opaque;
  es_resource_retain(&esp->super);
}


static void
es_set_rstr(duk_context *ctx, int obj_idx, const char *key, rstr_t *val)
{
  if(val == NULL)
    return;

  obj_idx = duk_normalize_index(ctx, obj_idx);
  duk_push_string(ctx, rstr_get(val));
  duk_put_prop_string(ctx, obj_idx, key);
}


static void
es_set_str(duk_context *ctx, int obj_idx, const char *key, const char *val)
{
  if(val == NULL)
    return;

  obj_idx = duk_normalize_index(ctx, obj_idx);
  duk_push_string(ctx, val);
  duk_put_prop_string(ctx, obj_idx, key);
}


static void
es_set_int(duk_context *ctx, int obj_idx, const char *key, int val)
{
  obj_idx = duk_normalize_index(ctx, obj_idx);
  duk_push_int(ctx, val);
  duk_put_prop_string(ctx, obj_idx, key);
}


static void
es_set_double(duk_context *ctx, int obj_idx, const char *key, double val)
{
  obj_idx = duk_normalize_index(ctx, obj_idx);
  duk_push_number(ctx, val);
  duk_put_prop_string(ctx, obj_idx, key);
}


/**
 *
 */
static void
esp_query(subtitle_provider_t *SP, sub_scanner_t *ss, int score,
          int autosel)
{
  es_sp_t *esp = SP->sp_opaque;
  es_context_t *ec = esp->super.er_ctx;

  duk_context *ctx = es_context_begin(ec);

  if(ctx != NULL && ss != NULL) {

    extern ecmascript_native_class_t es_native_prop;

    usage_event("Subtitle search", 1,
                USAGE_SEG("responder", rstr_get(ec->ec_id)));

    es_push_root(ctx, esp);

    es_push_native_obj(ctx, &es_native_prop, prop_ref_inc(ss->ss_proproot));

    duk_push_object(ctx);

    es_set_rstr(ctx, -1, "title", ss->ss_title);
    es_set_rstr(ctx, -1, "imdb",  ss->ss_imdbid);

    if(ss->ss_season > 0)
      es_set_int(ctx, -1, "season", ss->ss_season);

    if(ss->ss_year > 0)
      es_set_int(ctx, -1, "year", ss->ss_year);

    if(ss->ss_episode > 0)
      es_set_int(ctx, -1, "episode", ss->ss_episode);

    if(ss->ss_fsize > 0)
      es_set_double(ctx, -1, "filesize", ss->ss_fsize);

    if(ss->ss_duration > 0)
      es_set_int(ctx, -1, "duration", ss->ss_duration);

    if(ss->ss_hash_valid) {
      char str[64];
      snprintf(str, sizeof(str), "%016" PRIx64, ss->ss_opensub_hash);
      es_set_str(ctx, -1, "opensubhash", str);

      bin2hex(str, sizeof(str), ss->ss_subdbhash, 16);
      es_set_str(ctx, -1, "subdbhash", str);
    }

    duk_push_int(ctx, score);
    duk_push_boolean(ctx, autosel);

    int rc = duk_pcall(ctx, 4);
    if(rc)
      es_dump_err(ctx);

    duk_pop(ctx);

  }
  es_resource_release(&esp->super);
  es_context_end(ec, 1, ctx);
}


/**
 *
 */
static int
es_subtitleprovideradd(duk_context *ctx)
{
  const char *id = duk_to_string(ctx, 1);
  const char *title = duk_to_string(ctx, 2);

  es_context_t *ec = es_get(ctx);
  es_sp_t *esp = es_resource_create(ec, &es_resource_sp, 1);
  esp->id = strdup(id);
  es_root_register(ctx, 0, esp); // Register callback function

  esp->sp.sp_query  = esp_query;
  esp->sp.sp_retain = esp_retain;

  esp->sp.sp_opaque = esp;

  esp->title = prop_create_root(NULL);
  prop_set_string(esp->title, title);

  subtitle_provider_register(&esp->sp, id, esp->title, 0, "plugin", 1, 1);
  es_resource_push(ctx, &esp->super);
  return 1;
}


/**
 *
 */
static int
es_subtitleadditem(duk_context *ctx)
{
  prop_t *proproot = es_stprop_get(ctx, 0);

  const char *url      = duk_to_string(ctx, 1);
  const char *title    = duk_to_string(ctx, 2);
  const char *language = duk_get_string(ctx, 3);
  const char *format   = duk_get_string(ctx, 4);
  const char *source   = duk_get_string(ctx, 5);
  int score            = duk_get_number(ctx, 6);
  int autosel          = duk_get_boolean(ctx, 7);

  mp_add_track(proproot,
	       title, url, format, NULL, language, source, NULL,
               score, autosel);
  return 0;
}


/**
 *
 */
static int
es_getsubtitlelanguages(duk_context *ctx)
{
  int i, idx = 0;
  duk_push_array(ctx);

  for(i = 0; i < 3; i++) {
    const char *lang = i18n_subtitle_lang(i);
    if(lang) {
      duk_push_string(ctx, lang);
      duk_put_prop_index(ctx, -2, idx++);
    }
  }
  return 1;
}


/**
 * Showtime object exposed functions
 */
static const duk_function_list_entry fnlist_subtitle[] = {
  { "addProvider",     es_subtitleprovideradd,       3 },
  { "addItem",         es_subtitleadditem,           8 },
  { "getLanguages",    es_getsubtitlelanguages,      0 },
  { NULL, NULL, 0}
};

ES_MODULE("subtitle", fnlist_subtitle);
