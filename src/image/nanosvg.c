/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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

#include <stdio.h>
#include "image.h"
#include "pixmap.h"
#include "misc/buf.h"

#define NANOSVG_IMPLEMENTATION
#include "ext/nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "ext/nanosvg/nanosvgrast.h"

image_t *
nanosvg_decode(buf_t *buf, const image_meta_t *im,
               char *errbuf, size_t errlen)
{
  if(im->im_req_width != -1 && im->im_req_height != -1) {
    snprintf(errbuf, errlen, "Aspect distortion not supported for SVG");
    return NULL;
  }

  buf = buf_retain(buf);
  buf = buf_make_writable(buf);

  NSVGimage *image = nsvgParse(buf_str(buf), "px", 96.0f,
                               NSVG_RGB(255,255,255),
                               NSVG_RGB(0,0,0));
  buf_release(buf);
  if(image == NULL) {
    snprintf(errbuf, errlen, "Unable to parse SVG file");
    return NULL;
  }

  int orig_width = image->width;
  int orig_height = image->height;

  if(orig_width < 1 || orig_height < 1) {
    snprintf(errbuf, errlen, "Invalid SVG dimensions");
    nsvgDelete(image);
    return NULL;
  }

  int w, h;
  float scale = 1;
  if(im->im_req_width != -1) {
    scale = (float)im->im_req_width / orig_width;
    w = im->im_req_width;
    h = im->im_req_width * orig_height / orig_width;
  } else if(im->im_req_height != -1) {
    scale = (float)im->im_req_height / orig_height;
    w = im->im_req_height * orig_width / orig_height;
    h = im->im_req_height;
  } else {
    w = orig_width;
    h = orig_height;
  }

  pixmap_t *pm = pixmap_create(w, h, PIXMAP_RGBA, im->im_margin);
  NSVGrasterizer *rast = nsvgCreateRasterizer();

  nsvgRasterize(rast, image, im->im_margin, im->im_margin,
                scale, pm->pm_data, w, h, pm->pm_linesize);

  nsvgDeleteRasterizer(rast);
  nsvgDelete(image);
  image_t *img = image_create_from_pixmap(pm);
  pixmap_release(pm);
  return img;
}
