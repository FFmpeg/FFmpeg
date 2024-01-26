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
#include "cbs_mpeg2.h"
#include "mpeg12.h"

typedef struct MPEG2MetadataContext {
    CBSBSFContext common;

    MPEG2RawExtensionData sequence_display_extension;

    AVRational display_aspect_ratio;

    AVRational frame_rate;

    int video_format;
    int colour_primaries;
    int transfer_characteristics;
    int matrix_coefficients;

    int mpeg1_warned;
} MPEG2MetadataContext;


static int mpeg2_metadata_update_fragment(AVBSFContext *bsf,
                                          AVPacket *pkt,
                                          CodedBitstreamFragment *frag)
{
    MPEG2MetadataContext             *ctx = bsf->priv_data;
    MPEG2RawSequenceHeader            *sh = NULL;
    MPEG2RawSequenceExtension         *se = NULL;
    MPEG2RawSequenceDisplayExtension *sde = NULL;
    int i, se_pos;

    for (i = 0; i < frag->nb_units; i++) {
        if (frag->units[i].type == MPEG2_START_SEQUENCE_HEADER) {
            sh = frag->units[i].content;
        } else if (frag->units[i].type == MPEG2_START_EXTENSION) {
            MPEG2RawExtensionData *ext = frag->units[i].content;
            if (ext->extension_start_code_identifier ==
                MPEG2_EXTENSION_SEQUENCE) {
                se = &ext->data.sequence;
                se_pos = i;
            } else if (ext->extension_start_code_identifier ==
                MPEG2_EXTENSION_SEQUENCE_DISPLAY) {
                sde = &ext->data.sequence_display;
            }
        }
    }

    if (!sh || !se) {
        // No sequence header and sequence extension: not an MPEG-2 video
        // sequence.
        if (sh && !ctx->mpeg1_warned) {
            av_log(bsf, AV_LOG_WARNING, "Stream contains a sequence "
                   "header but not a sequence extension: maybe it's "
                   "actually MPEG-1?\n");
            ctx->mpeg1_warned = 1;
        }
        return 0;
    }

    if (ctx->display_aspect_ratio.num && ctx->display_aspect_ratio.den) {
        int num, den;

        av_reduce(&num, &den, ctx->display_aspect_ratio.num,
                  ctx->display_aspect_ratio.den, 65535);

        if (num == 4 && den == 3)
            sh->aspect_ratio_information = 2;
        else if (num == 16 && den == 9)
            sh->aspect_ratio_information = 3;
        else if (num == 221 && den == 100)
            sh->aspect_ratio_information = 4;
        else
            sh->aspect_ratio_information = 1;
    }

    if (ctx->frame_rate.num && ctx->frame_rate.den) {
        int code, ext_n, ext_d;

        ff_mpeg12_find_best_frame_rate(ctx->frame_rate,
                                       &code, &ext_n, &ext_d, 0);

        sh->frame_rate_code        = code;
        se->frame_rate_extension_n = ext_n;
        se->frame_rate_extension_d = ext_d;
    }

    if (ctx->video_format             >= 0 ||
        ctx->colour_primaries         >= 0 ||
        ctx->transfer_characteristics >= 0 ||
        ctx->matrix_coefficients      >= 0) {
        if (!sde) {
            int err;
            ctx->sequence_display_extension.extension_start_code =
                MPEG2_START_EXTENSION;
            ctx->sequence_display_extension.extension_start_code_identifier =
                MPEG2_EXTENSION_SEQUENCE_DISPLAY;
            sde = &ctx->sequence_display_extension.data.sequence_display;

            *sde = (MPEG2RawSequenceDisplayExtension) {
                .video_format = 5,

                .colour_description       = 0,
                .colour_primaries         = 2,
                .transfer_characteristics = 2,
                .matrix_coefficients      = 2,

                .display_horizontal_size =
                    se->horizontal_size_extension << 12 | sh->horizontal_size_value,
                .display_vertical_size =
                    se->vertical_size_extension << 12 | sh->vertical_size_value,
            };

            err = ff_cbs_insert_unit_content(frag, se_pos + 1,
                                             MPEG2_START_EXTENSION,
                                             &ctx->sequence_display_extension,
                                             NULL);
            if (err < 0) {
                av_log(bsf, AV_LOG_ERROR, "Failed to insert new sequence "
                       "display extension.\n");
                return err;
            }
        }

        if (ctx->video_format >= 0)
            sde->video_format = ctx->video_format;

        if (ctx->colour_primaries         >= 0 ||
            ctx->transfer_characteristics >= 0 ||
            ctx->matrix_coefficients      >= 0) {
            sde->colour_description = 1;

            if (ctx->colour_primaries >= 0)
                sde->colour_primaries = ctx->colour_primaries;

            if (ctx->transfer_characteristics >= 0)
                sde->transfer_characteristics = ctx->transfer_characteristics;

            if (ctx->matrix_coefficients >= 0)
                sde->matrix_coefficients = ctx->matrix_coefficients;
        }
    }

    return 0;
}

