/*
 *
 *      Copyright (C) 2012 Edgar Hucek
 *      Copyright (C) 2015 Lonelycoder AB
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <math.h>
#include <bcm_host.h>
#include <OMX_Core.h>
#include <interface/vmcs_host/vc_cecservice.h>
#include <interface/vchiq_arm/vchiq_if.h>

#include "main.h"
#include "rpi.h"
#include "misc/minmax.h"
#include "video/video_playback.h"
#include "htsmsg/htsmsg.h"
#include "htsmsg/htsmsg_store.h"
#include "prop/prop.h"
#include "settings.h"

extern int restart_ui;

static float get_display_aspect_ratio(HDMI_ASPECT_T aspect)
{
  float display_aspect;
  switch (aspect) {
    case HDMI_ASPECT_4_3:   display_aspect = 4.0/3.0;   break;
    case HDMI_ASPECT_14_9:  display_aspect = 14.0/9.0;  break;
    case HDMI_ASPECT_16_9:  display_aspect = 16.0/9.0;  break;
    case HDMI_ASPECT_5_4:   display_aspect = 5.0/4.0;   break;
    case HDMI_ASPECT_16_10: display_aspect = 16.0/10.0; break;
    case HDMI_ASPECT_15_9:  display_aspect = 15.0/9.0;  break;
    case HDMI_ASPECT_64_27: display_aspect = 64.0/27.0; break;
    default:                display_aspect = 16.0/9.0;  break;
  }
  return display_aspect;
}


/**
 * Return old mode so we can reset
 */
