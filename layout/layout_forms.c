/*
 *  Layout engine
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

#include <string.h>
#include <math.h>
#include <unistd.h>

#include <GL/glu.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include "layout.h"
#include "layout_forms.h"

static int layout_form_callback(glw_t *w, void *opaque,
				glw_signal_t signal, ...);

static int layout_form_entry_free(glw_t *w, void *opaque,
				  glw_signal_t signal, ...);

static int layout_form_entry_string(glw_t *w, void *opaque,
				    glw_signal_t signal, ...);

static int layout_form_entry_integer(glw_t *w, void *opaque,
				     glw_signal_t signal, ...);

static int layout_form_entry_list(glw_t *w, void *opaque,
				  glw_signal_t signal, ...);

static int layout_form_entry_button(glw_t *w, void *opaque,
				    glw_signal_t signal, ...);

static int layout_form_entry_child_mon(glw_t *w, void *opaque,
				       glw_signal_t signal, ...);

static int layout_form_list_option(glw_t *w, void *opaque,
				   glw_signal_t signal, ...);


/**
 * Find a target we can move to
 *
 * It must have a layout_form_callback attached to it so we
 * can 'escape' from it
 */

static glw_t *
find_anything_with_id(glw_t *w, glw_class_t skip_class, int skip)
{
  glw_t *c, *r, *best;

  if(w == NULL)
    return w;

  if(glw_get_opaque(w, layout_form_callback) != NULL) {
    /* Candidate for selection */

    switch(w->glw_class) {
    case GLW_LIST:
      /* We never want to switch to a list with no childs in it.
	 There is nothing the user can do there, so skip over it */
      if(TAILQ_FIRST(&w->glw_childs) != NULL)
	return w;
      break;
    default:
      return w;
    }
  }

  if(w->glw_class == skip_class) {
    best = NULL;
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
      if((r = find_anything_with_id(c, 0, 0)) != NULL) {
	best = r;
	if(skip == 0)
	  return r;
	skip--;
      }
    }
    if(best != NULL)
      return best;
  } else {
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
      if(w->glw_class == GLW_DECK && c != w->glw_selected)
	continue;
      if((r = find_anything_with_id(c, skip_class, skip)) != NULL)
	return r;
    }
  }

  return NULL;
}




/**
 *
 */
static glw_t *
layout_form_find(glw_t *w, int rev, glw_class_t cont_class, 
		 glw_class_t skip_class, int skip)
{
  glw_t *c, *p, *s;
  
  while(1) {
    p = w->glw_parent;
    if(p == NULL)
      return NULL;
    if(p->glw_class == cont_class) {
      s = w;
      while(1) {
	if(rev)
	  s = TAILQ_PREV(s, glw_queue, glw_parent_link);
	else
	  s = TAILQ_NEXT(s, glw_parent_link);

	if(s == NULL)
	  break;

	if((c = find_anything_with_id(s, skip_class, skip)) != NULL)
	  return c;
      }
    }
    w = w->glw_parent;
  }
}


/**
 * Compute the "X and Y" offset in the current widget tree
 *
 * We use these values so we arrive aligned with the old position when
 * we cross verital / horizontal containers
 */
static void
layout_form_get_xy(glw_t *w, int *xp, int *yp)
{
  glw_t *p = w->glw_parent;

  *xp = 0;
  *yp = 0;

  switch(p->glw_class) {
  default:
    break;
  case GLW_CONTAINER_X:
    while((w = TAILQ_PREV(w, glw_queue, glw_parent_link)) != NULL) {
      if(glw_get_opaque(w, layout_form_callback) != NULL)
	(*xp)++;
    }
    break;

  case GLW_CONTAINER_Y:
    while((w = TAILQ_PREV(w, glw_queue, glw_parent_link)) != NULL) {
      if(glw_get_opaque(w, layout_form_callback) != NULL)
	(*yp)++;
    }
    break;
  }
}



/**
 * Attach this callback to widgets in order to perform navigation
 * between them
 */
static int
layout_form_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  glw_t *n = NULL;
  glw_focus_stack_t *gfs = opaque;
  int x = 0, y = 0;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_UP:
  case GLW_SIGNAL_DOWN:
  case GLW_SIGNAL_LEFT:
  case GLW_SIGNAL_RIGHT:
    layout_form_get_xy(w, &x, &y);
    break;
  default:
    break;
  }

  switch(signal) {
  case GLW_SIGNAL_UP:
    n = layout_form_find(w, 1, GLW_CONTAINER_Y, GLW_CONTAINER_X, x);
    break;
  case GLW_SIGNAL_DOWN:
    n = layout_form_find(w, 0, GLW_CONTAINER_Y, GLW_CONTAINER_X, x);
    break;
  case GLW_SIGNAL_LEFT:
    n = layout_form_find(w, 1, GLW_CONTAINER_X, GLW_CONTAINER_Y, y);
    break;
  case GLW_SIGNAL_RIGHT:
    n = layout_form_find(w, 0, GLW_CONTAINER_X, GLW_CONTAINER_Y, y);
    break;
  default:
    break;
  }

  va_end(ap);

  if(n != NULL) {
    glw_focus_lose(gfs, w);
    glw_focus_set(gfs, n);
    return 1;
  }
  return 0;
}



