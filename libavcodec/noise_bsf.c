/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#include <stdlib.h>
#include <string.h>

#include "avcodec.h"
#include "bsf.h"

#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

typedef struct NoiseContext {
    const AVClass *class;
    int amount;
    int dropamount;
    unsigned int state;
} NoiseContext;

static int noise(AVBSFContext *ctx, AVPacket *pkt)
{
    NoiseContext *s = ctx->priv_data;
    int amount = s->amount > 0 ? s->amount : (s->state % 10001 + 1);
    int i, ret = 0;

    if (amount <= 0)
        return AVERROR(EINVAL);

    ret = ff_bsf_get_packet_ref(ctx, pkt);
    if (ret < 0)
        return ret;

    if (s->dropamount > 0 && s->state % s->dropamount == 0) {
        s->state++;
        av_packet_unref(pkt);
        return AVERROR(EAGAIN);
    }

    ret = av_packet_make_writable(pkt);
    if (ret < 0)
        goto fail;

    for (i = 0; i < pkt->size; i++) {
        s->state += pkt->data[i] + 1;
        if (s->state % amount == 0)
            pkt->data[i] = s->state;
    }
fail:
    if (ret < 0)
        av_packet_unref(pkt);

    return ret;
}

#define OFFSET(x) offsetof(NoiseContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption options[] = {
    { "amount", NULL, OFFSET(amount), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { "dropamount", NULL, OFFSET(dropamount), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { NULL },
};

static const AVClass noise_class = {
    .class_name = "noise",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVBitStreamFilter ff_noise_bsf = {
    .name           = "noise",
    .priv_data_size = sizeof(NoiseContext),
    .priv_class     = &noise_class,
    .filter         = noise,
};
