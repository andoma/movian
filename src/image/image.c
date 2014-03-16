/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2014 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */

#include "image.h"
#include "arch/atomic.h"
#include "misc/pixmap.h"
#include "showtime.h"

/**
 *
 */
image_t *
image_alloc(int num_components)
{
  image_t *im = calloc(1, sizeof(image_t) + sizeof(image_component_t) *
                        num_components);
  im->im_refcount = 1;
  im->im_num_components = num_components;
  return im;
}


/**
 *
 */
image_t *
image_retain(image_t *im)
{
  atomic_add(&im->im_refcount, 1);
  return im;
}


/**
 *
 */
void
image_clear_component(image_component_t *ic)
{
  switch(ic->type) {
  case IMAGE_none:
    break;

  case IMAGE_PIXMAP:
    pixmap_release(ic->pm);
    break;

  case IMAGE_CODED:
    free(ic->coded.data);
    break;

  case IMAGE_VECTOR:
    free(ic->vector.commands);
    break;

  case IMAGE_TEXT_INFO:
    free(ic->text_info.ti_charpos);
    break;
  }
  ic->type = IMAGE_none;
}


/**
 *
 */
void
image_release(image_t *im)
{
  if(im == NULL)
    return;

  if(atomic_add(&im->im_refcount, -1) > 1)
    return;

  for(int i = 0; i < im->im_num_components; i++)
    image_clear_component(&im->im_components[i]);

  free(im);
}


/**
 *
 */
void
image_dump(const image_t *im, const char *prefix)
{
  if(im == NULL) {
    trace(TRACE_NO_PROP, TRACE_DEBUG, prefix, "NULL");
    return;
  }
  trace(TRACE_NO_PROP, TRACE_DEBUG, prefix,
        "Image %d x %d (margin: %d) %d components",
        im->im_width, im->im_height, im->im_margin, im->im_num_components);
  for(int i = 0; i < im->im_num_components; i++) {
    const image_component_t *ic = &im->im_components[i];
    const pixmap_t *pm;
    const image_component_text_info_t *ti;
    switch(ic->type) {
    case IMAGE_none:

      trace(TRACE_NO_PROP, TRACE_DEBUG, prefix,
            "[%d]: Empty", i);
      break;

    case IMAGE_PIXMAP:
      pm = ic->pm;
      trace(TRACE_NO_PROP, TRACE_DEBUG, prefix,
            "[%d]: Pixmap %d x %d",
            i, pm->pm_width, pm->pm_height);
      break;

    case IMAGE_CODED:
      trace(TRACE_NO_PROP, TRACE_DEBUG, prefix,
            "IMG", "[%d]: Coded", i);
      break;

  case IMAGE_VECTOR:
      trace(TRACE_NO_PROP, TRACE_DEBUG, prefix,
            "[%d]: Vector %d ops", i, ic->vector.count);
    break;

    case IMAGE_TEXT_INFO:
      ti = &ic->text_info;
      trace(TRACE_NO_PROP, TRACE_DEBUG, prefix,
            "[%d]: Textinfo, lines:%d, flags: [%s %s]",
            i, ti->ti_lines,
            ti->ti_flags & IMAGE_TEXT_WRAPPED   ? "Wrapped" : "",
            ti->ti_flags & IMAGE_TEXT_TRUNCATED ? "Truncated" : "");
      break;
    }
  }
}