int
rpi_set_display_framerate(float fps, int width, int height)
{
  TV_DISPLAY_STATE_T state = {};

  if(vc_tv_get_display_state(&state)) {
    TRACE(TRACE_DEBUG, "TV", "Failed to get TV state");
    return -1;
  }

  TRACE(TRACE_DEBUG, "TV",
        "Searching for mode for %d x %d @ %f fps. Current mode=%d",
        width, height, fps, state.display.hdmi.mode);


  HDMI_RES_GROUP_T prefer_group;
  uint32_t prefer_mode;

  int native_deinterlace = 0;

  int num_modes =
    vc_tv_hdmi_get_supported_modes_new(HDMI_RES_GROUP_CEA, NULL, 0,
                                       &prefer_group, &prefer_mode);

  if(num_modes <= 0)
    return -1;

  TV_SUPPORTED_MODE_NEW_T *modes = alloca(sizeof(TV_SUPPORTED_MODE_NEW_T) *
                                          num_modes);

  num_modes =
    vc_tv_hdmi_get_supported_modes_new(HDMI_RES_GROUP_CEA, modes, num_modes,
                                       &prefer_group, &prefer_mode);

  TV_SUPPORTED_MODE_NEW_T *best_mode = NULL;

  uint32_t best_score = 1<<30;

  for(int i = 0; i < num_modes; i++) {
    TV_SUPPORTED_MODE_NEW_T *tv = modes + i;

    uint32_t score = 0;
    uint32_t w = tv->width;
    uint32_t h = tv->height;
    uint32_t r = tv->frame_rate;

    /* Check if frame rate match (equal or exact multiple) */
    if(fabs(r - 1.0f*fps) / fps < 0.002f)
      score += 0;
    else if(fabs(r - 2.0f*fps) / fps < 0.002f)
      score += 1<<8;
    else
      score += (1<<16) + (1<<20)/r; // bad - but prefer higher framerate

    /* Check size too, only choose, bigger resolutions */
    if(width && height)
      {
        /* cost of too small a resolution is high */
        score += MAX((int)(width -w), 0) * (1<<16);
        score += MAX((int)(height-h), 0) * (1<<16);
        /* cost of too high a resolution is lower */
        score += MAX((int)(w-width ), 0) * (1<<4);
        score += MAX((int)(h-height), 0) * (1<<4);
      }

    // native is good
    if (!tv->native)
      score += 1<<16;

#if 0
    // wanting 3D but not getting it is a negative
    if (is3d == CONF_FLAGS_FORMAT_SBS && !(tv->struct_3d_mask & HDMI_3D_STRUCT_SIDE_BY_SIDE_HALF_HORIZONTAL))
      score += 1<<18;
    if (is3d == CONF_FLAGS_FORMAT_TB  && !(tv->struct_3d_mask & HDMI_3D_STRUCT_TOP_AND_BOTTOM))
      score += 1<<18;
#endif

    // prefer square pixels modes
    float par = get_display_aspect_ratio((HDMI_ASPECT_T)tv->aspect_ratio)*(float)tv->height/(float)tv->width;
    score += fabs(par - 1.0f) * (1<<12);

    if(score < best_score) {
      best_mode = tv;
      best_score = score;
    }
  }

  if(best_mode == NULL)
    return -1;


  char response[80];
  TRACE(TRACE_DEBUG, "TV",
        "Output mode %d: %dx%d@%d %s%s:%x\n",
        best_mode->code, best_mode->width, best_mode->height,
        best_mode->frame_rate, best_mode->native ? "N":"",
        best_mode->scan_mode?"I":"", best_mode->code);

  if (native_deinterlace && best_mode->scan_mode)
    vc_gencmd(response, sizeof response, "hvs_update_fields %d", 1);

  // if we are closer to ntsc version of framerate, let gpu know
  int ifps = (int)(fps+0.5f);
  int ntsc_freq = fabs(fps*1001.0f/1000.0f - ifps) < fabs(fps-ifps);
  vc_gencmd(response, sizeof response, "hdmi_ntsc_freqs %d", ntsc_freq);

  /* Inform TV of any 3D settings. Note this property just applies
     to next hdmi mode change, so no need to call for 2D modes */
  HDMI_PROPERTY_PARAM_T property;
  property.property = HDMI_PROPERTY_3D_STRUCTURE;
  property.param1 = HDMI_3D_FORMAT_NONE;
  property.param2 = 0;

#if 0
  if (is3d != CONF_FLAGS_FORMAT_NONE)
    {
      if (is3d == CONF_FLAGS_FORMAT_SBS && best_mode->struct_3d_mask & HDMI_3D_STRUCT_SIDE_BY_SIDE_HALF_HORIZONTAL)
        property.param1 = HDMI_3D_FORMAT_SBS_HALF;
      else if (is3d == CONF_FLAGS_FORMAT_TB && best_mode->struct_3d_mask & HDMI_3D_STRUCT_TOP_AND_BOTTOM)
        property.param1 = HDMI_3D_FORMAT_TB_HALF;
      m_BcmHost.vc_tv_hdmi_set_property(&property);
    }
#endif

  TRACE(TRACE_DEBUG, "TV",
        "ntsc_freq:%d %s%s\n", ntsc_freq,
        property.param1 == HDMI_3D_FORMAT_SBS_HALF ? "3DSBS":"",
        property.param1 == HDMI_3D_FORMAT_TB_HALF ? "3DTB":"");

  int err =
    vc_tv_hdmi_power_on_explicit_new(HDMI_MODE_HDMI,
                                     HDMI_RES_GROUP_CEA,
                                     best_mode->code);

  if(err) {
    TRACE(TRACE_DEBUG, "TV", "Failed to set mode %d", best_mode->code);
    return -1;
  }
  return state.display.hdmi.mode;
}


static int respond;


static void
rpi_tv_vpi(vpi_op_t op, struct htsmsg *info, struct prop *p)
{
  if(!respond)
    return;

  if(op == VPI_START) {
    double framerate;
    unsigned int width, height;
    if(htsmsg_get_dbl(info, "framerate", &framerate))
      return;
    if(htsmsg_get_u32(info, "width", &width))
      return;
    if(htsmsg_get_u32(info, "height", &height))
      return;
    rpi_set_display_framerate(framerate, width, height);
    restart_ui = 1;
  }

  if(op == VPI_STOP) {
    vc_tv_hdmi_power_on_preferred();
    restart_ui = 1;
  }
}


VPI_REGISTER(rpi_tv_vpi)

static void
set_framerate(void *aux, int x)
{
  respond = x;
  printf("respond=%d\n", x);
}

static void
rpi_tv_init(void)
{
  prop_t *set = setting_get_dir("settings:tv");
  htsmsg_t *s = htsmsg_store_load("rpitv") ?: htsmsg_create_map();

  setting_create(SETTING_BOOL, set,
		 SETTINGS_INITIAL_UPDATE,
		 SETTING_TITLE(_p("Match display and content framerate")),
                 SETTING_VALUE(0),
		 SETTING_CALLBACK(set_framerate, NULL),
		 SETTING_HTSMSG("setframerate", s, "rpitv"),
		 NULL);


}


INITME(INIT_GROUP_IPC, rpi_tv_init, NULL, 10);
