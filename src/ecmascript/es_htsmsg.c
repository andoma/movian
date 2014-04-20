#include <assert.h>

#include "htsmsg/htsmsg_xml.h"
#include "ecmascript.h"

/**
 *
 */
static int
es_htsmsg_release_duk(duk_context *ctx)
{
  htsmsg_t *m = duk_require_pointer(ctx, 0);
  htsmsg_release(m);
  return 0;
}


/**
 *
 */
static int
es_htsmsg_create_from_xml_duk(duk_context *ctx)
{
  char errbuf[256];
  const char *xml = duk_safe_to_string(ctx, 0);

  htsmsg_t *m = htsmsg_xml_deserialize_cstr(xml, errbuf, sizeof(errbuf));
  if(m == NULL)
    duk_error(ctx, DUK_ERR_ERROR, "Malformed XML -- %s", errbuf);

  duk_push_pointer(ctx, m);
  return 1;
}

/**
 *
 */
static void
push_htsmsg_field(duk_context *ctx, const htsmsg_field_t *f)
{
  switch(f->hmf_type) {
  case HMF_STR:
    duk_push_string(ctx, f->hmf_str);
    break;
  case HMF_S64:
    duk_push_number(ctx, f->hmf_s64);
    break;
  case HMF_DBL:
    duk_push_number(ctx, f->hmf_dbl);
    break;
  default:
    duk_push_undefined(ctx);
    break;
  }
}

/**
 *
 */
static int
es_htsmsg_get_value_duk(duk_context *ctx)
{
  htsmsg_t *m = duk_require_pointer(ctx, 0);

  htsmsg_field_t *f;

  if(duk_is_number(ctx, 1)) {
    int i = duk_require_int(ctx, 1);

    HTSMSG_FOREACH(f, m) {
      if(!--i)
        break;
    }
  } else {
    const char *str = duk_safe_to_string(ctx, 1);
    f = htsmsg_field_find(m, str);
  }

  if(f == NULL)
    return 0;

 if(f->hmf_childs == NULL) {
   push_htsmsg_field(ctx, f);
   return 1;
 }

 int res_idx = duk_push_object(ctx);
 duk_push_pointer(ctx, f->hmf_childs);
 duk_put_prop_string(ctx, res_idx, "msg");

 push_htsmsg_field(ctx, f);
 duk_put_prop_string(ctx, res_idx, "value");
 return 1;
}


/**
 * Showtime object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_htsmsg[] = {

  { "htsmsgRelease",           es_htsmsg_release_duk,         1 },
  { "htsmsgCreateFromXML",     es_htsmsg_create_from_xml_duk, 1 },
  { "htsmsgGet",               es_htsmsg_get_value_duk,       2 },

  { NULL, NULL, 0}
};
