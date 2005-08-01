#ifndef INTFLOAT_READWRITE_H
#define INTFLOAT_READWRITE_H

#include "common.h"

double av_int2dbl(int64_t v);
float av_int2flt(int32_t v);
int64_t av_dbl2int(double d);
int32_t av_flt2int(float d);

#endif /* INTFLOAT_READWRITE_H */
