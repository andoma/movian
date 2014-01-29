#pragma once
#include "str.h"
int big5_convert(const struct charset *cs, char *dst,
                 const char *src, int len, int strict);
