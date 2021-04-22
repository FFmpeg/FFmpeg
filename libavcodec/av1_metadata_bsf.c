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
#include "cbs.h"
#include "cbs_bsf.h"
#include "cbs_av1.h"

typedef struct AV1MetadataContext {
    CBSBSFContext common;

    int td;
    AV1RawOBU td_obu;

    int color_primaries;
    int transfer_characteristics;
    int matrix_coefficients;

    int color_range;
    int chroma_sample_position;

    AVRational tick_rate;
    int num_ticks_per_picture;

    int delete_padding;
} AV1MetadataContext;


static int av1_metadata_update_sequence_header(AVBSFContext *bsf,
                                               AV1RawSequenceHeader *seq)
{
    AV1MetadataContext *ctx = bsf->priv_data;
    AV1RawColorConfig  *clc = &seq->color_config;
    AV1RawTimingInfo   *tim = &seq->timing_info;

    if (ctx->color_primaries >= 0          ||
        ctx->transfer_characteristics >= 0 ||
        ctx->matrix_coefficients >= 0) {
        clc->color_description_present_flag = 1;

        if (ctx->color_primaries >= 0)
            clc->color_primaries = ctx->color_primaries;
        if (ctx->transfer_characteristics >= 0)
            clc->transfer_characteristics = ctx->transfer_characteristics;
        if (ctx->matrix_coefficients >= 0)
            clc->matrix_coefficients = ctx->matrix_coefficients;
    }

    if (ctx->color_range >= 0) {
        if (clc->color_primaries          == AVCOL_PRI_BT709        &&
            clc->transfer_characteristics == AVCOL_TRC_IEC61966_2_1 &&
            clc->matrix_coefficients      == AVCOL_SPC_RGB) {
            av_log(bsf, AV_LOG_WARNING, "Warning: color_range cannot be set "
                   "on RGB streams encoded in BT.709 sRGB.\n");
        } else {
            clc->color_range = ctx->color_range;
        }
    }

    if (ctx->chroma_sample_position >= 0) {
        if (clc->mono_chrome || !clc->subsampling_x || !clc->subsampling_y) {
            av_log(bsf, AV_LOG_WARNING, "Warning: chroma_sample_position "
                   "can only be set for 4:2:0 streams.\n");
        } else {
            clc->chroma_sample_position = ctx->chroma_sample_position;
        }
    }

    if (ctx->tick_rate.num && ctx->tick_rate.den) {
        int num, den;

        av_reduce(&num, &den, ctx->tick_rate.num, ctx->tick_rate.den,
                  UINT32_MAX > INT_MAX ? UINT32_MAX : INT_MAX);

        tim->time_scale                = num;
        tim->num_units_in_display_tick = den;
        seq->timing_info_present_flag  = 1;

        if (ctx->num_ticks_per_picture > 0) {
            tim->equal_picture_interval = 1;
            tim->num_ticks_per_picture_minus_1 =
                ctx->num_ticks_per_picture - 1;
        }
    }

    return 0;
}

static int av1_metadata_update_fragment(AVBSFContext *bsf, AVPacket *pkt,
                                        CodedBitstreamFragment *frag)
{
    AV1MetadataContext *ctx = bsf->priv_data;
    int err, i;

    for (i = 0; i < frag->nb_units; i++) {
        if (frag->units[i].type == AV1_OBU_SEQUENCE_HEADER) {
            AV1RawOBU *obu = frag->units[i].content;
            err = av1_metadata_update_sequence_header(bsf, &obu->obu.sequence_header);
            if (err < 0)
                return err;
        }
    }

    // If a Temporal Delimiter is present, it must be the first OBU.
    if (frag->nb_units && frag->units[0].type == AV1_OBU_TEMPORAL_DELIMITER) {
        if (ctx->td == BSF_ELEMENT_REMOVE)
            ff_cbs_delete_unit(frag, 0);
    } else if (pkt && ctx->td == BSF_ELEMENT_INSERT) {
        err = ff_cbs_insert_unit_content(frag, 0, AV1_OBU_TEMPORAL_DELIMITER,
                                         &ctx->td_obu, NULL);
        if (err < 0) {
            av_log(bsf, AV_LOG_ERROR, "Failed to insert Temporal Delimiter.\n");
            return err;
        }
    }

    if (ctx->delete_padding) {
        for (i = frag->nb_units - 1; i >= 0; i--) {
            if (frag->units[i].type == AV1_OBU_PADDING)
                ff_cbs_delete_unit(frag, i);
        }
    }

    return 0;
}

