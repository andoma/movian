#pragma once

static __inline void wr64_be(uint8_t *ptr, uint64_t val)
{
#if !defined(__BIG_ENDIAN__)
  val = __builtin_bswap64(val);
#endif
  memcpy(ptr, &val, 8);
}


static __inline void wr32_be(uint8_t *ptr, uint32_t val)
{
#if !defined(__BIG_ENDIAN__)
  val = __builtin_bswap32(val);
#endif
  memcpy(ptr, &val, 4);
}


static __inline void wr16_be(uint8_t *ptr, uint16_t val)
{
#if !defined(__BIG_ENDIAN__)
  val = ((val >> 8) & 0xff) | ((val << 8) & 0xff00);
#endif
  memcpy(ptr, &val, 2);
}



static __inline uint64_t rd64_be(const uint8_t *ptr)
{
  uint64_t val;
  memcpy(&val, ptr, 8);
#if !defined(__BIG_ENDIAN__)
  val = __builtin_bswap64(val);
#endif
  return val;
}


static __inline uint32_t rd32_be(const uint8_t *ptr)
{
  uint32_t val;
  memcpy(&val, ptr, 4);
#if !defined(__BIG_ENDIAN__)
  val = __builtin_bswap32(val);
#endif
  return val;
}



static __inline uint16_t rd16_be(const uint8_t *ptr)
{
  uint16_t val;
  memcpy(&val, ptr, 2);
#if !defined(__BIG_ENDIAN__)
  val = ((val >> 8) & 0xff) | ((val << 8) & 0xff00);
#endif
  return val;
}
