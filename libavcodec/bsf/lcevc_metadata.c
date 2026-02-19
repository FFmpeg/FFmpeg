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
#include "cbs_lcevc.h"
#include "lcevc.h"

typedef struct LCEVCMetadataContext {
    CBSBSFContext common;

    int overscan_appropriate_flag;

    int video_format;
    int video_full_range_flag;
    int colour_primaries;
    int transfer_characteristics;
    int matrix_coefficients;

    int chroma_sample_loc_type;

    LCEVCRawAdditionalInfo ai;

    int delete_filler;
} LCEVCMetadataContext;

static int lcevc_metadata_handle_vui(AVBSFContext *bsf,
                                     CodedBitstreamFragment *au)
{
    LCEVCMetadataContext *ctx = bsf->priv_data;
    LCEVCRawProcessBlock *block = NULL;
    LCEVCRawAdditionalInfo *ai = &ctx->ai;
    LCEVCRawVUI *vui = &ai->vui;
    int position, err;

    position = ff_cbs_lcevc_find_process_block(ctx->common.output, au,
                                               LCEVC_PAYLOAD_TYPE_GLOBAL_CONFIG,
                                               &block);
    if (position < 0)
        return 0;

    memset(ai, 0, sizeof(*ai));
    ai->additional_info_type = LCEVC_ADDITIONAL_INFO_TYPE_VUI;

    if (ctx->overscan_appropriate_flag >= 0) {
        vui->overscan_info_present_flag = 1;
        vui->overscan_appropriate_flag = ctx->overscan_appropriate_flag;
    }

    if (ctx->video_format >= 0) {
        vui->video_signal_type_present_flag = 1;
        vui->video_format = ctx->video_format;
    } else
        vui->video_format = 5;

    if (ctx->video_full_range_flag >= 0) {
        vui->video_signal_type_present_flag = 1;
        vui->video_full_range_flag = ctx->video_full_range_flag;
    }

    if (ctx->colour_primaries >= 0) {
        vui->video_signal_type_present_flag = vui->colour_description_present_flag = 1;
        vui->colour_primaries = ctx->colour_primaries;
    } else
        vui->colour_primaries = 2;
    if (ctx->transfer_characteristics >= 0) {
        vui->video_signal_type_present_flag = vui->colour_description_present_flag = 1;
        vui->transfer_characteristics = ctx->transfer_characteristics;
    } else
        vui->transfer_characteristics = 2;
    if (ctx->matrix_coefficients >= 0) {
        vui->video_signal_type_present_flag = vui->colour_description_present_flag = 1;
        vui->matrix_coefficients = ctx->matrix_coefficients;
    } else
        vui->matrix_coefficients = 2;

    if (ctx->chroma_sample_loc_type >= 0) {
        vui->chroma_loc_info_present_flag = 1;
        vui->chroma_sample_loc_type_top_field = ctx->chroma_sample_loc_type;
        vui->chroma_sample_loc_type_top_field = ctx->chroma_sample_loc_type;
    }

    err = ff_cbs_lcevc_add_process_block(ctx->common.output, au, position,
                                         LCEVC_PAYLOAD_TYPE_ADDITIONAL_INFO,
                                         ai, NULL);
    if (err < 0)
        return err;

    return 0;
}

static int lcevc_metadata_update_fragment(AVBSFContext *bsf, AVPacket *pkt,
                                          CodedBitstreamFragment *au)
{
    LCEVCMetadataContext *ctx = bsf->priv_data;
    int err;

    if (ctx->overscan_appropriate_flag >= 0 || ctx->video_format >= 0 ||
        ctx->video_full_range_flag >= 0 || ctx->colour_primaries >= 0 ||
        ctx->transfer_characteristics >= 0 || ctx->matrix_coefficients >= 0 ||
        ctx->chroma_sample_loc_type >= 0) {
        err = lcevc_metadata_handle_vui(bsf, au);
        if (err < 0)
            return err;
    }

    if (ctx->delete_filler) {
        for (int i = 0; i < au->nb_units; i++) {
            if (au->units[i].type == LCEVC_NON_IDR_NUT ||
                au->units[i].type == LCEVC_IDR_NUT) {
                ff_cbs_lcevc_delete_process_block_type(ctx->common.output, au,
                                                       LCEVC_PAYLOAD_TYPE_FILLER);
            }
        }
    }

    return 0;
}

static const CBSBSFType lcevc_metadata_type = {
    .codec_id        = AV_CODEC_ID_LCEVC,
    .fragment_name   = "access unit",
    .unit_name       = "NAL unit",
    .update_fragment = &lcevc_metadata_update_fragment,
};

static int lcevc_metadata_init(AVBSFContext *bsf)
{
    return ff_cbs_bsf_generic_init(bsf, &lcevc_metadata_type);
}

#define OFFSET(x) offsetof(LCEVCMetadataContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption lcevc_metadata_options[] = {
    { "overscan_appropriate_flag", "Set VUI overscan appropriate flag",
        OFFSET(overscan_appropriate_flag), AV_OPT_TYPE_BOOL,
        { .i64 = -1 }, -1, 1, FLAGS },

    { "video_format", "Set video format (table E-2)",
        OFFSET(video_format), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 5, FLAGS},
    { "video_full_range_flag", "Set video full range flag",
        OFFSET(video_full_range_flag), AV_OPT_TYPE_BOOL,
        { .i64 = -1 }, -1, 1, FLAGS },
    { "colour_primaries", "Set colour primaries (table E-3)",
        OFFSET(colour_primaries), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 255, FLAGS },
    { "transfer_characteristics", "Set transfer characteristics (table E-4)",
        OFFSET(transfer_characteristics), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 255, FLAGS },
    { "matrix_coefficients", "Set matrix coefficients (table E-5)",
        OFFSET(matrix_coefficients), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 255, FLAGS },

    { "chroma_sample_loc_type", "Set chroma sample location type (figure E-1)",
        OFFSET(chroma_sample_loc_type), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 5, FLAGS },

    { "delete_filler", "Delete all filler",
        OFFSET(delete_filler), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS},

    { NULL }
};

static const AVClass lcevc_metadata_class = {
    .class_name = "lcevc_metadata_bsf",
    .item_name  = av_default_item_name,
    .option     = lcevc_metadata_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const enum AVCodecID lcevc_metadata_codec_ids[] = {
    AV_CODEC_ID_LCEVC, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_lcevc_metadata_bsf = {
    .p.name         = "lcevc_metadata",
    .p.codec_ids    = lcevc_metadata_codec_ids,
    .p.priv_class   = &lcevc_metadata_class,
    .priv_data_size = sizeof(LCEVCMetadataContext),
    .init           = &lcevc_metadata_init,
    .close          = &ff_cbs_bsf_generic_close,
    .filter         = &ff_cbs_bsf_generic_filter,
};