static const CBSBSFType av1_metadata_type = {
    .codec_id        = AV_CODEC_ID_AV1,
    .fragment_name   = "temporal unit",
    .unit_name       = "OBU",
    .update_fragment = &av1_metadata_update_fragment,
};

static int av1_metadata_init(AVBSFContext *bsf)
{
    AV1MetadataContext *ctx = bsf->priv_data;

    ctx->td_obu = (AV1RawOBU) {
        .header.obu_type = AV1_OBU_TEMPORAL_DELIMITER,
    };

    return ff_cbs_bsf_generic_init(bsf, &av1_metadata_type);
}

#define OFFSET(x) offsetof(AV1MetadataContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption av1_metadata_options[] = {
    BSF_ELEMENT_OPTIONS_PIR("td", "Temporal Delimiter OBU",
                            td, FLAGS),

    { "color_primaries", "Set color primaries (section 6.4.2)",
        OFFSET(color_primaries), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 255, FLAGS },
    { "transfer_characteristics", "Set transfer characteristics (section 6.4.2)",
        OFFSET(transfer_characteristics), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 255, FLAGS },
    { "matrix_coefficients", "Set matrix coefficients (section 6.4.2)",
        OFFSET(matrix_coefficients), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 255, FLAGS },

    { "color_range", "Set color range flag (section 6.4.2)",
        OFFSET(color_range), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 1, FLAGS, "cr" },
    { "tv", "TV (limited) range", 0, AV_OPT_TYPE_CONST,
        { .i64 = 0 }, .flags = FLAGS, .unit = "cr" },
    { "pc", "PC (full) range",    0, AV_OPT_TYPE_CONST,
        { .i64 = 1 }, .flags = FLAGS, .unit = "cr" },

    { "chroma_sample_position", "Set chroma sample position (section 6.4.2)",
        OFFSET(chroma_sample_position), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 3, FLAGS, "csp" },
    { "unknown",   "Unknown chroma sample position",  0, AV_OPT_TYPE_CONST,
        { .i64 = AV1_CSP_UNKNOWN },   .flags = FLAGS, .unit = "csp" },
    { "vertical",  "Left chroma sample position",     0, AV_OPT_TYPE_CONST,
        { .i64 = AV1_CSP_VERTICAL },  .flags = FLAGS, .unit = "csp" },
    { "colocated", "Top-left chroma sample position", 0, AV_OPT_TYPE_CONST,
        { .i64 = AV1_CSP_COLOCATED }, .flags = FLAGS, .unit = "csp" },

    { "tick_rate", "Set display tick rate (num_units_in_display_tick / time_scale)",
        OFFSET(tick_rate), AV_OPT_TYPE_RATIONAL,
        { .dbl = 0.0 }, 0, UINT_MAX, FLAGS },
    { "num_ticks_per_picture", "Set display ticks per picture for CFR streams",
        OFFSET(num_ticks_per_picture), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, INT_MAX, FLAGS },

    { "delete_padding", "Delete all Padding OBUs",
        OFFSET(delete_padding), AV_OPT_TYPE_BOOL,
        { .i64 = 0 }, 0, 1, FLAGS},

    { NULL }
};

static const AVClass av1_metadata_class = {
    .class_name = "av1_metadata_bsf",
    .item_name  = av_default_item_name,
    .option     = av1_metadata_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const enum AVCodecID av1_metadata_codec_ids[] = {
    AV_CODEC_ID_AV1, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_av1_metadata_bsf = {
    .name           = "av1_metadata",
    .priv_data_size = sizeof(AV1MetadataContext),
    .priv_class     = &av1_metadata_class,
    .init           = &av1_metadata_init,
    .close          = &ff_cbs_bsf_generic_close,
    .filter         = &ff_cbs_bsf_generic_filter,
    .codec_ids      = av1_metadata_codec_ids,
};
