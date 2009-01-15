/*
 *  GL Widgets, model loader
 *  Copyright (C) 2008 Andreas Öman
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

#include <unistd.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <alloca.h>

#include "glw.h"
#include "glw_model.h"

/**
 *
 */
static glw_t *
glw_model_error(glw_root_t *gr, errorinfo_t *ei, glw_t *parent)
{
  char buf[128];

  snprintf(buf, sizeof(buf), "%s:%d: Error: %s",
	   ei->file, ei->line, ei->error);
  fprintf(stderr, "%s\n", buf);

  return glw_create_i(gr,
		      GLW_LABEL,
		      GLW_ATTRIB_PARENT, parent,
		      GLW_ATTRIB_CAPTION, buf,
		      NULL);
}


/**
 *
 */
static glw_t *
glw_model_create2(glw_root_t *gr, token_t *sof, const char *src, glw_t *parent,
		  prop_t *prop, int flags)
{
  token_t *eof, *l;
  errorinfo_t ei;
  glw_t *r;
  glw_model_eval_context_t ec;

  if((l = glw_model_load1(gr, src, &ei, sof)) == NULL)
    return glw_model_error(gr, &ei, parent);

  eof = calloc(1, sizeof(token_t));
  eof->type = TOKEN_END;
  eof->file = refstr_create(src);
  l->next = eof;
  
  if(glw_model_preproc(gr, sof, &ei))
    return glw_model_error(gr, &ei, parent);

  if(glw_model_parse(sof, &ei))
    return glw_model_error(gr, &ei, parent);

  memset(&ec, 0, sizeof(ec));

  r = glw_create_i(gr,
		   GLW_MODEL,
		   GLW_ATTRIB_CAPTION, src,
		   GLW_ATTRIB_PARENT, parent,
		   NULL);
  ec.gr = gr;
  ec.w = r;
  ec.ei = &ei;
  ec.prop = prop;
  ec.sublist = &ec.w->glw_prop_subscriptions;

  if(glw_model_eval_block(sof, &ec)) {
    glw_destroy0(ec.w);
    return glw_model_error(gr, &ei, parent);
  }
  
  return r;
}
  

/**
 *
 */
glw_t *
glw_model_create(glw_root_t *gr, const char *src,
		 glw_t *parent, int flags, prop_t *prop)
{
  token_t *sof;
  glw_t *r;

  sof = calloc(1, sizeof(token_t));
  sof->type = TOKEN_START;
  sof->file = refstr_create(src);

  r = glw_model_create2(gr, sof, src, parent, prop, flags);

  glw_model_free_chain(sof);
  return r;
}


/**
 *
 */
static int
glw_model_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_t *c = TAILQ_FIRST(&w->glw_childs);

  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
  case GLW_SIGNAL_RENDER:
  case GLW_SIGNAL_EVENT:
    if(c != NULL)
      return glw_signal0(c, signal, extra);
    return 0;

  default:
    break;
  }
  return 0;
}

/**
 *
 */
void 
glw_model_ctor(glw_t *w, int init, va_list ap)
{
  if(init)
    glw_signal_handler_int(w, glw_model_callback);
}

