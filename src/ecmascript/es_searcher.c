#include "prop/prop.h"
#include "ecmascript.h"


typedef struct searcher_aux {
  prop_t *model;
  const char *query;
  prop_t *loading;
} searcher_aux_t;


/**
 *
 */
static int
searcher_push_args(duk_context *duk, void *opaque)
{
  searcher_aux_t *sa = opaque;
  es_stprop_push(duk, sa->model);
  duk_push_string(duk, sa->query);
  es_stprop_push(duk, sa->loading);
  return 3;
}


/**
 *
 */
void
ecmascript_search(struct prop *model, const char *query, prop_t *loading)
{
  searcher_aux_t sa = { model, query, loading };
  es_hook_invoke("searcher", searcher_push_args, &sa);
}
