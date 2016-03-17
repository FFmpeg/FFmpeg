/*
 * Range coder
 * Copyright (c) 2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Range coder.
 * based upon
 *    "Range encoding: an algorithm for removing redundancy from a digitised
 *                     message.
 *     G. N. N. Martin                  Presented in March 1979 to the Video &
 *                                      Data Recording Conference,
 *     IBM UK Scientific Center         held in Southampton July 24-27 1979."
 */

#include <string.h>

#include "libavutil/attributes.h"
#include "libavutil/intreadwrite.h"

#include "avcodec.h"
#include "rangecoder.h"

av_cold void ff_init_range_encoder(RangeCoder *c, uint8_t *buf, int buf_size)
{
    c->bytestream_start  =
    c->bytestream        = buf;
    c->bytestream_end    = buf + buf_size;
    c->low               = 0;
    c->range             = 0xFF00;
    c->outstanding_count = 0;
    c->outstanding_byte  = -1;
}

av_cold void ff_init_range_decoder(RangeCoder *c, const uint8_t *buf,
                                   int buf_size)
{
    /* cast to avoid compiler warning */
    ff_init_range_encoder(c, (uint8_t *)buf, buf_size);

    c->low         = AV_RB16(c->bytestream);
    c->bytestream += 2;
}

void ff_build_rac_states(RangeCoder *c, int factor, int max_p)
{
    const int64_t one = 1LL << 32;
    int64_t p;
    int last_p8, p8, i;

    memset(c->zero_state, 0, sizeof(c->zero_state));
    memset(c->one_state, 0, sizeof(c->one_state));

    last_p8 = 0;
    p       = one / 2;
    for (i = 0; i < 128; i++) {
        p8 = (256 * p + one / 2) >> 32; // FIXME: try without the one
        if (p8 <= last_p8)
            p8 = last_p8 + 1;
        if (last_p8 && last_p8 < 256 && p8 <= max_p)
            c->one_state[last_p8] = p8;

        p      += ((one - p) * factor + one / 2) >> 32;
        last_p8 = p8;
    }

    for (i = 256 - max_p; i <= max_p; i++) {
        if (c->one_state[i])
            continue;

        p  = (i * one + 128) >> 8;
        p += ((one - p) * factor + one / 2) >> 32;
        p8 = (256 * p + one / 2) >> 32; // FIXME: try without the one
        if (p8 <= i)
            p8 = i + 1;
        if (p8 > max_p)
            p8 = max_p;
        c->one_state[i] = p8;
    }

    for (i = 1; i < 255; i++)
        c->zero_state[i] = 256 - c->one_state[256 - i];
}

/* Return the number of bytes written. */
int ff_rac_terminate(RangeCoder *c)
{
    c->range = 0xFF;
    c->low  += 0xFF;
    renorm_encoder(c);
    c->range = 0xFF;
    renorm_encoder(c);

    assert(c->low == 0);
    assert(c->range >= 0x100);

    return c->bytestream - c->bytestream_start;
}
