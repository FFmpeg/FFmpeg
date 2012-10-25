#ifndef LIBAV_COMPAT_TMS470_MATH_H
#define LIBAV_COMPAT_TMS470_MATH_H

#include_next <math.h>

#undef INFINITY
#undef NAN

#define INFINITY (*(const float*)((const unsigned []){ 0x7f800000 }))
#define NAN      (*(const float*)((const unsigned []){ 0x7fc00000 }))

#endif /* LIBAV_COMPAT_TMS470_MATH_H */
