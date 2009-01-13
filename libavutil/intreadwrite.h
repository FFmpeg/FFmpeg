/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVUTIL_INTREADWRITE_H
#define AVUTIL_INTREADWRITE_H

#include <stdint.h>
#include "config.h"
#include "bswap.h"

#ifdef __GNUC__

struct unaligned_64 { uint64_t l; } __attribute__((packed));
struct unaligned_32 { uint32_t l; } __attribute__((packed));
struct unaligned_16 { uint16_t l; } __attribute__((packed));

#define AV_RN16(a) (((const struct unaligned_16 *) (a))->l)
#define AV_RN32(a) (((const struct unaligned_32 *) (a))->l)
#define AV_RN64(a) (((const struct unaligned_64 *) (a))->l)

#define AV_WN16(a, b) (((struct unaligned_16 *) (a))->l) = (b)
#define AV_WN32(a, b) (((struct unaligned_32 *) (a))->l) = (b)
#define AV_WN64(a, b) (((struct unaligned_64 *) (a))->l) = (b)

#elif defined(__DECC)

#define AV_RN16(a) (*((const __unaligned uint16_t*)(a)))
#define AV_RN32(a) (*((const __unaligned uint32_t*)(a)))
#define AV_RN64(a) (*((const __unaligned uint64_t*)(a)))

#define AV_WN16(a, b) *((__unaligned uint16_t*)(a)) = (b)
#define AV_WN32(a, b) *((__unaligned uint32_t*)(a)) = (b)
#define AV_WN64(a, b) *((__unaligned uint64_t*)(a)) = (b)

#else

#define AV_RN16(a) (*((const uint16_t*)(a)))
#define AV_RN32(a) (*((const uint32_t*)(a)))
#define AV_RN64(a) (*((const uint64_t*)(a)))

#define AV_WN16(a, b) *((uint16_t*)(a)) = (b)
#define AV_WN32(a, b) *((uint32_t*)(a)) = (b)
#define AV_WN64(a, b) *((uint64_t*)(a)) = (b)

#endif /* !__GNUC__ */

/* endian macros */
#define AV_RB8(x)     (((const uint8_t*)(x))[0])
#define AV_WB8(p, d)  do { ((uint8_t*)(p))[0] = (d); } while(0)

#define AV_RL8(x)     AV_RB8(x)
#define AV_WL8(p, d)  AV_WB8(p, d)

#if HAVE_FAST_UNALIGNED
# ifdef WORDS_BIGENDIAN
#  define AV_RB16(x)    AV_RN16(x)
#  define AV_WB16(p, d) AV_WN16(p, d)

#  define AV_RL16(x)    bswap_16(AV_RN16(x))
#  define AV_WL16(p, d) AV_WN16(p, bswap_16(d))

#  define AV_RB32(x)    AV_RN32(x)
#  define AV_WB32(p, d) AV_WN32(p, d)

#  define AV_RL32(x)    bswap_32(AV_RN32(x))
#  define AV_WL32(p, d) AV_WN32(p, bswap_32(d))

#  define AV_RB64(x)    AV_RN64(x)
#  define AV_WB64(p, d) AV_WN64(p, d)

#  define AV_RL64(x)    bswap_64(AV_RN64(x))
#  define AV_WL64(p, d) AV_WN64(p, bswap_64(d))
# else /* WORDS_BIGENDIAN */
#  define AV_RB16(x)    bswap_16(AV_RN16(x))
#  define AV_WB16(p, d) AV_WN16(p, bswap_16(d))

#  define AV_RL16(x)    AV_RN16(x)
#  define AV_WL16(p, d) AV_WN16(p, d)

#  define AV_RB32(x)    bswap_32(AV_RN32(x))
#  define AV_WB32(p, d) AV_WN32(p, bswap_32(d))

#  define AV_RL32(x)    AV_RN32(x)
#  define AV_WL32(p, d) AV_WN32(p, d)

#  define AV_RB64(x)    bswap_64(AV_RN64(x))
#  define AV_WB64(p, d) AV_WN64(p, bswap_64(d))

#  define AV_RL64(x)    AV_RN64(x)
#  define AV_WL64(p, d) AV_WN64(p, d)
# endif
#else /* HAVE_FAST_UNALIGNED */
#define AV_RB16(x)  ((((const uint8_t*)(x))[0] << 8) | ((const uint8_t*)(x))[1])
#define AV_WB16(p, d) do { \
                    ((uint8_t*)(p))[1] = (d); \
                    ((uint8_t*)(p))[0] = (d)>>8; } while(0)

#define AV_RL16(x)  ((((const uint8_t*)(x))[1] << 8) | \
                      ((const uint8_t*)(x))[0])