/**
 *
 */
int
layout_form_initialize(struct layout_form_entry_list *lfelist, glw_t *m,
		       glw_focus_stack_t *gfs, ic_t *ic, int updatefocus)
{
  glw_t *w, *c;
  glw_t *ff = NULL;  /* first widget to focus */
  layout_form_entry_t *lfe, *l;
  int len, v;
  char buf[100];

  TAILQ_FOREACH(l, lfelist, lfe_link) {
    w =  glw_find_by_id(m, l->lfe_id, 1);
    if(w == NULL) {
      printf("WARNING: Widget %s not found\n", l->lfe_id);
      continue;
    }
    if(ff == NULL)
      ff = w;

    lfe = calloc(1, sizeof(layout_form_entry_t));
    *lfe = *l;

    lfe->lfe_ic = ic;

    glw_set(w,
	    GLW_ATTRIB_SIGNAL_HANDLER, layout_form_callback, gfs, 402,
	    NULL);

    switch(lfe->lfe_type) {

    case LFE_TYPE_STRING:
      len = strlen((char *)lfe->lfe_buf);
      lfe->lfe_buf_ptr = len;
      lfe->lfe_buf_len = len;
      glw_set(w,
	      GLW_ATTRIB_SIGNAL_HANDLER,  layout_form_entry_string, lfe, 401,
	      GLW_ATTRIB_CAPTION, (char *)lfe->lfe_buf,
	      GLW_ATTRIB_CURSOR_POSITION, len,
	      NULL);
      break;

    case LFE_TYPE_LIST:
      glw_set(w,
	      GLW_ATTRIB_SIGNAL_HANDLER,  layout_form_entry_list, lfe, 401,
	      NULL);
      break;
 
    case LFE_TYPE_BUTTON:
      glw_set(w,
	      GLW_ATTRIB_SIGNAL_HANDLER,  layout_form_entry_button, lfe, 401,
	      NULL);
      break;

    case LFE_TYPE_CHILD_MONITOR:
      glw_set(w,
	      GLW_ATTRIB_SIGNAL_HANDLER, layout_form_entry_child_mon, lfe, 401,
	      NULL);
      break;

    case LFE_TYPE_LIST_OPTION:
      glw_set(w,
	      GLW_ATTRIB_SIGNAL_HANDLER, layout_form_list_option, lfe, 401,
	      NULL);

      v = *(int *)lfe->lfe_buf;

      TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
	if(c->glw_u32 == v)
	  glw_select(w, c);

      break;

    case LFE_TYPE_INTEGER:
      snprintf(buf, sizeof(buf), lfe->lfe_fmt, *(int *)lfe->lfe_buf);
      glw_set(w,
	      GLW_ATTRIB_SIGNAL_HANDLER,  layout_form_entry_integer, lfe, 401,
	      GLW_ATTRIB_CAPTION, buf,
	      NULL);
      break;
 
    default:
      glw_set(w,
	      GLW_ATTRIB_SIGNAL_HANDLER,  layout_form_entry_free, lfe, 401,
	      NULL);
      break;
    }
  }

  if(updatefocus) {
    if(ff != NULL)
      glw_focus_set(gfs, ff);
    glw_focus_stack_activate(gfs);
  }
  return 0;
}

/**
 *
 */
void
layout_form_query(struct layout_form_entry_list *lfelist, glw_t *m,
		  glw_focus_stack_t *gfs, inputevent_t *iep)
{
  ic_t ic;

  input_init(&ic);
  layout_form_initialize(lfelist, m, gfs, &ic, 1);
  input_getevent(&ic, 1, iep, NULL);
}




/**
 * Callback for coupling display of a tab entry (deck child) to a list entry
 */
static int
layout_form_tab_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  glw_t *tab = opaque;

  switch(signal) {
  case GLW_SIGNAL_SELECTED_SELF:
    tab->glw_parent->glw_selected = tab;
    return 1;
  default:
    break;
  }
  return 0;
}


/**
 * Create a new tab that's connected with an entry in a list
 *
 * The tab must reside in a container that's willing to switch
 * display when the glw_selected changes (this is done by the
 * layout_form_tab_callback() above)
 */
