/*
 *  Forms
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

#ifndef LAYOUT_FORMS_H
#define LAYOUT_FORMS_H

#include "hid/input.h"

TAILQ_HEAD(layout_form_entry_list, layout_form_entry);

typedef struct layout_form_entry {
  TAILQ_ENTRY(layout_form_entry) lfe_link;
  
  ic_t *lfe_ic;

  const char *lfe_id;

  glw_t *lfe_widget;

  int lfe_type;
#define LFE_TYPE_STRING 1
#define LFE_TYPE_BUTTON 2
#define LFE_TYPE_LIST   3
#define LFE_TYPE_CUSTOM 4
#define LFE_TYPE_CHILD_MONITOR 5

  int lfe_value;

  void *lfe_buf;
  size_t lfe_buf_size;   /* Total size of buffer (allocated memory) */
  int lfe_buf_ptr;       /* Current position for insert (cursor)    */
  int lfe_buf_len;       /* Size of current data (string length)    */

  glw_callback_t *lfe_widget_callback;

} layout_form_entry_t;


int layout_form_query(struct layout_form_entry_list *lfelist,
		      glw_t *m, glw_focus_stack_t *gfs);

int layout_form_initialize(struct layout_form_entry_list *lfelist,
			   glw_t *m, glw_focus_stack_t *gfs, ic_t *ic,
			   int updatefocus);

/**
 * Generates an event when the child changes
 */
#define LFE_ADD_MONITOR_CHILD(listp, id, val) do {			\
  layout_form_entry_t *lfe = alloca(sizeof(layout_form_entry_t));	\
  memset(lfe, 0, sizeof(layout_form_entry_t));				\
  lfe->lfe_type = LFE_TYPE_CHILD_MONITOR;                               \
  lfe->lfe_value = val;                                                 \
  TAILQ_INSERT_TAIL(listp, lfe, lfe_link);				\
  lfe->lfe_id = id;							\
} while(0)

/**
 * Custom callback
 */
#define LFE_ADD_CUSTOM(listp, id, cb) do {				\
  layout_form_entry_t *lfe = alloca(sizeof(layout_form_entry_t));	\
  memset(lfe, 0, sizeof(layout_form_entry_t));				\
  lfe->lfe_type = LFE_TYPE_CUSTOM;                                      \
  lfe->lfe_widget_callback = cb                                         \
  TAILQ_INSERT_TAIL(listp, lfe, lfe_link);				\
  lfe->lfe_id = id;							\
} while(0)

/**
 * Modifies a string storage
 */
#define LFE_ADD_STR(listp, id, str, bufsize, retval) do {		\
  layout_form_entry_t *lfe = alloca(sizeof(layout_form_entry_t));	\
  memset(lfe, 0, sizeof(layout_form_entry_t));				\
  lfe->lfe_type = LFE_TYPE_STRING;                                      \
  lfe->lfe_buf = str;							\
  lfe->lfe_buf_size = bufsize;						\
  lfe->lfe_value = retval;                                              \
  TAILQ_INSERT_TAIL(listp, lfe, lfe_link);				\
  lfe->lfe_id = id;							\
} while(0)

/**
 * Modifies a string storage based on the LIST child IDs
 */
#define LFE_ADD_LIST(listp, id, str, bufsize) do {			\
  layout_form_entry_t *lfe = alloca(sizeof(layout_form_entry_t));	\
  memset(lfe, 0, sizeof(layout_form_entry_t));				\
  lfe->lfe_type = LFE_TYPE_LIST;                                        \
  lfe->lfe_buf = str;							\
  lfe->lfe_buf_size = bufsize;						\
  TAILQ_INSERT_TAIL(listp, lfe, lfe_link);				\
  lfe->lfe_id = id;							\
} while(0)

/**
 * Signal an even when pressed
 */
#define LFE_ADD_BTN(listp, id, val) do {				\
  layout_form_entry_t *lfe = alloca(sizeof(layout_form_entry_t));	\
  memset(lfe, 0, sizeof(layout_form_entry_t));				\
  lfe->lfe_type = LFE_TYPE_BUTTON;					\
  lfe->lfe_value = val;							\
  TAILQ_INSERT_TAIL(listp, lfe, lfe_link);				\
  lfe->lfe_id = id;							\
} while(0)

#define LFE_ADD(listp, id) do {						\
  layout_form_entry_t *lfe = alloca(sizeof(layout_form_entry_t));	\
  memset(lfe, 0, sizeof(layout_form_entry_t));				\
  TAILQ_INSERT_TAIL(listp, lfe, lfe_link);				\
  lfe->lfe_id = id;							\
} while(0)

glw_t *layout_form_add_tab(glw_t *m, const char *listname,
			   const char *listmodel, const char *deckname,
			   const char *tabmodel);

glw_t *layout_form_add_tab2(glw_t *m, const char *listname, glw_t *listentry,
			    const char *deckname, const char *tabmodel);


#endif /* LAYOUT_FORMS_H */

