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

extern inline int ticker_tick(Ticker *tick, int num)
{
    int n = num * tick->div;

    tick->value += num * tick->mod;
    while (tick->value > 0) {
        tick->value -= tick->inrate;
        n++;
    }

    return n;
}
