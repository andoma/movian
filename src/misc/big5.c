
#include <inttypes.h>
#include "str.h"
#include "big5.h"

#define BIG5_TABLE_OFFSET 0xa100

static const uint16_t big5table[] = {
#include "big5_table.h"
};


int
big5_convert(const struct charset *cs, char *dst,
             const char *src, int len, int strict)
{
  int outlen = 0;

  for(int i = 0; i < len; i++) {
    if(*src < 0x80) {
      if(dst != NULL)
        *dst++ = *src;
      outlen++;
      src++;
      continue;
    }

    unsigned int in;

    if(len == 1) {
      in = -1;
      src++;
    } else {
      in = (src[0] << 8) | src[1];
      in -= BIG5_TABLE_OFFSET;
      src += 2;
      i++;
    }

    uint16_t out;

    if(in > sizeof(big5table) / 2) {

      if(strict)
        return -1;

      out = 0xfffd;
    } else {
      out = big5table[in];
      if(out == 0) {
        if(strict)
          return -1;
        else
          out = 0xfffd;
      }
    }

    int ol = utf8_put(dst, out);
    outlen += ol;
    if(dst)
      dst += ol;
  }
  return outlen;
}


