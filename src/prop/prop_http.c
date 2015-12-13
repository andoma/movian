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
#include <stdio.h>
#include <assert.h>

#include "networking/http_server.h"
#include "prop_i.h"
#include "misc/str.h"


static prop_t *
prop_from_path(const char *path)
{
  char **n = strvec_split(path, '/');
  prop_t *p = prop_get_by_name((const char **)n, 1, NULL);
  strvec_free(n);
  return p;
}


static void
emit_str(htsbuf_queue_t *q, int html, const char *str)
{
  if(html)
    htsbuf_append_and_escape_xml(q, str);
  else
    htsbuf_qprintf(q, "%s", str);
}


static void
emit_value(htsbuf_queue_t *q, int html, prop_t *p)
{
  switch(p->hp_type) {
  case PROP_RSTRING:

    if(html && p->hp_rstrtype == PROP_STR_RICH) {
      htsbuf_qprintf(q, "%s", rstr_get(p->hp_rstring));
    } else {
      emit_str(q, html, rstr_get(p->hp_rstring));
    }
    break;
  case PROP_CSTRING:
    emit_str(q, html, p->hp_cstring);
    break;

  case PROP_URI:
    emit_str(q, html, rstr_get(p->hp_uri_title));
    htsbuf_qprintf(q, " ");
    emit_str(q, html, rstr_get(p->hp_uri));
    break;

  case PROP_FLOAT:
    htsbuf_qprintf(q, "%f", p->hp_float);
    break;
  case PROP_INT:
    htsbuf_qprintf(q, "%d", p->hp_int);
    break;
  case PROP_VOID:
    htsbuf_qprintf(q, "(void)");
    break;
  case PROP_ZOMBIE:
    htsbuf_qprintf(q, "(zombie)");
    break;
  case PROP_PROXY:
    htsbuf_qprintf(q, "(proxy)");
    break;
  case PROP_PROP:
    htsbuf_qprintf(q, "(prop)");
    break;
  case PROP_DIR:
    break;
  }
}


static int
hc_prop(http_connection_t *hc, const char *remain, void *opaque,
	http_cmd_t method)
{
  htsbuf_queue_t out;
  int rval;
  prop_t *p;
  const char *s;

  int html = 0;

  s = http_arg_get_hdr(hc, "accept");
  html = s != NULL && strstr(s, "text/html");


  if(remain == NULL) {
    return 404;
  } else {
    p = prop_from_path(remain);
    if(p == NULL)
      return 404;
  }

  const char *name = p->hp_name ? p->hp_name : "<unnamed>";

  htsbuf_queue_init(&out, 0);

  switch(method) {

  case HTTP_CMD_POST:
    if((s = http_arg_get_req(hc, "action")) != NULL) {
      event_t *e = event_create_action_str(s);
      prop_send_ext_event(p, e);
      event_release(e);
      rval = HTTP_STATUS_OK;
      break;
    }

    if((s = http_arg_get_req(hc, "debug")) != NULL) {
      hts_mutex_lock(&prop_mutex);
      if(!strcmp(s, "on")) {
        p->hp_flags |= PROP_DEBUG_THIS;
      } else {
        p->hp_flags &= ~PROP_DEBUG_THIS;
      }
      hts_mutex_unlock(&prop_mutex);
      rval = HTTP_STATUS_OK;
      break;
    }
    rval = 400;
    break;

  case HTTP_CMD_GET:

    if(html) {
      htsbuf_qprintf(&out, "<html><body>");
    }
    htsbuf_qprintf(&out, "%s (ref:%d xref:%d) is a ", name,
                   p->hp_refcount, p->hp_xref);

    hts_mutex_lock(&prop_mutex);

    if(p->hp_type == PROP_DIR) {
      prop_t *c;

      htsbuf_qprintf(&out, "directory\n");

      if(html)
        htsbuf_qprintf(&out, "<table border=1>\n");

      int cnt = 0;
      TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link) {

        char tmp[32];
        const char *cname = c->hp_name;
        const char *ref = c->hp_name;

        if(cname == NULL) {
          snprintf(tmp, sizeof(tmp), "*%d", cnt);
          ref = tmp;


          cname = "<unnamed>";

        }

        if(html) {

          htsbuf_qprintf(&out, "<tr>\n");

          htsbuf_qprintf(&out, "<td><a href=\"/api/prop/%s/%s\">",
                         remain, ref);
          htsbuf_append_and_escape_xml(&out, cname);
          htsbuf_qprintf(&out, "</a>\n");


          htsbuf_qprintf(&out, "<td>");
          if(c->hp_type == PROP_DIR) {
          htsbuf_qprintf(&out, "dir");

          } else {
            emit_value(&out, html, c);
          }
          htsbuf_qprintf(&out, "</tr>\n");

        } else {
          htsbuf_qprintf(&out, "  %s\n", cname);
        }
        cnt++;
      }

      if(html)
        htsbuf_qprintf(&out, "</table>\n");

    } else {

      emit_value(&out, html, p);
      htsbuf_append(&out, "\n", 1);
    }

#ifdef PROP_SUB_RECORD_SOURCE

    const char *br = html ? "<br>" : "\n";
    prop_sub_t *s;

    htsbuf_qprintf(&out, "Value Subscribers:%s", br);

    LIST_FOREACH(s, &p->hp_value_subscriptions, hps_value_prop_link)
      htsbuf_qprintf(&out, "%s:%d%s", s->hps_file, s->hps_line, br);

    htsbuf_qprintf(&out, "Canonical Subscribers:%s", br);

    LIST_FOREACH(s, &p->hp_canonical_subscriptions, hps_canonical_prop_link)
      htsbuf_qprintf(&out, "%s:%d%s", s->hps_file, s->hps_line, br);
#endif

    hts_mutex_unlock(&prop_mutex);

    rval = http_send_reply(hc, 0,
                           html ?
                           "text/html; charset=utf-8" :
                           "text/plain; charset=utf-8",
                           NULL, NULL, 0, &out);
    break;

  default:
    rval = HTTP_STATUS_METHOD_NOT_ALLOWED;
    break;
  }

  prop_ref_dec(p);

  return rval;
}


#ifdef PROP_DEBUG
/**
 *
 */
static int
hc_subtrack(http_connection_t *hc, const char *remain, void *opaque,
	   http_cmd_t method)
{
  if(remain == NULL)
    return 404;
  void *ptr = (void *)(intptr_t)strtol(remain, NULL, 16);
  prop_track_sub(ptr);
  return 200;
}
#endif



/**
 *
 */
static void
prop_http_init(void)
{
  http_path_add("/api/prop", NULL, hc_prop, 0);
#ifdef PROP_DEBUG
  http_path_add("/subtrack", NULL, hc_subtrack, 0);
#endif
}

INITME(INIT_GROUP_API, prop_http_init, NULL, 0);
