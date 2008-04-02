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

#include <GL/glu.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include "layout.h"

#include "audio/audio_ui.h"


void
layout_create(void)
{
  layout_overlay_create();
  layout_switcher_create();
  layout_world_create();
}

void
layout_hide(appi_t *ai)
{

}


void
layout_appi_add(appi_t *ai)
{
}



/**
 * Master scene rendering
 */
void 
layout_draw(float aspect)
{
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  layout_world_render(aspect);
  layout_switcher_render(aspect);
  layout_overlay_render(aspect);
}