#define AV_WL16(p, d) do { \
                    ((uint8_t*)(p))[0] = (d); \
                    ((uint8_t*)(p))[1] = (d)>>8; } while(0)

#define AV_RB32(x)  ((((const uint8_t*)(x))[0] << 24) | \
                     (((const uint8_t*)(x))[1] << 16) | \
                     (((const uint8_t*)(x))[2] <<  8) | \
                      ((const uint8_t*)(x))[3])
#define AV_WB32(p, d) do { \
                    ((uint8_t*)(p))[3] = (d); \
                    ((uint8_t*)(p))[2] = (d)>>8; \
                    ((uint8_t*)(p))[1] = (d)>>16; \
                    ((uint8_t*)(p))[0] = (d)>>24; } while(0)

#define AV_RL32(x) ((((const uint8_t*)(x))[3] << 24) | \
                    (((const uint8_t*)(x))[2] << 16) | \
                    (((const uint8_t*)(x))[1] <<  8) | \
                     ((const uint8_t*)(x))[0])
#define AV_WL32(p, d) do { \
                    ((uint8_t*)(p))[0] = (d); \
                    ((uint8_t*)(p))[1] = (d)>>8; \
                    ((uint8_t*)(p))[2] = (d)>>16; \
                    ((uint8_t*)(p))[3] = (d)>>24; } while(0)

#define AV_RB64(x)  (((uint64_t)((const uint8_t*)(x))[0] << 56) | \
                     ((uint64_t)((const uint8_t*)(x))[1] << 48) | \
                     ((uint64_t)((const uint8_t*)(x))[2] << 40) | \
                     ((uint64_t)((const uint8_t*)(x))[3] << 32) | \
                     ((uint64_t)((const uint8_t*)(x))[4] << 24) | \
                     ((uint64_t)((const uint8_t*)(x))[5] << 16) | \
                     ((uint64_t)((const uint8_t*)(x))[6] <<  8) | \
                      (uint64_t)((const uint8_t*)(x))[7])
#define AV_WB64(p, d) do { \
                    ((uint8_t*)(p))[7] = (d);     \
                    ((uint8_t*)(p))[6] = (d)>>8;  \
                    ((uint8_t*)(p))[5] = (d)>>16; \
                    ((uint8_t*)(p))[4] = (d)>>24; \
                    ((uint8_t*)(p))[3] = (d)>>32; \
                    ((uint8_t*)(p))[2] = (d)>>40; \
                    ((uint8_t*)(p))[1] = (d)>>48; \
                    ((uint8_t*)(p))[0] = (d)>>56; } while(0)

#define AV_RL64(x)  (((uint64_t)((const uint8_t*)(x))[7] << 56) | \
                     ((uint64_t)((const uint8_t*)(x))[6] << 48) | \
                     ((uint64_t)((const uint8_t*)(x))[5] << 40) | \
                     ((uint64_t)((const uint8_t*)(x))[4] << 32) | \
                     ((uint64_t)((const uint8_t*)(x))[3] << 24) | \
                     ((uint64_t)((const uint8_t*)(x))[2] << 16) | \
                     ((uint64_t)((const uint8_t*)(x))[1] <<  8) | \
                      (uint64_t)((const uint8_t*)(x))[0])
#define AV_WL64(p, d) do { \
                    ((uint8_t*)(p))[0] = (d);     \
                    ((uint8_t*)(p))[1] = (d)>>8;  \
                    ((uint8_t*)(p))[2] = (d)>>16; \
                    ((uint8_t*)(p))[3] = (d)>>24; \
                    ((uint8_t*)(p))[4] = (d)>>32; \
                    ((uint8_t*)(p))[5] = (d)>>40; \
                    ((uint8_t*)(p))[6] = (d)>>48; \
                    ((uint8_t*)(p))[7] = (d)>>56; } while(0)
#endif  /* HAVE_FAST_UNALIGNED */

#define AV_RB24(x)  ((((const uint8_t*)(x))[0] << 16) | \
                     (((const uint8_t*)(x))[1] <<  8) | \
                      ((const uint8_t*)(x))[2])
#define AV_WB24(p, d) do { \
                    ((uint8_t*)(p))[2] = (d); \
                    ((uint8_t*)(p))[1] = (d)>>8; \
                    ((uint8_t*)(p))[0] = (d)>>16; } while(0)

#define AV_RL24(x)  ((((const uint8_t*)(x))[2] << 16) | \
                     (((const uint8_t*)(x))[1] <<  8) | \
                      ((const uint8_t*)(x))[0])
#define AV_WL24(p, d) do { \
                    ((uint8_t*)(p))[0] = (d); \
                    ((uint8_t*)(p))[1] = (d)>>8; \
                    ((uint8_t*)(p))[2] = (d)>>16; } while(0)

#endif /* AVUTIL_INTREADWRITE_H */
