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

typedef struct layout_form_entry {
  const char *lfe_id;
  enum {
    FORM_TYPE_STRING,
    FORM_TYPE_IP,
    FORM_TYPE_INTEGER,
    FORM_TYPE_SELECTION,
    FORM_TYPE_BUTTON,
  } lfe_type;

  void *lfe_buffer;         /* for result, we also use this to prep
			       the current value */
  size_t lfe_buffer_size;   /* size of dito */

  int lfe_changed;          /* Set if buffer has been changed */

} layout_form_entry_t;


int layout_form_query(layout_form_entry_t lfes[], int nlfes,
		      const char *model);

#endif /* LAYOUT_FORMS_H */

