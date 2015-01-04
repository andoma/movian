#pragma once

#define FA_DEBUG           0x1
// #define FA_DUMP  0x2
#define FA_STREAMING       0x4
#define FA_CACHE           0x8
#define FA_BUFFERED_SMALL  0x10
#define FA_BUFFERED_BIG    0x20
#define FA_DISABLE_AUTH    0x40
#define FA_COMPRESSION     0x80
#define FA_NOFOLLOW        0x100
#define FA_WRITE           0x400  // Open for writing (always creates file)
#define FA_APPEND          0x800  /* Only if FA_WRITE:
                                     Seek to EOF when opening
                                     otherwise truncate */
#define FA_IMPORTANT       0x1000
#define FA_NO_RETRIES      0x2000
#define FA_NO_PARKING      0x4000
#define FA_BUFFERED_NO_PREFETCH 0x8000
#define FA_CONTENT_ON_ERROR     0x10000