glw_t *
layout_form_add_tab(glw_t *m, const char *listname, const char *listmodel,
		    const char *deckname, const char *tabmodel)
{
  glw_t *w, *d, *t;

  w = glw_find_by_id(m, listname, 0);
  d = glw_find_by_id(m, deckname, 0);

  if(w == NULL || d == NULL)
    return NULL;

  t = glw_create(GLW_MODEL,
		 GLW_ATTRIB_FILENAME, tabmodel,
		 GLW_ATTRIB_PARENT, d,
		 NULL);

  glw_create(GLW_MODEL,
	     GLW_ATTRIB_FILENAME, listmodel,
	     GLW_ATTRIB_SIGNAL_HANDLER, layout_form_tab_callback, t, 400,
	     GLW_ATTRIB_PARENT, w,
	     NULL);
  return t;
}

/**
 * Create a new tab that's connected with an entry in a list
 *
 * The tab must reside in a container that's willing to switch
 * display when the glw_selected changes (this is done by the
 * layout_form_tab_callback() above)
 */
glw_t *
layout_form_add_tab2(glw_t *m, const char *listname, glw_t *listentry,
		     const char *deckname, const char *tabmodel)
{
  glw_t *w, *d, *t;

  w = glw_find_by_id(m, listname, 0);
  d = glw_find_by_id(m, deckname, 0);

  if(w == NULL || d == NULL)
    return NULL;

  t = glw_create(GLW_MODEL,
		 GLW_ATTRIB_FILENAME, tabmodel,
		 GLW_ATTRIB_PARENT, d,
		 NULL);

  glw_set(listentry,
	  GLW_ATTRIB_SIGNAL_HANDLER, layout_form_tab_callback, t, 400,
	  GLW_ATTRIB_PARENT, w,
	  NULL);
  return t;
}


/**
 *
 */
static int
get_input_char(inputevent_t *ie)
{
  if(ie->type != INPUT_KEY)
    return -1;

  switch(ie->u.key) {
  case INPUT_KEY_BACK:
  case 32 ... 127:
    return ie->u.key;
  default:
    return -1;
  }
}

/**
 * Insert char in buf
 */
static int
insert_char(layout_form_entry_t *lfe, int ch)
{
  int dlen = lfe->lfe_buf_len + 1; /* string length including trailing NUL */
  int i;
  char *buf = lfe->lfe_buf;

  if(dlen == lfe->lfe_buf_size)
    return -1; /* Max length */
  
  dlen++;

  for(i = dlen; i != lfe->lfe_buf_ptr; i--)
    buf[i] = buf[i - 1];
  
  buf[i] = ch;
  lfe->lfe_buf_len++;
  lfe->lfe_buf_ptr++;
  return 0;
}



/**
 * Delete char from buf
 */
static int
del_char(layout_form_entry_t *lfe)
{
  int dlen = lfe->lfe_buf_len + 1; /* string length including trailing NUL */
  int i;
  char *buf = lfe->lfe_buf;

  if(lfe->lfe_buf_ptr == 0)
    return -1;

  dlen--;

  lfe->lfe_buf_len--;
  lfe->lfe_buf_ptr--;

  for(i = lfe->lfe_buf_ptr; i != dlen; i++)
    buf[i] = buf[i + 1];

  return 0;
}



/**
 * Callback for controlling a string form entry
 */
static int
layout_form_entry_string(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  layout_form_entry_t *lfe = opaque;
  inputevent_t *ie, ie0;
  int r;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_DTOR:
    free(lfe);
    return 0;

  case GLW_SIGNAL_ENTER:
    if(lfe->lfe_value == 0)
      break;

    ie0.type = INPUT_U32;
    ie0.u.u32 = lfe->lfe_value;
    input_postevent(lfe->lfe_ic, &ie0);
    return 1;

  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);

    r = get_input_char(ie);
 
    if(r == INPUT_KEY_BACK) {
      r = del_char(lfe);
    } else if(r != -1) {
      insert_char(lfe, r);
    }

    glw_set(w,
	    GLW_ATTRIB_CAPTION, lfe->lfe_buf,
	    GLW_ATTRIB_CURSOR_POSITION, lfe->lfe_buf_ptr,
	    NULL);
    return 1;

  case GLW_SIGNAL_LEFT:
    lfe->lfe_buf_ptr--;
    if(lfe->lfe_buf_ptr < 0)
      lfe->lfe_buf_ptr = 0;

    glw_set(w,
	    GLW_ATTRIB_CURSOR_POSITION, lfe->lfe_buf_ptr,
	    NULL);
    return 1;

  case GLW_SIGNAL_RIGHT:
    lfe->lfe_buf_ptr++;
    if(lfe->lfe_buf_ptr > lfe->lfe_buf_len)
      lfe->lfe_buf_ptr = lfe->lfe_buf_len;

    glw_set(w,
	    GLW_ATTRIB_CURSOR_POSITION, lfe->lfe_buf_ptr,
	    NULL);
    return 1;


  default:
    break;
  }

  va_end(ap);

  return 0;
}

