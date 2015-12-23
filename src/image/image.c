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
#include "image.h"

#include "main.h"
#include "arch/atomic.h"
#include "misc/buf.h"
#include "pixmap.h"

struct pixmap *(*accel_image_decode)(image_coded_type_t type,
				     struct buf *buf,
				     const image_meta_t *im,
				     char *errbuf, size_t errlen,
                                     const image_t *img);



/**
 *
 */
image_t *
image_alloc(int num_components)
{
  image_t *im = calloc(1, sizeof(image_t) + sizeof(image_component_t) *
                        num_components);
  atomic_set(&im->im_refcount, 1);
  im->im_num_components = num_components;
  return im;
}


/**
 *
 */
image_t *
image_retain(image_t *im)
{
  atomic_inc(&im->im_refcount);
  return im;
}


/**
 *
 */
void
image_clear_component(image_component_t *ic)
{
  switch(ic->type) {
  case IMAGE_component_none:
    break;

  case IMAGE_PIXMAP:
    pixmap_release(ic->pm);
    break;

  case IMAGE_CODED:
    buf_release(ic->coded.icc_buf);
    break;

  case IMAGE_VECTOR:
    free(ic->vector.icv_data);
    break;

  case IMAGE_TEXT_INFO:
    free(ic->text_info.ti_charpos);
    break;
  }
  ic->type = IMAGE_component_none;
}


/**
 *
 */
void
image_release(image_t *im)
{
  if(im == NULL)
    return;

  if(atomic_dec(&im->im_refcount))
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
    tracelog(TRACE_NO_PROP, TRACE_DEBUG, prefix, "NULL");
    return;
  }
  tracelog(TRACE_NO_PROP, TRACE_DEBUG, prefix,
        "Image %d x %d (margin: %d) %d components",
        im->im_width, im->im_height, im->im_margin, im->im_num_components);
  for(int i = 0; i < im->im_num_components; i++) {
    const image_component_t *ic = &im->im_components[i];
    const pixmap_t *pm;
    const image_component_text_info_t *ti;
    switch(ic->type) {
    case IMAGE_component_none:

      tracelog(TRACE_NO_PROP, TRACE_DEBUG, prefix,
            "[%d]: Empty", i);
      break;

    case IMAGE_PIXMAP:
      pm = ic->pm;
      tracelog(TRACE_NO_PROP, TRACE_DEBUG, prefix,
            "[%d]: Pixmap %d x %d",
            i, pm->pm_width, pm->pm_height);
      break;

    case IMAGE_CODED:
      tracelog(TRACE_NO_PROP, TRACE_DEBUG, prefix,
            "[%d]: Coded", i);
      break;

  case IMAGE_VECTOR:
      tracelog(TRACE_NO_PROP, TRACE_DEBUG, prefix,
            "[%d]: Vector %d ops", i, ic->vector.icv_used);
    break;

    case IMAGE_TEXT_INFO:
      ti = &ic->text_info;
      tracelog(TRACE_NO_PROP, TRACE_DEBUG, prefix,
            "[%d]: Textinfo, lines:%d, flags: [%s %s]",
            i, ti->ti_lines,
            ti->ti_flags & IMAGE_TEXT_WRAPPED   ? "Wrapped" : "",
            ti->ti_flags & IMAGE_TEXT_TRUNCATED ? "Truncated" : "");
      break;
    }
  }
}


/**
 *
 */
image_t *
image_coded_alloc(void **datap, size_t size, image_coded_type_t type)
{
  image_t *img;
  buf_t *b = buf_create(size);
  if(b == NULL)
    return NULL;

  img = image_alloc(1);

  img->im_components[0].type = IMAGE_CODED;
  image_component_coded_t *icc = &img->im_components[0].coded;

  icc->icc_buf = b;
  icc->icc_type = type;
  *datap = buf_str(b);
  return img;
}


/**
 *
 */
image_t *
image_coded_create_from_data(const void *data, size_t size,
                             image_coded_type_t type)
{
  void *mem;
  image_t *img = image_coded_alloc(&mem, size, type);
  if(img != NULL)
    memcpy(mem, data, size);
  return img;
}


