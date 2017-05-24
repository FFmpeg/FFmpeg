/*
 * Direct Stream Digital (DSD) decoder
 * based on BSD licensed dsd2pcm by Sebastian Gesemann
 * Copyright (c) 2009, 2011 Sebastian Gesemann. All rights reserved.
 * Copyright (c) 2014 Peter Ross
 *
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

#include "libavcodec/internal.h"
#include "libavcodec/mathops.h"
#include "avcodec.h"
#include "dsd_tablegen.h"
#include "dsd.h"

static av_cold void dsd_ctables_tableinit(void)
{
    int t, e, m, sign;
    double acc[CTABLES];
    for (e = 0; e < 256; ++e) {
        memset(acc, 0, sizeof(acc));
        for (m = 0; m < 8; ++m) {
            sign = (((e >> (7 - m)) & 1) * 2 - 1);
            for (t = 0; t < CTABLES; ++t)
                acc[t] += sign * htaps[t * 8 + m];
        }
        for (t = 0; t < CTABLES; ++t)
            ctables[CTABLES - 1 - t][e] = acc[t];
    }
}

av_cold void ff_init_dsd_data(void)
{
    static int done = 0;
    if (done)
        return;
    dsd_ctables_tableinit();
    done = 1;
}

void ff_dsd2pcm_translate(DSDContext* s, size_t samples, int lsbf,
                          const unsigned char *src, ptrdiff_t src_stride,
                          float *dst, ptrdiff_t dst_stride)
{
    unsigned pos, i;
    unsigned char* p;
    double sum;

    pos = s->pos;

    while (samples-- > 0) {
        s->buf[pos] = lsbf ? ff_reverse[*src] : *src;
        src += src_stride;

        p = s->buf + ((pos - CTABLES) & FIFOMASK);
        *p = ff_reverse[*p];

        sum = 0.0;
        for (i = 0; i < CTABLES; i++) {
            unsigned char a = s->buf[(pos                   - i) & FIFOMASK];
            unsigned char b = s->buf[(pos - (CTABLES*2 - 1) + i) & FIFOMASK];
            sum += ctables[i][a] + ctables[i][b];
        }

        *dst = (float)sum;
        dst += dst_stride;

        pos = (pos + 1) & FIFOMASK;
    }

    s->pos = pos;
}
