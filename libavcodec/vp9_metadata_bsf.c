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

#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "bsf.h"
#include "cbs.h"
#include "cbs_vp9.h"

typedef struct VP9MetadataContext {
    const AVClass *class;

    CodedBitstreamContext *cbc;
    CodedBitstreamFragment fragment;

    int color_space;
    int color_range;

    int color_warnings;
} VP9MetadataContext;


static int vp9_metadata_filter(AVBSFContext *bsf, AVPacket *pkt)
{
    VP9MetadataContext *ctx = bsf->priv_data;
    CodedBitstreamFragment *frag = &ctx->fragment;
    int err, i;

    err = ff_bsf_get_packet_ref(bsf, pkt);
    if (err < 0)
        return err;

    err = ff_cbs_read_packet(ctx->cbc, frag, pkt);
    if (err < 0) {
        av_log(bsf, AV_LOG_ERROR, "Failed to read packet.\n");
        goto fail;
    }

    for (i = 0; i < frag->nb_units; i++) {
        VP9RawFrame *frame = frag->units[i].content;
        VP9RawFrameHeader *header = &frame->header;
        int profile = (header->profile_high_bit << 1) + header->profile_low_bit;

        if (header->frame_type == VP9_KEY_FRAME ||
            header->intra_only && profile > 0) {
            if (ctx->color_space >= 0) {
                if (!(profile & 1) && ctx->color_space == VP9_CS_RGB) {
                    if (!(ctx->color_warnings & 2)) {
                        av_log(bsf, AV_LOG_WARNING, "Warning: RGB "
                               "incompatible with profiles 0 and 2.\n");
                        ctx->color_warnings |= 2;
                    }
                } else
                    header->color_space = ctx->color_space;
            }

            if (ctx->color_range >= 0)
                header->color_range = ctx->color_range;
            if (header->color_space == VP9_CS_RGB) {
                if (!(ctx->color_warnings & 1) && !header->color_range) {
                    av_log(bsf, AV_LOG_WARNING, "Warning: Color space RGB "
                           "implicitly sets color range to PC range.\n");
                    ctx->color_warnings |= 1;
                }
                header->color_range = 1;
            }
        } else if (!(ctx->color_warnings & 4) && header->intra_only && !profile &&
                   ctx->color_space >= 0 && ctx->color_space != VP9_CS_BT_601) {
            av_log(bsf, AV_LOG_WARNING, "Warning: Intra-only frames in "
                   "profile 0 are automatically BT.601.\n");
            ctx->color_warnings |= 4;
        }
    }

    err = ff_cbs_write_packet(ctx->cbc, pkt, frag);
    if (err < 0) {
        av_log(bsf, AV_LOG_ERROR, "Failed to write packet.\n");
        goto fail;
    }

    err = 0;
fail:
    ff_cbs_fragment_reset(ctx->cbc, frag);

    if (err < 0)
        av_packet_unref(pkt);

    return err;
}

static int vp9_metadata_init(AVBSFContext *bsf)
{
    VP9MetadataContext *ctx = bsf->priv_data;

    return ff_cbs_init(&ctx->cbc, AV_CODEC_ID_VP9, bsf);
}

static void vp9_metadata_close(AVBSFContext *bsf)
{
    VP9MetadataContext *ctx = bsf->priv_data;

    ff_cbs_fragment_free(ctx->cbc, &ctx->fragment);
    ff_cbs_close(&ctx->cbc);
}

#define OFFSET(x) offsetof(VP9MetadataContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption vp9_metadata_options[] = {
    { "color_space", "Set colour space (section 7.2.2)",
        OFFSET(color_space), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, VP9_CS_RGB, FLAGS, "cs" },
    { "unknown",  "Unknown/unspecified",  0, AV_OPT_TYPE_CONST,
        { .i64 = VP9_CS_UNKNOWN   }, .flags = FLAGS, .unit = "cs" },
    { "bt601",    "ITU-R BT.601-7",       0, AV_OPT_TYPE_CONST,
        { .i64 = VP9_CS_BT_601    }, .flags = FLAGS, .unit = "cs" },
    { "bt709",    "ITU-R BT.709-6",       0, AV_OPT_TYPE_CONST,
        { .i64 = VP9_CS_BT_709    }, .flags = FLAGS, .unit = "cs" },
    { "smpte170", "SMPTE-170",            0, AV_OPT_TYPE_CONST,
        { .i64 = VP9_CS_SMPTE_170 }, .flags = FLAGS, .unit = "cs" },
    { "smpte240", "SMPTE-240",            0, AV_OPT_TYPE_CONST,
        { .i64 = VP9_CS_SMPTE_240 }, .flags = FLAGS, .unit = "cs" },
    { "bt2020",   "ITU-R BT.2020-2",      0, AV_OPT_TYPE_CONST,
        { .i64 = VP9_CS_BT_2020   }, .flags = FLAGS, .unit = "cs" },
    { "rgb",      "sRGB / IEC 61966-2-1", 0, AV_OPT_TYPE_CONST,
        { .i64 = VP9_CS_RGB       }, .flags = FLAGS, .unit = "cs" },

    { "color_range", "Set colour range (section 7.2.2)",
        OFFSET(color_range), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 1, FLAGS, "cr" },
    { "tv", "TV (limited) range", 0, AV_OPT_TYPE_CONST,
        { .i64 = 0 }, .flags = FLAGS, .unit = "cr" },
    { "pc", "PC (full) range",    0, AV_OPT_TYPE_CONST,
        { .i64 = 1 }, .flags = FLAGS, .unit = "cr" },

    { NULL }
};

static const AVClass vp9_metadata_class = {
    .class_name = "vp9_metadata_bsf",
    .item_name  = av_default_item_name,
    .option     = vp9_metadata_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const enum AVCodecID vp9_metadata_codec_ids[] = {
    AV_CODEC_ID_VP9, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_vp9_metadata_bsf = {
    .name           = "vp9_metadata",
    .priv_data_size = sizeof(VP9MetadataContext),
    .priv_class     = &vp9_metadata_class,
    .init           = &vp9_metadata_init,
    .close          = &vp9_metadata_close,
    .filter         = &vp9_metadata_filter,
    .codec_ids      = vp9_metadata_codec_ids,
};
