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

#include <stdbool.h>

#include "libavutil/avassert.h"
#include "libavutil/dovi_meta.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "bsf.h"
#include "bsf_internal.h"
#include "h2645_parse.h"
#include "packet.h"

#include "hevc/hevc.h"

enum DOVISplitMode {
    DOVI_SPLIT_BL     = 0,
    DOVI_SPLIT_BL_RPU = 1,
    DOVI_SPLIT_EL     = 2,
    DOVI_SPLIT_EL_RPU = 3,
};

typedef struct DOVISplitContext {
    const AVClass *class;
    int mode;

    int nal_length_size;        /* 0 means Annex-B input */
    int out_nal_length_size;    /* 0 means Annex-B output */
    H2645Packet pkt;
} DOVISplitContext;

static int hvcc_nal_length_size(const uint8_t *data, int size)
{
    if (size >= 23 && data[0] == 1 && AV_RB24(data) != 1 && AV_RB32(data) != 1)
        return (data[21] & 3) + 1;
    return 0;
}

static int dovi_split_init(AVBSFContext *ctx)
{
    DOVISplitContext *s = ctx->priv_data;
    bool keep_bl  = s->mode == DOVI_SPLIT_BL || s->mode == DOVI_SPLIT_BL_RPU;
    bool keep_el  = s->mode == DOVI_SPLIT_EL || s->mode == DOVI_SPLIT_EL_RPU;
    bool keep_rpu = s->mode == DOVI_SPLIT_BL_RPU || s->mode == DOVI_SPLIT_EL_RPU;

    /* Profile 7 is currently the only supported variant with EL by Dolby.
     * Default to that in case there is no DOVI config. */
    uint8_t dv_profile = 7;

    for (int i = 0; i < ctx->par_out->nb_coded_side_data; i++) {
        AVPacketSideData *sd = &ctx->par_out->coded_side_data[i];
        AVDOVIDecoderConfigurationRecord *cfg;
        if (sd->type != AV_PKT_DATA_DOVI_CONF)
            continue;
        cfg = (AVDOVIDecoderConfigurationRecord *)sd->data;
        cfg->bl_present_flag  &= keep_bl;
        cfg->el_present_flag  &= keep_el;
        cfg->rpu_present_flag &= keep_rpu;
        dv_profile = cfg->dv_profile;
        break;
    }

    if (keep_el) {
        const AVPacketSideData *sd;
        sd = av_packet_side_data_get(ctx->par_out->coded_side_data,
                                     ctx->par_out->nb_coded_side_data,
                                     AV_PKT_DATA_HEVC_CONF);
        if (sd && sd->size >= 23) {
            uint8_t *new_ed = av_mallocz(sd->size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!new_ed)
                return AVERROR(ENOMEM);
            memcpy(new_ed, sd->data, sd->size);
            av_freep(&ctx->par_out->extradata);
            ctx->par_out->extradata = new_ed;
            ctx->par_out->extradata_size = sd->size;
        }

        /* DV profile to EL size ratio */
        static const uint8_t el_div[] = {
            [2] = 2,
            [3] = 1,
            [4] = 2,
            [6] = 2,
            [7] = 2,
        };
        int div = dv_profile < FF_ARRAY_ELEMS(el_div) ? el_div[dv_profile] : 0;
        if (!div)
            av_log(ctx, AV_LOG_WARNING, "Unexpected DV Profile %d.\n", dv_profile);

        /* P7: EL is 1:1 for FHD BL */
        if (dv_profile == 7 && ctx->par_in->width <= 1920)
            div = 1;
        if (div > 1) {
            ctx->par_out->width = ctx->par_in->width / div;
            ctx->par_out->height = ctx->par_in->height / div;
        }
    }

    /* Drop AV_PKT_DATA_HEVC_CONF as it's no longer valid on output. It's
     * set as extradata for EL. */
    av_packet_side_data_remove(ctx->par_out->coded_side_data,
                               &ctx->par_out->nb_coded_side_data,
                               AV_PKT_DATA_HEVC_CONF);

    s->nal_length_size = hvcc_nal_length_size(ctx->par_in->extradata,
                                              ctx->par_in->extradata_size);
    s->out_nal_length_size = hvcc_nal_length_size(ctx->par_out->extradata,
                                                  ctx->par_out->extradata_size);

    return 0;
}

static void dovi_split_close(AVBSFContext *ctx)
{
    DOVISplitContext *s = ctx->priv_data;
    ff_h2645_packet_uninit(&s->pkt);
}