/**
 * Callback for controlling an integer
 */
static int
layout_form_entry_integer(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  layout_form_entry_t *lfe = opaque;
  int v;
  char buf[100];
  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_DTOR:
    free(lfe);
    return 0;

  case GLW_SIGNAL_LEFT:
    v = *(int *)lfe->lfe_buf;
    v = GLW_MAX(v - lfe->lfe_step, lfe->lfe_min);
    *(int *)lfe->lfe_buf = v;
    snprintf(buf, sizeof(buf), lfe->lfe_fmt, *(int *)lfe->lfe_buf);
    
    glw_set(w,
	    GLW_ATTRIB_CAPTION, buf,
	    NULL);
    return 1;

  case GLW_SIGNAL_RIGHT:
    v = *(int *)lfe->lfe_buf;
    v = GLW_MIN(v + lfe->lfe_step, lfe->lfe_max);
    *(int *)lfe->lfe_buf = v;
    snprintf(buf, sizeof(buf), lfe->lfe_fmt, *(int *)lfe->lfe_buf);
    
    glw_set(w,
	    GLW_ATTRIB_CAPTION, buf,
	    NULL);
    return 1;


  default:
    break;
  }

  va_end(ap);

  return 0;
}


/**
 * Callback for controlling a button form entry
 */
static int
layout_form_entry_button(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  layout_form_entry_t *lfe = opaque;
  inputevent_t ie;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_DTOR:
    free(lfe);
    return 0;

  case GLW_SIGNAL_ENTER:
    ie.type = INPUT_U32;
    ie.u.u32 = lfe->lfe_value;
    input_postevent(lfe->lfe_ic, &ie);
    return 1;
  default:
    break;
  }
  va_end(ap);
  return 0;
}

/**
 * Callback for controlling a string form entry
 */
static int
layout_form_entry_child_mon(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  layout_form_entry_t *lfe = opaque;
  inputevent_t ie;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_DTOR:
    free(lfe);
    return 0;

  case GLW_SIGNAL_SELECTED_CHANGED:
    ie.type = INPUT_U32;
    ie.u.u32 = lfe->lfe_value;
    input_postevent(lfe->lfe_ic, &ie);
    return 1;
  default:
    break;
  }
  va_end(ap);
  return 0;
}



/**
 * Callback for controlling a string form entry
 */
static int
layout_form_entry_list(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  layout_form_entry_t *lfe = opaque;
  glw_t *c;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_DTOR:
    free(lfe);
    return 0;

  case GLW_SIGNAL_SELECTED_CHANGED:
    c = va_arg(ap, void *);
    if(c != NULL)
      snprintf(lfe->lfe_buf, lfe->lfe_buf_size, "%s", c->glw_id ?: "");

    return 1;
  default:
    break;
  }

  va_end(ap);

  return 0;
}


/**
 * Dummy entry, just for freeing
 */
static int
layout_form_entry_free(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  layout_form_entry_t *lfe = opaque;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_DTOR:
    free(lfe);
    return 0;
  default:
    break;
  }

  va_end(ap);

  return 0;
}

static int
layout_form_list_option(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  layout_form_entry_t *lfe = opaque;
  glw_t *c;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_DTOR:
    free(lfe);
    return 0;

  case GLW_SIGNAL_SELECTED_CHANGED:
    c = va_arg(ap, void *);
    if(c != NULL)
      memcpy(lfe->lfe_buf, &c->glw_u32, sizeof(int));

    return 1;
  default:
    break;
  }

  va_end(ap);

  return 0;
}

/**
 * Options fill
 */
void
layout_form_fill_options(glw_t *m, const char *id,
			 layout_form_entry_options_t options[], int num)
{
  int i;

  if((m = glw_find_by_id(m, id, 1)) == NULL) 
    return;

  for(i = 0; i < num; i++) {
    glw_create(GLW_TEXT_BITMAP,
	       GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	       GLW_ATTRIB_PARENT, m,
	       GLW_ATTRIB_CAPTION, options[i].caption,
	       GLW_ATTRIB_U32,     options[i].id,
	       NULL);
  }
}
  


/**
 * Options fill
 */
void
layout_form_add_option(glw_t *m, const char *id, const char *caption, int u32)
{
  if((m = glw_find_by_id(m, id, 1)) == NULL) 
    return;

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_PARENT, m,
	     GLW_ATTRIB_CAPTION, caption,
	     GLW_ATTRIB_U32,    u32,
	     NULL);
}
  
