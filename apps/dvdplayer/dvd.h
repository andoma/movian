/*
 *  DVD player
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

#ifndef DVD_H
#define DVD_H

#include <dvdnav/dvdnav.h>

#include "app.h"
#include "mpeg_support.h"
#include "video/video_decoder.h"

typedef struct dvd_player {
  
  dvdnav_t    *dp_dvdnav;

  media_pipe_t *dp_mp;

  ic_t         dp_ic;

  pes_player_t dp_pp;


  int          dp_audio_track;
#define DP_AUDIO_DISABLE    -1
#define DP_AUDIO_FOLLOW_VM  -2

  int          dp_audio_track_vm;


  int          dp_spu_track;
#define DP_SPU_DISABLE   -1
#define DP_SPU_FOLLOW_VM -2
  int          dp_spu_track_vm;


  uint32_t     dp_clut[16];

  int          dp_inmenu;

  struct appi *dp_ai;

  pci_t          dp_pci;

  glw_t *dp_widget_chapter;
  glw_t *dp_widget_title;
  glw_t *dp_widget_time;
  glw_t *dp_status_overlay;
  glw_t *dp_menu_playfield;

  vd_conf_t dp_vdc;

  int dp_force_status_display;

} dvd_player_t;

int dvd_main(appi_t *ai, const char *devname, int isdrive, glw_t *parent);

#endif /* DVD_H */
