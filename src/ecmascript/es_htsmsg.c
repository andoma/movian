#include <assert.h>

#include "htsmsg/htsmsg_xml.h"
#include "ecmascript.h"


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

  es_push_native_obj(ctx, ES_NATIVE_HTSMSG, m);
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
  htsmsg_t *m = es_get_native_obj(ctx, 0, ES_NATIVE_HTSMSG);
  htsmsg_field_t *f;
  int want_attr = 0;

  if(duk_is_number(ctx, 1)) {
    int i = duk_require_int(ctx, 1);

    HTSMSG_FOREACH(f, m) {
      if(f->hmf_flags & HMF_XML_ATTRIBUTE)
        continue;
      if(!i--)
        break;
    }

    if(f == NULL)
      return 0;

  } else {

    const char *str = duk_safe_to_string(ctx, 1);
    if(*str == '@') {
      want_attr = 1;
      str++;
    }

    f = htsmsg_field_find(m, str);

    if(f == NULL)
      return 0;

    if(want_attr) {
      if(!(f->hmf_flags & HMF_XML_ATTRIBUTE))
        return 0;
    } else {
      if(f->hmf_flags & HMF_XML_ATTRIBUTE)
        return 0;
    }
  }


  int res_idx = duk_push_object(ctx);

  if(f->hmf_childs != NULL) {
    es_push_native_obj(ctx, ES_NATIVE_HTSMSG, htsmsg_retain(f->hmf_childs));
    duk_put_prop_string(ctx, res_idx, "msg");
  }

  push_htsmsg_field(ctx, f);
  duk_put_prop_string(ctx, res_idx, "value");
  return 1;
}


/**
 *
 */
static int
es_htsmsg_get_name_duk(duk_context *ctx)
{
  htsmsg_t *m = es_get_native_obj(ctx, 0, ES_NATIVE_HTSMSG);
  htsmsg_field_t *f;
  int i = duk_require_int(ctx, 1);

  HTSMSG_FOREACH(f, m) {
    if(f->hmf_flags & HMF_XML_ATTRIBUTE)
      continue;
    if(!i--)
      break;
  }

  if(f == NULL || f->hmf_name == NULL)
    return 0;
  duk_push_string(ctx, f->hmf_name);
  return 1;
}


/**
 *
 */
static int
es_htsmsg_enumerate_duk(duk_context *ctx)
{
  htsmsg_t *m = es_get_native_obj(ctx, 0, ES_NATIVE_HTSMSG);
  htsmsg_field_t *f;
  int idx = 0;

  duk_push_array(ctx);

  HTSMSG_FOREACH(f, m) {
    if(f->hmf_flags & HMF_XML_ATTRIBUTE)
      continue;

    if(f->hmf_name == NULL)
      continue;

    duk_push_string(ctx, f->hmf_name);
    duk_put_prop_index(ctx, -2, idx++);
  }
  return 1;
}


/**
 *
 */
static int
es_htsmsg_length_duk(duk_context *ctx)
{
  htsmsg_t *m = es_get_native_obj(ctx, 0, ES_NATIVE_HTSMSG);
  htsmsg_field_t *f;
  unsigned int cnt = 0;

  HTSMSG_FOREACH(f, m) {
    if(f->hmf_flags & HMF_XML_ATTRIBUTE)
      continue;
    cnt++;
  }
  duk_push_uint(ctx, cnt);
  return 1;
}


/**
 *
 */
static int
es_htsmsg_print_duk(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);
  htsmsg_t *m = es_get_native_obj(ctx, 0, ES_NATIVE_HTSMSG);
  htsmsg_print(ec->ec_id, m);
  return 0;
}


/**
 * Showtime object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_htsmsg[] = {

  { "htsmsgCreateFromXML",     es_htsmsg_create_from_xml_duk, 1 },
  { "htsmsgGet",               es_htsmsg_get_value_duk,       2 },
  { "htsmsgEnumerate",         es_htsmsg_enumerate_duk,       1 },
  { "htsmsgLength",            es_htsmsg_length_duk,          1 },
  { "htsmsgGetName",           es_htsmsg_get_name_duk,        2 },
  { "htsmsgPrint",             es_htsmsg_print_duk,           1 },

  { NULL, NULL, 0}
};