static const CBSBSFType mpeg2_metadata_type = {
    .codec_id        = AV_CODEC_ID_MPEG2VIDEO,
    .fragment_name   = "frame",
    .unit_name       = "start code",
    .update_fragment = &mpeg2_metadata_update_fragment,
};

static int mpeg2_metadata_init(AVBSFContext *bsf)
{
    MPEG2MetadataContext *ctx = bsf->priv_data;

#define VALIDITY_CHECK(name) do { \
        if (!ctx->name) { \
            av_log(bsf, AV_LOG_ERROR, "The value 0 for %s is " \
                                      "forbidden.\n", #name); \
            return AVERROR(EINVAL); \
        } \
    } while (0)
    VALIDITY_CHECK(colour_primaries);
    VALIDITY_CHECK(transfer_characteristics);
    VALIDITY_CHECK(matrix_coefficients);
#undef VALIDITY_CHECK

    return ff_cbs_bsf_generic_init(bsf, &mpeg2_metadata_type);
}

#define OFFSET(x) offsetof(MPEG2MetadataContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption mpeg2_metadata_options[] = {
    { "display_aspect_ratio", "Set display aspect ratio (table 6-3)",
        OFFSET(display_aspect_ratio), AV_OPT_TYPE_RATIONAL,
        { .dbl = 0.0 }, 0, 65535, FLAGS },

    { "frame_rate", "Set frame rate",
        OFFSET(frame_rate), AV_OPT_TYPE_RATIONAL,
        { .dbl = 0.0 }, 0, UINT_MAX, FLAGS },

    { "video_format", "Set video format (table 6-6)",
        OFFSET(video_format), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 7, FLAGS },
    { "colour_primaries", "Set colour primaries (table 6-7)",
        OFFSET(colour_primaries), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 255, FLAGS },
    { "transfer_characteristics", "Set transfer characteristics (table 6-8)",
        OFFSET(transfer_characteristics), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 255, FLAGS },
    { "matrix_coefficients", "Set matrix coefficients (table 6-9)",
        OFFSET(matrix_coefficients), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 255, FLAGS },

    { NULL }
};

static const AVClass mpeg2_metadata_class = {
    .class_name = "mpeg2_metadata_bsf",
    .item_name  = av_default_item_name,
    .option     = mpeg2_metadata_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const enum AVCodecID mpeg2_metadata_codec_ids[] = {
    AV_CODEC_ID_MPEG2VIDEO, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_mpeg2_metadata_bsf = {
    .p.name         = "mpeg2_metadata",
    .p.codec_ids    = mpeg2_metadata_codec_ids,
    .p.priv_class   = &mpeg2_metadata_class,
    .priv_data_size = sizeof(MPEG2MetadataContext),
    .init           = &mpeg2_metadata_init,
    .close          = &ff_cbs_bsf_generic_close,
    .filter         = &ff_cbs_bsf_generic_filter,
};
