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

#include <libdvdnav/dvdnav.h>

#include "app.h"
#include "mpeg_support.h"

typedef struct dvd_player {
  
  dvdnav_t    *dp_dvdnav;

  pes_player_t dp_pp;


  int          dp_audio_mode;
#define DP_AUDIO_FOLLOW_VM 0
#define DP_AUDIO_DISABLE   1
#define DP_AUDIO_OVERRIDE  2
  int          dp_audio[3];



  int          dp_spu_mode;
#define DP_SPU_FOLLOW_VM 0
#define DP_SPU_DISABLE   1
#define DP_SPU_OVERRIDE  2
  int          dp_spu[3];

  uint32_t     dp_clut[16];

  int          dp_inmenu;

  struct appi *dp_ai;

  pci_t          dp_pci;

  glw_t *dp_widget_chapter;
  glw_t *dp_widget_title;
  glw_t *dp_widget_time;

} dvd_player_t;

int dvd_main(appi_t *ai, const char *devname, int isdrive, glw_t *parent);

#endif /* DVD_H */
