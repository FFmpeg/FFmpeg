#ifndef INTREADWRITE_H
#define INTREADWRITE_H

#ifdef __GNUC__

struct unaligned_64 { uint64_t l; } __attribute__((packed));
struct unaligned_32 { uint32_t l; } __attribute__((packed));
struct unaligned_16 { uint16_t l; } __attribute__((packed));

#define LD16(a) (((const struct unaligned_16 *) (a))->l)
#define LD32(a) (((const struct unaligned_32 *) (a))->l)
#define LD64(a) (((const struct unaligned_64 *) (a))->l)

#define ST16(a, b) (((struct unaligned_16 *) (a))->l) = (b)
#define ST32(a, b) (((struct unaligned_32 *) (a))->l) = (b)

#else /* __GNUC__ */

#define LD16(a) (*((uint16_t*)(a)))
#define LD32(a) (*((uint32_t*)(a)))
#define LD64(a) (*((uint64_t*)(a)))

#define ST16(a, b) *((uint16_t*)(a)) = (b)
#define ST32(a, b) *((uint32_t*)(a)) = (b)

#endif /* !__GNUC__ */

/* endian macros */
#if !defined(BE_16) || !defined(BE_32) || !defined(LE_16) || !defined(LE_32)
#define BE_16(x)  ((((uint8_t*)(x))[0] << 8) | ((uint8_t*)(x))[1])
#define BE_32(x)  ((((uint8_t*)(x))[0] << 24) | \
                   (((uint8_t*)(x))[1] << 16) | \
                   (((uint8_t*)(x))[2] << 8) | \
                    ((uint8_t*)(x))[3])
#define LE_16(x)  ((((uint8_t*)(x))[1] << 8) | ((uint8_t*)(x))[0])
#define LE_32(x)  ((((uint8_t*)(x))[3] << 24) | \
                   (((uint8_t*)(x))[2] << 16) | \
                   (((uint8_t*)(x))[1] << 8) | \
                    ((uint8_t*)(x))[0])
#endif

#endif /* INTREADWRITE_H */
