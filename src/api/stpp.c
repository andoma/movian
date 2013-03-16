/*
 *  STPP - Showtime Property Protocol
 *  Copyright (C) 2013 Andreas Öman
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
 */

#include <stdio.h>
#include <assert.h>

#include "networking/http_server.h"
#include "htsmsg/htsmsg_json.h"
#include "misc/str.h"
#include "prop/prop.h"

#define STPP_CMD_SET 3

/**
 *
 */
static int
stpp_init(http_connection_t *hc)
{
  if(!gconf.enable_experimental)
    return 403;

  return 0;
}

/**
 *
 */
static void
stpp_cmd_set(int propref, const char *path, htsmsg_field_t *v)
{
  if(path == NULL || v == NULL)
    return;

  prop_t *p = prop_get_global();
  switch(v->hmf_type) {
  case HMF_S64:
    prop_setdn(NULL, p, path, PROP_SET_INT, v->hmf_s64);
    break;
  case HMF_STR:
    prop_setdn(NULL, p, path, PROP_SET_STRING, v->hmf_str);
    break;
  case HMF_DBL:
    prop_setdn(NULL, p, path, PROP_SET_FLOAT, v->hmf_dbl);
    break;
  }
}


/**
 *
 */
static void
stpp_json(htsmsg_t *m)
{
  htsmsg_print(m);
  int cmd = htsmsg_get_u32_or_default(m, HTSMSG_INDEX(0), 0);
  
  switch(cmd) {
  case STPP_CMD_SET:
    stpp_cmd_set(htsmsg_get_u32_or_default(m, HTSMSG_INDEX(1), 0),
		 htsmsg_get_str(m, HTSMSG_INDEX(2)),
		 htsmsg_field_find(m, HTSMSG_INDEX(3)));
    break;
  }
}

/**
 *
 */
static int
stpp_input(http_connection_t *hc, int opcode, 
	   uint8_t *data, size_t len, void *opaque)
{
  if(opcode != 1)
    return 0;

  htsmsg_t *m = htsmsg_json_deserialize((const char *)data);
  if(m != NULL) {
    stpp_json(m);
    htsmsg_destroy(m);
  }
  return 0;
}


/**
 *
 */
static void
stpp_fini(http_connection_t *hc, void *opaque)
{
}


/**
 *
 */
static void
ws_init(void)
{
  http_add_websocket("/showtime/stpp", stpp_init, stpp_input, stpp_fini);
}


INITME(INIT_GROUP_API, ws_init);
