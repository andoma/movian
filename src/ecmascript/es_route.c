/*
 *  Copyright (C) 2007-2018 Lonelycoder AB
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

#include "ecmascript.h"
#include "service.h"
#include "arch/threads.h"
#include "misc/regex.h"
#include "navigator.h"
#include "backend/backend.h"
#include "usage.h"

LIST_HEAD(es_route_list, es_route);

typedef struct es_route {
  es_resource_t super;
  LIST_ENTRY(es_route) er_link;
  char *er_pattern;
  hts_regex_t er_regex;
  int er_prio;
} es_route_t;


static struct es_route_list routes;

static HTS_MUTEX_DECL(route_mutex);


/**
 *
 */
static void
es_route_destroy(es_resource_t *eres)
{
  es_route_t *er = (es_route_t *)eres;

  es_debug(eres->er_ctx, "Route %s removed", er->er_pattern);

  es_root_unregister(eres->er_ctx->ec_duk, eres);

  hts_mutex_lock(&route_mutex);
  LIST_REMOVE(er, er_link);
  hts_mutex_unlock(&route_mutex);

  free(er->er_pattern);
  hts_regfree(&er->er_regex);

  es_resource_unlink(&er->super);
}


/**
 *
 */
static void
es_route_info(es_resource_t *eres, char *dst, size_t dstsize)
{
  es_route_t *er = (es_route_t *)eres;
  snprintf(dst, dstsize, "%s (prio:%d)", er->er_pattern, er->er_prio);
}


/**
 *
 */
static const es_resource_class_t es_resource_route = {
  .erc_name = "route",
  .erc_size = sizeof(es_route_t),
  .erc_destroy = es_route_destroy,
  .erc_info = es_route_info,
};


/**
 *
 */
static int
er_cmp(const es_route_t *a, const es_route_t *b)
{
  return b->er_prio - a->er_prio;
}


/**
 *
 */
static int
es_route_create(duk_context *ctx)
{
  const char *str = duk_safe_to_string(ctx, 0);

  if(str[0] != '^') {
    int l = strlen(str);
    char *s = alloca(l + 2);
    s[0] = '^';
    memcpy(s+1, str, l+1);
    str = s;
  }

  es_context_t *ec = es_get(ctx);

  hts_mutex_lock(&route_mutex);

  es_route_t *er;

  LIST_FOREACH(er, &routes, er_link)
    if(!strcmp(er->er_pattern, str))
      break;

  if(er != NULL) {
    hts_mutex_unlock(&route_mutex);
    duk_error(ctx, DUK_ERR_ERROR, "Route %s already exist", str);
  }

  er = es_resource_alloc(&es_resource_route);
  const char *errmsg;
  if(hts_regcomp(&er->er_regex, str, &errmsg)) {
    hts_mutex_unlock(&route_mutex);
    free(er);
    duk_error(ctx, DUK_ERR_ERROR,
              "Invalid regular expression for route %s -- %s", str, errmsg);
  }

  er->er_pattern = strdup(str);

  es_debug(ec, "Route %s added", er->er_pattern);

  er->er_prio = strcspn(str, "()[]*?+$") ?: INT32_MAX;

  LIST_INSERT_SORTED(&routes, er, er_link, er_cmp, es_route_t);

  es_resource_link(&er->super, ec, 1);

  hts_mutex_unlock(&route_mutex);

  es_root_register(ctx, 1, er);

  es_resource_push(ctx, &er->super);
  return 1;
}


/**
 *
 */
static int
es_route_test(duk_context *ctx)
{
  const char *str = duk_safe_to_string(ctx, 0);

  hts_mutex_lock(&route_mutex);

  es_route_t *er;
  hts_regmatch_t matches[8];

  LIST_FOREACH(er, &routes, er_link)
    if(!hts_regexec(&er->er_regex, str, 8, matches))
      break;

  duk_push_boolean(ctx, er != NULL);

  hts_mutex_unlock(&route_mutex);

  return 1;
}


/**
 *
 */
int
ecmascript_openuri(prop_t *page, const char *url, int sync)
{
  hts_regmatch_t matches[8];

  hts_mutex_lock(&route_mutex);

  es_route_t *er;

  LIST_FOREACH(er, &routes, er_link)
    if(!hts_regexec(&er->er_regex, url, 8, matches))
      break;

  if(er == NULL) {
    hts_mutex_unlock(&route_mutex);
    return 1;
  }

  es_resource_retain(&er->super);

  es_context_t *ec = er->super.er_ctx;

  hts_mutex_unlock(&route_mutex);

  duk_context *ctx = es_context_begin(ec);

  if(ctx == NULL) {
    es_context_end(ec, 1, ctx);
    es_resource_release(&er->super);
    return 1;
  }

  es_push_root(ctx, er);

  es_stprop_push(ctx, page);

  duk_push_boolean(ctx, sync);

  int array_idx = duk_push_array(ctx);

  es_debug(ec, "Opening route %s", er->er_pattern);
  usage_page_open(sync, rstr_get(ec->ec_id));

  for(int i = 1; i < 8; i++) {
    if(matches[i].rm_so == -1)
      break;

    es_debug(ec, "  Page argument %d : %.*s", i,
             matches[i].rm_eo - matches[i].rm_so, url + matches[i].rm_so);

    duk_push_lstring(ctx,
                     url + matches[i].rm_so,
                     matches[i].rm_eo - matches[i].rm_so);
    duk_put_prop_index(ctx, array_idx, i-1);
  }

  int rc = duk_pcall(ctx, 3);
  if(rc) {
    if(duk_is_string(ctx, -1)) {
      nav_open_error(page, duk_to_string(ctx, -1));
    } else {
      duk_get_prop_string(ctx, -1, "message");
      nav_open_error(page, duk_to_string(ctx, -1));
      duk_pop(ctx);
    }
    es_dump_err(ctx);
  }
  duk_pop(ctx);

  es_context_end(ec, 1, ctx);
  es_resource_release(&er->super);

  return 0;
}


/**
 *
 */
static int
es_backend_open(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  const char *url = duk_require_string(ctx, 1);
  int sync = duk_require_boolean(ctx, 2);
  if(backend_open(p, url, sync))
    duk_error(ctx, DUK_ERR_ERROR, "No handler for URL");
  return 0;
}


static const duk_function_list_entry fnlist_route[] = {
  { "create",                  es_route_create,      2 },
  { "backendOpen",             es_backend_open,      3 },
  { "test",                    es_route_test,        1 },
  { NULL, NULL, 0}
};

ES_MODULE("route", fnlist_route);
