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

#include "htsmsg/htsmsg_xml.h"
#include "ecmascript.h"


ES_NATIVE_CLASS(htsmsg, &htsmsg_release);

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

  es_push_native_obj(ctx, &es_native_htsmsg, m);
  return 1;
}


/**
 *
 */
void
es_push_htsmsg_field(duk_context *ctx, const htsmsg_field_t *f)
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
  htsmsg_t *m = es_get_native_obj(ctx, 0, &es_native_htsmsg);
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
    es_push_native_obj(ctx, &es_native_htsmsg, htsmsg_retain(f->hmf_childs));
    duk_put_prop_string(ctx, res_idx, "msg");
  }

  es_push_htsmsg_field(ctx, f);
  duk_put_prop_string(ctx, res_idx, "value");
  return 1;
}


/**
 *
 */
static int
es_htsmsg_get_name_duk(duk_context *ctx)
{
  htsmsg_t *m = es_get_native_obj(ctx, 0, &es_native_htsmsg);
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
  htsmsg_t *m = es_get_native_obj(ctx, 0, &es_native_htsmsg);
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
  htsmsg_t *m = es_get_native_obj(ctx, 0, &es_native_htsmsg);
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
  htsmsg_t *m = es_get_native_obj(ctx, 0, &es_native_htsmsg);
  htsmsg_print(rstr_get(ec->ec_id), m);
  return 0;
}


static const duk_function_list_entry fnlist_htsmsg[] = {

  { "createFromXML",     es_htsmsg_create_from_xml_duk, 1 },
  { "get",               es_htsmsg_get_value_duk,       2 },
  { "enumerate",         es_htsmsg_enumerate_duk,       1 },
  { "length",            es_htsmsg_length_duk,          1 },
  { "getName",           es_htsmsg_get_name_duk,        2 },
  { "print",             es_htsmsg_print_duk,           1 },

  { NULL, NULL, 0}
};

ES_MODULE("htsmsg", fnlist_htsmsg);
