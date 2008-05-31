/*
 *  Display settings
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


#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#include <libhts/hts_strtab.h>
#include <libhts/htscfg.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include "hid/keymapper.h"
#include "layout/layout.h"
#include "layout/layout_forms.h"
#include "display_settings.h"

static struct strtab displaymodetab[] = {
  { "windowed",           DISPLAYMODE_WINDOWED },
  { "fullscreen",         DISPLAYMODE_FULLSCREEN },
};

display_settings_t display_settings;


/**
 * Load display settings
 */
void
display_settings_load(void)
{
  char path[PATH_MAX];
  struct config_head cl;
  const char *v;

  snprintf(path, sizeof(path), "%s/display", settingsdir);

  TAILQ_INIT(&cl);

  if(config_read_file0(path, &cl) == -1)
    return;

  if((v = config_get_str_sub(&cl, "displaymode", NULL)) != NULL)
    display_settings.displaymode = str2val(v, displaymodetab);

  config_free0(&cl);
}



/**
 * Save display settings
 */
void
display_settings_save(void)
{
  char path[PATH_MAX];
  FILE *fp;

  snprintf(path, sizeof(path), "%s/display", settingsdir);

  if((fp = fopen(path, "w+")) == NULL)
    return;

  fprintf(fp, "displaymode = %s\n", val2str(display_settings.displaymode,
					    displaymodetab));
  fclose(fp);
}





void
display_settings_init(glw_t *m, glw_focus_stack_t *gfs, ic_t *ic)
{
  struct layout_form_entry_list lfelist;
  glw_t *t;

  TAILQ_INIT(&lfelist);

  t = layout_form_add_tab(m,
			  "settings_list",     "settings/display-icon",
			  "settings_container","settings/display-tab");
  
  if(t == NULL)
    return;

  layout_form_add_option(t, "displaymodes", "Windowed", 
			 DISPLAYMODE_WINDOWED);
  layout_form_add_option(t, "displaymodes", "Fullscreen",
			 DISPLAYMODE_FULLSCREEN);

  LFE_ADD_OPTION(&lfelist, "displaymodes", &display_settings.displaymode);

  layout_form_initialize(&lfelist, m, gfs, ic, 0);
}
