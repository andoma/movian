#include <assert.h>

#include "ecmascript.h"
#include "service.h"
#include "arch/threads.h"
#include "misc/regex.h"
#include "backend/backend.h"

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
static const es_resource_class_t es_resource_route = {
  .erc_name = "route",
  .erc_size = sizeof(es_route_t),
  .erc_destroy = es_route_destroy,
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
  if(hts_regcomp(&er->er_regex, str)) {
    hts_mutex_unlock(&route_mutex);
    free(er);
    duk_error(ctx, DUK_ERR_ERROR, "Invalid regular expression for route %s",
              str);
  }

  er->er_pattern = strdup(str);
  er->er_prio = strcspn(str, "()[].*?+$") ?: INT32_MAX;

  LIST_INSERT_SORTED(&routes, er, er_link, er_cmp, es_route_t);

  es_resource_init(&er->super, ec);

  es_resource_retain(&er->super);

  hts_mutex_unlock(&route_mutex);

  duk_push_pointer(ctx, er);
  return 1;
}


/**
 *
 */
static int
ecmascript_openuri(prop_t *page, const char *url, int sync)
{
  hts_regmatch_t matches[8];

  hts_mutex_lock(&route_mutex);

  es_route_t *er;

  LIST_FOREACH(er, &routes, er_link)
    if(!hts_regexec(&er->er_regex, url, 8, matches, 0))
      break;

  if(er == NULL) {
    hts_mutex_unlock(&route_mutex);
    return 1;
  }

  es_resource_retain(&er->super);

  es_context_t *ec = er->super.er_ctx;

  hts_mutex_unlock(&route_mutex);

  es_context_begin(ec);

  es_resource_release(&er->super);

  duk_context *ctx = ec->ec_duk;


  duk_push_global_object(ctx);

  duk_get_prop_string(ctx, -1, "routeInvoke");

  if(duk_is_function(ctx, -1)) {
    duk_push_pointer(ctx, er);
    duk_push_pointer(ctx, prop_ref_inc(page));

    int array_idx = duk_push_array(ctx);

    for(int i = 1; i < 8; i++) {
      if(matches[i].rm_so == -1)
        break;

      duk_push_lstring(ctx,
                       url + matches[i].rm_so,
                       matches[i].rm_eo - matches[i].rm_so);
      duk_put_prop_index(ctx, array_idx, i-1);
    }

    int rc = duk_pcall(ctx, 3);
    if(rc)
      es_dump_err(ctx);

    duk_pop(ctx);
  }
  duk_pop(ctx);
  es_context_end(ec);
  return 0;
}


/**
 *
 */
static void
ecmascript_search(struct prop *model, const char *query, prop_t *loading)
{
  es_context_t **ctxs = ecmascript_get_all_contexts();

  for(int i = 0; ctxs[i] != NULL; i++) {
    es_context_t *ec = ctxs[i];
    es_context_begin(ec);

    duk_context *ctx = ec->ec_duk;
    duk_push_global_object(ctx);
    duk_get_prop_string(ctx, -1, "searchInvoke");

    if(duk_is_function(ctx, -1)) {
      duk_push_pointer(ctx, prop_ref_inc(model));
      duk_push_string(ctx, query);
      duk_push_pointer(ctx, prop_ref_inc(loading));

      int rc = duk_pcall(ctx, 3);
      if(rc)
        es_dump_err(ctx);

      duk_pop(ctx);
    }
    duk_pop(ctx);

    es_context_end(ec);
  }

  ecmascript_release_context_vector(ctxs);
}


/**
 *
 */
static backend_t be_ecmascript = {
  .be_flags  = BACKEND_OPEN_CHECKS_URI,
  .be_open   = ecmascript_openuri,
  .be_search = ecmascript_search,
};

BE_REGISTER(ecmascript);


/**
 * Showtime object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_page[] = {
  { "routeCreate",             es_route_create,      1 },
  { NULL, NULL, 0}
};
