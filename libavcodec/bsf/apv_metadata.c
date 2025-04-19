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

#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "bsf.h"
#include "bsf_internal.h"
#include "cbs.h"
#include "cbs_bsf.h"
#include "cbs_apv.h"

typedef struct APVMetadataContext {
    CBSBSFContext common;

    int color_primaries;
    int transfer_characteristics;
    int matrix_coefficients;
    int full_range_flag;
} APVMetadataContext;


static int apv_metadata_update_frame_header(AVBSFContext *bsf,
                                            APVRawFrameHeader *hdr)
{
    APVMetadataContext *ctx = bsf->priv_data;

    if (ctx->color_primaries >= 0          ||
        ctx->transfer_characteristics >= 0 ||
        ctx->matrix_coefficients >= 0      ||
        ctx->full_range_flag >= 0) {
        hdr->color_description_present_flag = 1;

        if (ctx->color_primaries >= 0)
            hdr->color_primaries = ctx->color_primaries;
        if (ctx->transfer_characteristics >= 0)
            hdr->transfer_characteristics = ctx->transfer_characteristics;
        if (ctx->matrix_coefficients >= 0)
            hdr->matrix_coefficients = ctx->matrix_coefficients;
        if (ctx->full_range_flag >= 0)
            hdr->full_range_flag = ctx->full_range_flag;
    }

    return 0;
}

static int apv_metadata_update_fragment(AVBSFContext *bsf, AVPacket *pkt,
                                        CodedBitstreamFragment *frag)
{
    int err, i;

    for (i = 0; i < frag->nb_units; i++) {
        if (frag->units[i].type == APV_PBU_PRIMARY_FRAME) {
            APVRawFrame *pbu = frag->units[i].content;
            err = apv_metadata_update_frame_header(bsf, &pbu->frame_header);
            if (err < 0)
                return err;
        }
    }

    return 0;
}

static const CBSBSFType apv_metadata_type = {
    .codec_id        = AV_CODEC_ID_APV,
    .fragment_name   = "access unit",
    .unit_name       = "PBU",
    .update_fragment = &apv_metadata_update_fragment,
};

static int apv_metadata_init(AVBSFContext *bsf)
{
    return ff_cbs_bsf_generic_init(bsf, &apv_metadata_type);
}

#define OFFSET(x) offsetof(APVMetadataContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption apv_metadata_options[] = {
    { "color_primaries", "Set color primaries (section 5.3.5)",
        OFFSET(color_primaries), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 255, FLAGS },
    { "transfer_characteristics", "Set transfer characteristics (section 5.3.5)",
        OFFSET(transfer_characteristics), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 255, FLAGS },
    { "matrix_coefficients", "Set matrix coefficients (section 5.3.5)",
        OFFSET(matrix_coefficients), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 255, FLAGS },

    { "full_range_flag", "Set full range flag flag (section 5.3.5)",
        OFFSET(full_range_flag), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 1, FLAGS, .unit = "cr" },
    { "tv", "TV (limited) range", 0, AV_OPT_TYPE_CONST,
        { .i64 = 0 }, .flags = FLAGS, .unit = "cr" },
    { "pc", "PC (full) range",    0, AV_OPT_TYPE_CONST,
        { .i64 = 1 }, .flags = FLAGS, .unit = "cr" },

    { NULL }
};

static const AVClass apv_metadata_class = {
    .class_name = "apv_metadata_bsf",
    .item_name  = av_default_item_name,
    .option     = apv_metadata_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const enum AVCodecID apv_metadata_codec_ids[] = {
    AV_CODEC_ID_APV, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_apv_metadata_bsf = {
    .p.name         = "apv_metadata",
    .p.codec_ids    = apv_metadata_codec_ids,
    .p.priv_class   = &apv_metadata_class,
    .priv_data_size = sizeof(APVMetadataContext),
    .init           = &apv_metadata_init,
    .close          = &ff_cbs_bsf_generic_close,
    .filter         = &ff_cbs_bsf_generic_filter,
};
