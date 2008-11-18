#include <inttypes.h>
#include <sys/time.h>
#include <time.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "fileaccess/fa_rawloader.h"

#include "glw.h"
#include "glw_text.h"
#include "glw_text_bitmap.h"

FT_Library glw_text_library;
FT_Face glw_text_face_variable;

/*
 *
 */
int
glw_text_getutf8(const char **s)
{
  uint8_t c;
  int r;
  int l;

  c = **s;
  *s = *s + 1;

  switch(c) {
  case 0 ... 127:
    return c;

  case 192 ... 223:
    r = c & 0x1f;
    l = 1;
    break;

  case 224 ... 239:
    r = c & 0xf;
    l = 2;
    break;

  case 240 ... 247:
    r = c & 0x7;
    l = 3;
    break;

  case 248 ... 251:
    r = c & 0x3;
    l = 4;
    break;

  case 252 ... 253:
    r = c & 0x1;
    l = 5;
    break;
  default:
    return 0;
  }

  while(l-- > 0) {
    c = **s;
    *s = *s + 1;
    if(c == 0)
      return 0;
    r = r << 6 | (c & 0x3f);
  }
  return r;
}

/*
 *
 */
int
glw_text_init(glw_root_t *gr)
{
  int error;
  const void *r;
  size_t size;

  const char *font_variable = "theme://font.ttf";

  error = FT_Init_FreeType(&glw_text_library);
  if(error) {
    fprintf(stderr, "Freetype init error\n");
    return -1;
  }

  if((r = fa_rawloader(font_variable, &size)) == NULL) {
    fprintf(stderr, "Unable to load %s\n", font_variable);
    return -1;
  }

  if(glw_text_bitmap_init(gr, r, size) < 0) {
    fa_rawunload(r);
    fprintf(stderr, "Cannot load font \"%s\" for bitmapped rendering\n", 
	    font_variable);
    return -1;
  }

  //  glw_rawunload(r);
  return 0;
}
