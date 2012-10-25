/* Work around the class() function in AIX math.h clashing with identifiers
 * named "class". */

#ifndef LIBAV_COMPAT_AIX_MATH_H
#define LIBAV_COMPAT_AIX_MATH_H

#define class class_in_math_h_causes_problems

#include_next <math.h>

#undef class

#endif /* LIBAV_COMPAT_AIX_MATH_H */
