#include <inttypes.h>
#include "../config.h"
#include "rgb2rgb.h"

/* TODO: MMX optimization */

void rgb24to32(uint8_t *src,uint8_t *dst,uint32_t src_size)
{
  uint32_t *dest = (uint32_t *)dst;
  uint8_t *s = src;
  uint8_t *end;
  end = s + src_size;
  while(s < end)
  {
    uint32_t rgb0;
    rgb0 = *(uint32_t *)s;
    *dest++ = rgb0 & 0xFFFFFFUL;
    s += 3;
  }
}

void rgb32to24(uint8_t *src,uint8_t *dst,uint32_t src_size)
{
  uint8_t *dest = dst;
  uint8_t *s = src;
  uint8_t *end;
  end = s + src_size;
  while(s < end)
  {
    *dest++ = *s++;
    *dest++ = *s++;
    *dest++ = *s++;
    s++;
  }
}
