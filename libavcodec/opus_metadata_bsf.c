/*
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

#include "bsf.h"
#include "bsf_internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"

typedef struct OpusBSFContext {
    const AVClass *class;
    int gain;
} OpusBSFContext;

static int opus_metadata_init(AVBSFContext *bsfc)
{
    OpusBSFContext *s = bsfc->priv_data;

    if (bsfc->par_out->extradata_size < 19)
        return AVERROR_INVALIDDATA;

    AV_WL16(bsfc->par_out->extradata + 16, s->gain);

    return 0;
}

#define OFFSET(x) offsetof(OpusBSFContext, x)
#define FLAGS (AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption opus_metadata_options[] = {
    { "gain", "Gain, actual amplification is pow(10, gain/(20.0*256))", OFFSET(gain),
      AV_OPT_TYPE_INT, { .i64 = 0 }, -(INT16_MAX + 1), INT16_MAX, .flags = FLAGS },

    { NULL },
};

static const AVClass opus_metadata_class = {
    .class_name = "opus_metadata_bsf",
    .item_name  = av_default_item_name,
    .option     = opus_metadata_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_OPUS, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_opus_metadata_bsf = {
    .name           = "opus_metadata",
    .priv_data_size = sizeof(OpusBSFContext),
    .priv_class     = &opus_metadata_class,
    .init           = &opus_metadata_init,
    .filter         = &ff_bsf_get_packet_ref,
    .codec_ids      = codec_ids,
};
