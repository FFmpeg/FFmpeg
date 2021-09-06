/*
 * MQ-coder encoder
 * Copyright (c) 2007 Kamil Nowosad
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

/**
 * MQ-coder encoder
 * @file
 * @author Kamil Nowosad
 */

#include "libavutil/avassert.h"
#include "mqc.h"

static void byteout(MqcState *mqc)
{
retry:
    if (*mqc->bp == 0xff){
        mqc->bp++;
        *mqc->bp = mqc->c >> 20;
        mqc->c &= 0xfffff;
        mqc->ct = 7;
    } else if ((mqc->c & 0x8000000)){
        (*mqc->bp)++;
        mqc->c &= 0x7ffffff;
        goto retry;
    } else{
        mqc->bp++;
        *mqc->bp = mqc->c >> 19;
        mqc->c &= 0x7ffff;
        mqc->ct = 8;
    }
}

static void renorme(MqcState *mqc)
{
    do{
        mqc->a += mqc->a;
        mqc->c += mqc->c;
        if (!--mqc->ct)
            byteout(mqc);
    } while (!(mqc->a & 0x8000));
}

static void setbits(MqcState *mqc)
{
    int tmp = mqc->c + mqc->a;
    mqc->c |= 0xffff;
    if (mqc->c >= tmp)
        mqc->c -= 0x8000;
}

void ff_mqc_initenc(MqcState *mqc, uint8_t *bp)
{
    ff_mqc_init_contexts(mqc);
    mqc->a = 0x8000;
    mqc->c = 0;
    mqc->bp = bp-1;
    mqc->bpstart = bp;
    mqc->ct = 12 + (*mqc->bp == 0xff);
}

void ff_mqc_encode(MqcState *mqc, uint8_t *cxstate, int d)
{
    int qe;

    qe = ff_mqc_qe[*cxstate];
    mqc->a -= qe;
    if ((*cxstate & 1) == d){
        if (!(mqc->a & 0x8000)){
            if (mqc->a < qe)
                mqc->a = qe;
            else
                mqc->c += qe;
            *cxstate = ff_mqc_nmps[*cxstate];
            renorme(mqc);
        } else
            mqc->c += qe;
    } else{
        if (mqc->a < qe)
            mqc->c += qe;
        else
            mqc->a = qe;
        *cxstate = ff_mqc_nlps[*cxstate];
        renorme(mqc);
    }
}

static int mqc_flush(MqcState *mqc)
{
    setbits(mqc);
    mqc->c = mqc->c << mqc->ct;
    byteout(mqc);
    mqc->c = mqc->c << mqc->ct;
    byteout(mqc);
    if (*mqc->bp != 0xff)
        mqc->bp++;
    return mqc->bp - mqc->bpstart;
}

int ff_mqc_flush_to(MqcState *mqc, uint8_t *dst, int *dst_len)
{
    MqcState mqc2 = *mqc;
    mqc2.bpstart=
    mqc2.bp = dst;
    *mqc2.bp = *mqc->bp;
    mqc_flush(&mqc2);
    *dst_len = mqc2.bp - dst;
    if (mqc->bp < mqc->bpstart) {
        av_assert1(mqc->bpstart - mqc->bp == 1);
        av_assert1(*dst_len > 0);
        av_assert1(mqc->bp[0] == 0 && dst[0] == 0);
        (*dst_len) --;
        memmove(dst, dst+1, *dst_len);
        return mqc->bp - mqc->bpstart + 1 + *dst_len;
    }
    return mqc->bp - mqc->bpstart + *dst_len;
}