static int nal_is_kept(const DOVISplitContext *s, const H2645NAL *nal,
                       const uint8_t **payload, int *payload_size)
{
    bool keep_el  = s->mode == DOVI_SPLIT_EL || s->mode == DOVI_SPLIT_EL_RPU;
    bool keep_bl  = s->mode == DOVI_SPLIT_BL || s->mode == DOVI_SPLIT_BL_RPU;
    bool keep_rpu = s->mode == DOVI_SPLIT_BL_RPU || s->mode == DOVI_SPLIT_EL_RPU;

    switch (nal->type) {
    case HEVC_NAL_UNSPEC63:
        /* EL: keep only when extracting EL, strip two-bytes of outer NAL header */
        if (!keep_el || nal->raw_size <= 2)
            return 0;
        *payload = nal->raw_data + 2;
        *payload_size = nal->raw_size - 2;
        return 1;
    case HEVC_NAL_UNSPEC62:
        /* RPU: kept verbatim only when the selected mode opted in. */
        if (!keep_rpu)
            return 0;
        *payload = nal->raw_data;
        *payload_size = nal->raw_size;
        return 1;
    default:
        /* Anything else is a base-layer NAL. */
        if (!keep_bl)
            return 0;
        *payload = nal->raw_data;
        *payload_size = nal->raw_size;
        return 1;
    }
}

static int dovi_split_filter(AVBSFContext *ctx, AVPacket *out)
{
    DOVISplitContext *s = ctx->priv_data;
    AVPacket *in = NULL;
    AVBufferRef *out_buf = NULL;
    uint8_t *dst;
    size_t out_size = 0;
    int kept_count = 0;
    int flags = (s->nal_length_size ? H2645_FLAG_IS_NALFF : 0) |
                H2645_FLAG_SMALL_PADDING;
    int prefix_size = s->out_nal_length_size ? s->out_nal_length_size : 4;
    int ret;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    ret = ff_h2645_packet_split(&s->pkt, in->data, in->size, ctx,
                                s->nal_length_size, AV_CODEC_ID_HEVC, flags);
    if (ret < 0)
        goto fail;

    for (int i = 0; i < s->pkt.nb_nals; i++) {
        const uint8_t *payload;
        int payload_size;
        if (!nal_is_kept(s, &s->pkt.nals[i], &payload, &payload_size))
            continue;
        out_size += prefix_size + payload_size;
        kept_count++;
    }

    if (!kept_count) {
        ret = AVERROR(EAGAIN);
        goto fail;
    }

    out_buf = av_buffer_alloc(out_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!out_buf) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    dst = out_buf->data;
    for (int i = 0; i < s->pkt.nb_nals; i++) {
        const uint8_t *payload;
        int payload_size;
        if (!nal_is_kept(s, &s->pkt.nals[i], &payload, &payload_size))
            continue;
        switch (s->out_nal_length_size) {
        case 0: AV_WB32(dst, 1);            break;
        case 1: AV_WB8 (dst, payload_size); break;
        case 2: AV_WB16(dst, payload_size); break;
        case 3: AV_WB24(dst, payload_size); break;
        case 4: AV_WB32(dst, payload_size); break;
        }
        dst += prefix_size;
        memcpy(dst, payload, payload_size);
        dst += payload_size;
    }
    memset(dst, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    av_assert0(dst == out_buf->data + out_size);

    ret = av_packet_copy_props(out, in);
    if (ret < 0)
        goto fail;

    out->buf  = out_buf;
    out->data = out_buf->data;
    out->size = out_size;
    out_buf   = NULL;

fail:
    av_buffer_unref(&out_buf);
    av_packet_free(&in);
    if (ret < 0 && ret != AVERROR(EAGAIN))
        av_packet_unref(out);
    return ret;
}

#define OFFSET(x) offsetof(DOVISplitContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_BSF_PARAM)
static const AVOption dovi_split_options[] = {
    { "mode", "Which Dolby Vision components to keep in the output bitstream", OFFSET(mode), AV_OPT_TYPE_INT, { .i64 = DOVI_SPLIT_BL }, DOVI_SPLIT_BL, DOVI_SPLIT_EL_RPU, FLAGS, .unit = "mode" },
        { "bl", "Base layer only", 0, AV_OPT_TYPE_CONST, { .i64 = DOVI_SPLIT_BL }, .flags = FLAGS, .unit = "mode" },
        { "bl_rpu", "Base layer with the RPU NAL", 0, AV_OPT_TYPE_CONST, { .i64 = DOVI_SPLIT_BL_RPU }, .flags = FLAGS, .unit = "mode" },
        { "el", "Enhancement layer only", 0, AV_OPT_TYPE_CONST, { .i64 = DOVI_SPLIT_EL }, .flags = FLAGS, .unit = "mode" },
        { "el_rpu", "Enhancement layer with the RPU NAL", 0, AV_OPT_TYPE_CONST, { .i64 = DOVI_SPLIT_EL_RPU }, .flags = FLAGS, .unit = "mode" },
    { NULL },
};

static const AVClass dovi_split_class = {
    .class_name = "dovi_split_bsf",
    .item_name  = av_default_item_name,
    .option     = dovi_split_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const enum AVCodecID dovi_split_codec_ids[] = {
    AV_CODEC_ID_HEVC, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_dovi_split_bsf = {
    .p.name         = "dovi_split",
    .p.codec_ids    = dovi_split_codec_ids,
    .p.priv_class   = &dovi_split_class,
    .priv_data_size = sizeof(DOVISplitContext),
    .init           = dovi_split_init,
    .close          = dovi_split_close,
    .filter         = dovi_split_filter,
};
