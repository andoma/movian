#include <stdio.h>
#include "htsmsg/htsmsg_store.h"
#include "settings.h"
#include "video_settings.h"

int subtitle_always_select;
int subtitle_scaling;
int subtitle_alignment;

static void
set_subtitle_always_select(void *opaque, int v)
{
  subtitle_always_select = v;
}

static void
set_subtitle_scale(void *opaque, int v)
{
  subtitle_scaling = v;
}

static void
set_subtitle_alignment(void *opaque, const char *str)
{
  subtitle_alignment = atoi(str);
}


void
video_settings_init(void)
{
  htsmsg_t *store;
  prop_t *s;
  setting_t *x;

 


  s = settings_add_dir(NULL, "Subtitles", NULL, NULL);

  if((store = htsmsg_store_load("subtitles")) == NULL)
    store = htsmsg_create_map();

  settings_create_bool(s, "alwaysselect", "Always select a subtitle", 1, 
		       store, set_subtitle_always_select, NULL,
		       SETTINGS_INITIAL_UPDATE,  NULL,
		       settings_generic_save_settings, 
		       (void *)"subtitles");
  
  settings_create_int(s, "scale", "Subtitle size scaling",
		      100, store, 5, 300, 5, set_subtitle_scale, NULL,
		      SETTINGS_INITIAL_UPDATE, "%", NULL,
		      settings_generic_save_settings, 
		      (void *)"subtitles");

  x = settings_create_multiopt(s, "alignment", "Subtitle alignment",
			       set_subtitle_alignment, NULL);

  settings_multiopt_add_opt(x, "0", "Center", 1);
  settings_multiopt_add_opt(x, "1", "Left", 0);
  settings_multiopt_add_opt(x, "2", "Right", 0);
  settings_multiopt_add_opt(x, "3", "Auto", 0);

  settings_multiopt_initiate(x, store, settings_generic_save_settings, 
			     (void *)"subtitles");
}
