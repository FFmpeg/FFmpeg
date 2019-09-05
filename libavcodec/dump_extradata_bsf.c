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

#include <string.h>

#include "avcodec.h"
#include "bsf.h"

#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

enum DumpFreq {
    DUMP_FREQ_KEYFRAME,
    DUMP_FREQ_ALL,
};

typedef struct DumpExtradataContext {
    const AVClass *class;
    AVPacket pkt;
    int freq;
} DumpExtradataContext;

static int dump_extradata(AVBSFContext *ctx, AVPacket *out)
{
    DumpExtradataContext *s = ctx->priv_data;
    AVPacket *in = &s->pkt;
    int ret = 0;

    ret = ff_bsf_get_packet_ref(ctx, in);
    if (ret < 0)
        return ret;

    if (ctx->par_in->extradata &&
        (s->freq == DUMP_FREQ_ALL ||
         (s->freq == DUMP_FREQ_KEYFRAME && in->flags & AV_PKT_FLAG_KEY)) &&
         in->size >= ctx->par_in->extradata_size &&
         memcmp(in->data, ctx->par_in->extradata, ctx->par_in->extradata_size)) {
        if (in->size >= INT_MAX - ctx->par_in->extradata_size) {
            ret = AVERROR(ERANGE);
            goto fail;
        }

        ret = av_new_packet(out, in->size + ctx->par_in->extradata_size);
        if (ret < 0)
            goto fail;

        ret = av_packet_copy_props(out, in);
        if (ret < 0) {
            av_packet_unref(out);
            goto fail;
        }

        memcpy(out->data, ctx->par_in->extradata, ctx->par_in->extradata_size);
        memcpy(out->data + ctx->par_in->extradata_size, in->data, in->size);
    } else {
        av_packet_move_ref(out, in);
    }

fail:
    av_packet_unref(in);

    return ret;
}

#define OFFSET(x) offsetof(DumpExtradataContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption options[] = {
    { "freq", "When to dump extradata", OFFSET(freq), AV_OPT_TYPE_INT,
        { .i64 = DUMP_FREQ_KEYFRAME }, DUMP_FREQ_KEYFRAME, DUMP_FREQ_ALL, FLAGS, "freq" },
        { "k",        NULL, 0, AV_OPT_TYPE_CONST, { .i64 = DUMP_FREQ_KEYFRAME }, .flags = FLAGS, .unit = "freq" },
        { "keyframe", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = DUMP_FREQ_KEYFRAME }, .flags = FLAGS, .unit = "freq" },
        { "e",        NULL, 0, AV_OPT_TYPE_CONST, { .i64 = DUMP_FREQ_ALL      }, .flags = FLAGS, .unit = "freq" },
        { "all",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = DUMP_FREQ_ALL      }, .flags = FLAGS, .unit = "freq" },
    { NULL },
};

static const AVClass dump_extradata_class = {
    .class_name = "dump_extradata bsf",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVBitStreamFilter ff_dump_extradata_bsf = {
    .name           = "dump_extra",
    .priv_data_size = sizeof(DumpExtradataContext),
    .priv_class     = &dump_extradata_class,
    .filter         = dump_extradata,
};
