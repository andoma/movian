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
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include <jpeglib.h>
#include <unistd.h>

#include "fileaccess/fileaccess.h"

#include "image.h"
#include "pixmap.h"
#include "misc/buf.h"


struct my_error_mgr {
  struct jpeg_error_mgr pub;
  jmp_buf setjmp_buffer;
};

typedef struct my_error_mgr *my_error_ptr;


static void
my_error_exit(j_common_ptr cinfo)
{
  my_error_ptr myerr = (my_error_ptr) cinfo->err;
  (*cinfo->err->output_message) (cinfo);

  longjmp(myerr->setjmp_buffer, 1);
}


pixmap_t *
libjpeg_decode(fa_handle_t *fh, const image_meta_t *im,
               char *errbuf, size_t errlen)
{
  struct jpeg_decompress_struct cinfo;
  struct my_error_mgr jerr;
  JSAMPARRAY buffer = NULL;
  fa_seek(fh, 0, SEEK_SET);
  FILE *f = fa_fopen(fh, 1);
  pixmap_t *pm = NULL;

  cinfo.err = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = my_error_exit;
  if(setjmp(jerr.setjmp_buffer)) {
    jpeg_destroy_decompress(&cinfo);
    fclose(f);
    if(pm != NULL)
      pixmap_release(pm);
    return NULL;
  }


  jpeg_create_decompress(&cinfo);
  //  jpeg_mem_src(&cinfo, (void *)buf_cstr(b), buf_len(b));
  jpeg_stdio_src(&cinfo, f);

  jpeg_read_header(&cinfo, TRUE);

  cinfo.buffered_image = 1;
  jpeg_start_decompress(&cinfo);

  while(!jpeg_input_complete(&cinfo)) {

    if(pm != NULL) {

      if(im->im_incremental != NULL)
        im->im_incremental(im->im_opaque, pm);
      pixmap_release(pm);
    }

    pm = pixmap_create(cinfo.output_width,
                       cinfo.output_height,
                       PIXMAP_RGB24, 0);

    if(buffer == NULL) {
      buffer = (*cinfo.mem->alloc_sarray)
        ((j_common_ptr) &cinfo, JPOOL_IMAGE,
         pm->pm_linesize, 1);
    }

    jpeg_start_output(&cinfo, cinfo.input_scan_number);

    while(cinfo.output_scanline < cinfo.output_height) {
      jpeg_read_scanlines(&cinfo, buffer, 1);
      memcpy(pm->pm_data + (cinfo.output_scanline - 1) * pm->pm_linesize,
             buffer[0], pm->pm_linesize);
    }
    jpeg_finish_output(&cinfo);
  }
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  fclose(f);
  return pm;
}
