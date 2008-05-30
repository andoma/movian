/*
 *  Audio framework
 *  Copyright (C) 2007 Andreas Öman
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

#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "showtime.h"
#include "audio.h"
#include "audio_ui.h"

#include "layout/layout_forms.h"

void
audio_init(void)
{
  audio_alsa_init();
  audio_widget_make();
}




void
audio_settings_init(glw_t *m, glw_focus_stack_t *gfs, ic_t *ic)
{
  struct layout_form_entry_list lfelist;
  glw_t *t;

  TAILQ_INIT(&lfelist);

  t = layout_form_add_tab(m,
			  "settings_list",     "settings/audio-icon",
			  "settings_container","settings/audio-tab");
  
  if(t == NULL)
    return;

  LFE_ADD(&lfelist, "audiocontrollers");

  layout_form_initialize(&lfelist, m, gfs, ic, 0);
}