/**
 *
 */
image_t *
image_coded_create_from_buf(struct buf *buf, image_coded_type_t type)
{
  image_t *img = image_alloc(1);

  img->im_components[0].type = IMAGE_CODED;
  image_component_coded_t *icc = &img->im_components[0].coded;

  icc->icc_buf = buf_retain(buf);
  icc->icc_type = type;
  return img;
}


/**
 *
 */
image_t *
image_create_from_pixmap(pixmap_t *pm)
{
  image_t *img = image_alloc(1);

  img->im_components[0].type = IMAGE_PIXMAP;
  img->im_components[0].pm = pixmap_dup(pm);

  img->im_width  = pm->pm_width;
  img->im_height = pm->pm_height;
  img->im_margin = pm->pm_margin;
  return img;
}


/**
 *
 */
static image_t *
image_decode_coded(const image_t *src, const image_meta_t *meta,
                   char *errbuf, size_t errlen)
{
  const image_component_t *ic = &src->im_components[0];
  const image_component_coded_t *icc = &ic->coded;

  if(icc->icc_type == IMAGE_SVG)
    return nanosvg_decode(icc->icc_buf, meta,  errbuf, errlen);

  pixmap_t *pm = NULL;

  if(accel_image_decode != NULL)
    pm = accel_image_decode(icc->icc_type, icc->icc_buf, meta, errbuf, errlen,
                            src);

  if(pm == NULL)
    pm = image_decode_libav(icc->icc_type, icc->icc_buf, meta, errbuf, errlen);

  if(pm == NULL)
    return NULL;

  /*
   * Invert aspect ratio for orientations that rotate image 90/270 deg, etc
   * Might seem strange, but it does the right thing
   */

  if(src->im_orientation >= LAYOUT_ORIENTATION_TRANSPOSE)
    pm->pm_aspect = 1.0f / pm->pm_aspect;

  image_t *new = image_create_from_pixmap(pm);
  new->im_origin_coded_type = icc->icc_type;
  new->im_orientation = src->im_orientation;
  pixmap_release(pm);
  return new;
}


/**
 *
 */
static void
image_postprocess_pixmap(image_t *img, const image_meta_t *im)
{
  image_component_t *ic = &img->im_components[0];

  if(im->im_shadow)
    pixmap_drop_shadow(ic->pm, im->im_shadow, im->im_shadow);

  if(im->im_corner_radius)
    ic->pm = pixmap_rounded_corners(ic->pm, im->im_corner_radius,
				    im->im_corner_selection);

  if(im->im_intensity_analysis)
    pixmap_intensity_analysis(ic->pm);

}


/**
 *
 */
image_t *
image_decode(image_t *im, const image_meta_t *meta,
             char *errbuf, size_t errlen)
{
  image_t *r;
  const image_component_t *ic = &im->im_components[0];
  assert(im->im_num_components == 1); // We can only deal with one for now

  switch(ic->type) {
  case IMAGE_component_none:
    return im;

  case IMAGE_PIXMAP:
    image_postprocess_pixmap(im, meta);
    return im;

  case IMAGE_CODED:
    r = image_decode_coded(im, meta, errbuf, errlen);
    if(r == NULL)
      break;
    r = image_decode(r, meta, errbuf, errlen);
    break;

  case IMAGE_VECTOR:
    r = image_rasterize_ft(ic, im->im_width, im->im_height, im->im_margin);
    break;

  default:
    abort();
  }

  image_release(im);

  return r;
}


/**
 *
 */
image_t *
image_create_vector(int width, int height, int margin)
{
  image_t *img = image_alloc(1);

  img->im_components[0].type = IMAGE_VECTOR;
  image_component_vector_t *icv = &img->im_components[0].vector;
  img->im_width  = width;
  img->im_height = height;
  img->im_margin = margin;

  icv->icv_capacity = 256;
  icv->icv_data = malloc(icv->icv_capacity * sizeof(uint32_t));
  return img;
}

