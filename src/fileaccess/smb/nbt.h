#pragma once

/**
 * NBT Header
 */
typedef struct {
  uint8_t msg;
  uint8_t flags;
  uint16_t length;
} __attribute__((packed)) NBT_t;


#if defined(__BIG_ENDIAN__)

#define htole_64(v) __builtin_bswap64(v)
#define htole_32(v) __builtin_bswap32(v)
#define htole_16(v) __builtin_bswap16(v)

#define letoh_16(v) __builtin_bswap16(v)
#define letoh_32(v) __builtin_bswap32(v)
#define letoh_64(v) __builtin_bswap64(v)

#elif defined(__LITTLE_ENDIAN__) || (__BYTE_ORDER == __LITTLE_ENDIAN)

#define htole_64(v) (v)
#define htole_32(v) (v)
#define htole_16(v) (v)

#define letoh_16(v) (v)
#define letoh_32(v) (v)
#define letoh_64(v) (v)

#else

#error Dont know endian

#endif

