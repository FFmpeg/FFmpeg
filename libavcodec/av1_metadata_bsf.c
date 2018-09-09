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
#include "cbs_av1.h"

typedef struct AV1MetadataContext {
    const AVClass *class;

    CodedBitstreamContext *cbc;
    CodedBitstreamFragment access_unit;

    int color_primaries;
    int transfer_characteristics;
    int matrix_coefficients;

    int color_range;
    int chroma_sample_position;

    AVRational tick_rate;
    int num_ticks_per_picture;
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
        if (!clc->color_description_present_flag) {
            clc->color_description_present_flag = 1;
            clc->color_primaries          = AVCOL_PRI_UNSPECIFIED;
            clc->transfer_characteristics = AVCOL_TRC_UNSPECIFIED;
            clc->matrix_coefficients      = AVCOL_SPC_UNSPECIFIED;
        }

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
        if (clc->mono_chrome) {
            av_log(bsf, AV_LOG_WARNING, "Warning: chroma_sample_position "
                   "is not meaningful for monochrome streams.\n");
        } else if (clc->subsampling_x == 0 &&
                   clc->subsampling_y == 0) {
            av_log(bsf, AV_LOG_WARNING, "Warning: chroma_sample_position "
                   "is not meaningful for non-chroma-subsampled streams.\n");
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

static int av1_metadata_filter(AVBSFContext *bsf, AVPacket *out)
{
    AV1MetadataContext *ctx = bsf->priv_data;
    AVPacket *in = NULL;
    CodedBitstreamFragment *frag = &ctx->access_unit;
    AV1RawOBU *obu;
    int err, i;

    err = ff_bsf_get_packet(bsf, &in);
    if (err < 0)
        return err;

    err = ff_cbs_read_packet(ctx->cbc, frag, in);
    if (err < 0) {
        av_log(bsf, AV_LOG_ERROR, "Failed to read packet.\n");
        goto fail;
    }

    for (i = 0; i < frag->nb_units; i++) {
        if (frag->units[i].type == AV1_OBU_SEQUENCE_HEADER) {
            obu = frag->units[i].content;
            err = av1_metadata_update_sequence_header(bsf, &obu->obu.sequence_header);
            if (err < 0)
                goto fail;
        }
    }

    err = ff_cbs_write_packet(ctx->cbc, out, frag);
    if (err < 0) {
        av_log(bsf, AV_LOG_ERROR, "Failed to write packet.\n");
        goto fail;
    }

    err = av_packet_copy_props(out, in);
    if (err < 0)
        goto fail;

    err = 0;
fail:
    ff_cbs_fragment_uninit(ctx->cbc, frag);

    if (err < 0)
        av_packet_unref(out);
    av_packet_free(&in);

    return err;
}

static int av1_metadata_init(AVBSFContext *bsf)
{
    AV1MetadataContext *ctx = bsf->priv_data;
    CodedBitstreamFragment *frag = &ctx->access_unit;
    AV1RawOBU *obu;
    int err, i;

    err = ff_cbs_init(&ctx->cbc, AV_CODEC_ID_AV1, bsf);
    if (err < 0)
        return err;

    if (bsf->par_in->extradata) {
        err = ff_cbs_read_extradata(ctx->cbc, frag, bsf->par_in);
        if (err < 0) {
            av_log(bsf, AV_LOG_ERROR, "Failed to read extradata.\n");
            goto fail;
        }

        for (i = 0; i < frag->nb_units; i++) {
            if (frag->units[i].type == AV1_OBU_SEQUENCE_HEADER) {
                obu = frag->units[i].content;
                err = av1_metadata_update_sequence_header(bsf, &obu->obu.sequence_header);
                if (err < 0)
                    goto fail;
            }
        }

        err = ff_cbs_write_extradata(ctx->cbc, bsf->par_out, frag);
        if (err < 0) {
            av_log(bsf, AV_LOG_ERROR, "Failed to write extradata.\n");
            goto fail;
        }
    }

    err = 0;
fail:
    ff_cbs_fragment_uninit(ctx->cbc, frag);
    return err;
}

static void av1_metadata_close(AVBSFContext *bsf)
{
    AV1MetadataContext *ctx = bsf->priv_data;
    ff_cbs_close(&ctx->cbc);
}

#define OFFSET(x) offsetof(AV1MetadataContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption av1_metadata_options[] = {
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
    .close          = &av1_metadata_close,
    .filter         = &av1_metadata_filter,
    .codec_ids      = av1_metadata_codec_ids,
};
