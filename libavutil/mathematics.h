#ifndef MATHEMATICS_H
#define MATHEMATICS_H

#include "rational.h"

enum AVRounding {
    AV_ROUND_ZERO     = 0, ///< round toward zero
    AV_ROUND_INF      = 1, ///< round away from zero
    AV_ROUND_DOWN     = 2, ///< round toward -infinity
    AV_ROUND_UP       = 3, ///< round toward +infinity
    AV_ROUND_NEAR_INF = 5, ///< round to nearest and halfway cases away from zero
};

/**
 * rescale a 64bit integer with rounding to nearest.
 * a simple a*b/c isn't possible as it can overflow
 */
int64_t av_rescale(int64_t a, int64_t b, int64_t c);

/**
 * rescale a 64bit integer with specified rounding.
 * a simple a*b/c isn't possible as it can overflow
 */
int64_t av_rescale_rnd(int64_t a, int64_t b, int64_t c, enum AVRounding);

/**
 * rescale a 64bit integer by 2 rational numbers.
 */
int64_t av_rescale_q(int64_t a, AVRational bq, AVRational cq);

#endif /* MATHEMATICS_H */
