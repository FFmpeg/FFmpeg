/* tick.h - Compute successive integer multiples of a rational
 * number without long-term rounding error.
 * (c)2002 by Lennert Buytenhek <buytenh@gnu.org>
 * File licensed under the GPL, see http://www.fsf.org/ for more info.
 * Dedicated to Marija Kulikova.
 */

#include "avcodec.h"

typedef struct Ticker {
    int value;
    int inrate;
    int outrate;
    int div;
    int mod;
} Ticker;

extern void ticker_init(Ticker *tick, INT64 inrate, INT64 outrate);

static inline int ticker_tick(Ticker *tick, int num)
{
    int n = num * tick->div;

    tick->value += num * tick->mod;
#if 1
    if (tick->value > 0) {
        n += (tick->value / tick->inrate);
        tick->value = tick->value % tick->inrate;
        if (tick->value > 0) {
            tick->value -= tick->inrate;
            n++;
        }
    }
#else
    while (tick->value > 0) {
        tick->value -= tick->inrate;
        n++;
    }
#endif
    return n;
}

static inline INT64 ticker_abs(Ticker *tick, int num)
{
    INT64 n = (INT64) num * tick->div;
    INT64 value = (INT64) num * tick->mod;

    if (value > 0) {
        n += (value / tick->inrate);
        value = value % tick->inrate;
        if (value > 0) {
            /* value -= tick->inrate; */
            n++;
        }
    }
    return n;
}
