/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#include "showtime.h"
#include "prop_gvariant.h"


/**
 *
 */
static void
fixup_title(char *k)
{
  while(*k) {
    if(*k >= 'A' && *k <= 'Z')
      *k += 32;
    else if (*k == '.')
      *k = '_';
    k++;
  }
}


/**
 *
 */
void
prop_set_from_gvariant(GVariant *v, prop_t *p)
{
  const GVariantType *T = g_variant_get_type(v);

  if(g_variant_type_equal(T, G_VARIANT_TYPE_BOOLEAN)) {
    prop_set_int(p, g_variant_get_boolean(v));
  } else if(g_variant_type_equal(T, G_VARIANT_TYPE_BYTE)) {
    prop_set_int(p, g_variant_get_byte(v));
  } else if(g_variant_type_equal(T, G_VARIANT_TYPE_INT16)) {
    prop_set_int(p, g_variant_get_int16(v));
  } else if(g_variant_type_equal(T, G_VARIANT_TYPE_UINT16)) {
    prop_set_int(p, g_variant_get_uint16(v));
  } else if(g_variant_type_equal(T, G_VARIANT_TYPE_INT32)) {
    prop_set_int(p, g_variant_get_int32(v));
  } else if(g_variant_type_equal(T, G_VARIANT_TYPE_UINT32)) {
    prop_set_int(p, g_variant_get_uint32(v));
  } else if(g_variant_type_equal(T, G_VARIANT_TYPE_INT64)) {
    int64_t val = g_variant_get_int64(v);
    if(val <= INT_MAX)
      prop_set_int(p, val);
    else
      prop_set_float(p, val);

  } else if(g_variant_type_equal(T, G_VARIANT_TYPE_UINT64)) {
    uint64_t val = g_variant_get_uint64(v);
    if(val <= INT_MAX)
      prop_set_int(p, val);
    else
      prop_set_float(p, val);
  } else if(g_variant_type_equal(T, G_VARIANT_TYPE_STRING) ||
	    g_variant_type_equal(T, G_VARIANT_TYPE_OBJECT_PATH) ||
	    g_variant_type_equal(T, G_VARIANT_TYPE_SIGNATURE)) {
    const gchar *val = g_variant_get_string(v, NULL);
    prop_set_string(p, val);
  } else if(g_variant_type_equal(T, G_VARIANT_TYPE_VARDICT)) {
    prop_void_childs(p);
    prop_set_from_vardict(v, p);
  } else if(g_variant_type_is_array(T)) {
    int num = g_variant_n_children(v);
    prop_destroy_childs(p);
    for(int i = 0; i < num; i++) {
      prop_set_from_gvariant(g_variant_get_child_value(v, i),
			     prop_create(p, NULL));
    }
  } else {
    fprintf(stderr, 
	    "%s(): can't deal with type %s\n",
	   __FUNCTION__, g_variant_get_type_string(v));
  }
}




/**
 *
 */
void
prop_set_from_vardict(GVariant *v, prop_t *parent)
{
  GVariantIter iter;
  GVariant *value;
  gchar *key;

  g_variant_iter_init(&iter, v);
  while(g_variant_iter_loop(&iter, "{sv}", &key, &value)) {
    char *k = mystrdupa(key);
    fixup_title(k);
    prop_set_from_gvariant(value, prop_create(parent, k));
  }
}


/**
 *
 */
void
prop_set_from_tuple(GVariant *v, prop_t *parent)
{
  const char *key =
    g_variant_get_string(g_variant_get_child_value(v, 0), NULL);
  GVariant *value = g_variant_get_variant(g_variant_get_child_value(v, 1));

  char *k = mystrdupa(key);
  fixup_title(k);
  prop_set_from_gvariant(value, prop_create(parent, k));
}

