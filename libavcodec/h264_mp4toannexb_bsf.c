/*
 * H.264 MP4 to Annex B byte stream format filter
 * Copyright (c) 2007 Benoit Fouet <benoit.fouet@free.fr>
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

#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "avcodec.h"
#include "bsf.h"
#include "bytestream.h"
#include "h264.h"

typedef struct H264BSFContext {
    uint8_t *sps;
    uint8_t *pps;
    int      sps_size;
    int      pps_size;
    uint8_t  length_size;
    uint8_t  new_idr;
    uint8_t  idr_sps_seen;
    uint8_t  idr_pps_seen;
    int      extradata_parsed;
} H264BSFContext;

static void count_or_copy(uint8_t **out, uint64_t *out_size,
                          const uint8_t *in, int in_size, int ps, int copy)
{
    uint8_t start_code_size = ps < 0 ? 0 : *out_size == 0 || ps ? 4 : 3;

    if (copy) {
        memcpy(*out + start_code_size, in, in_size);
        if (start_code_size == 4) {
            AV_WB32(*out, 1);
        } else if (start_code_size) {
            (*out)[0] =
            (*out)[1] = 0;
            (*out)[2] = 1;
        }
        *out  += start_code_size + in_size;
    }
    *out_size += start_code_size + in_size;
}

static int h264_extradata_to_annexb(AVBSFContext *ctx, const int padding)
{
    H264BSFContext *s = ctx->priv_data;
    GetByteContext ogb, *gb = &ogb;
    uint16_t unit_size;
    uint32_t total_size                 = 0;
    uint8_t *out                        = NULL, unit_nb, sps_done = 0;
    static const uint8_t nalu_header[4] = { 0, 0, 0, 1 };
    int length_size, pps_offset = 0;

    bytestream2_init(gb, ctx->par_in->extradata, ctx->par_in->extradata_size);

    bytestream2_skipu(gb, 4);

    /* retrieve length coded size */
    length_size = (bytestream2_get_byteu(gb) & 0x3) + 1;

    /* retrieve sps and pps unit(s) */
    unit_nb = bytestream2_get_byteu(gb) & 0x1f; /* number of sps unit(s) */
    if (!unit_nb) {
        goto pps;
    }

    while (unit_nb--) {
        int err;

        /* possible overread ok due to padding */
        unit_size   = bytestream2_get_be16u(gb);
        total_size += unit_size + 4;
        av_assert1(total_size <= INT_MAX - padding);
        if (bytestream2_get_bytes_left(gb) < unit_size + !sps_done) {
            av_log(ctx, AV_LOG_ERROR, "Global extradata truncated, "
                   "corrupted stream or invalid MP4/AVCC bitstream\n");
            av_free(out);
            return AVERROR_INVALIDDATA;
        }
        if ((err = av_reallocp(&out, total_size + padding)) < 0)
            return err;
        memcpy(out + total_size - unit_size - 4, nalu_header, 4);
        bytestream2_get_bufferu(gb, out + total_size - unit_size, unit_size);
pps:
        if (!unit_nb && !sps_done++) {
            unit_nb = bytestream2_get_byteu(gb); /* number of pps unit(s) */
            pps_offset = total_size;
        }
    }

    if (out)
        memset(out + total_size, 0, padding);

    if (pps_offset) {
        s->sps      = out;
        s->sps_size = pps_offset;
    } else {
        av_log(ctx, AV_LOG_WARNING,
               "Warning: SPS NALU missing or invalid. "
               "The resulting stream may not play.\n");
    }
    if (pps_offset < total_size) {
        s->pps      = out + pps_offset;
        s->pps_size = total_size - pps_offset;
    } else {
        av_log(ctx, AV_LOG_WARNING,
               "Warning: PPS NALU missing or invalid. "
               "The resulting stream may not play.\n");
    }

    av_freep(&ctx->par_out->extradata);
    ctx->par_out->extradata      = out;
    ctx->par_out->extradata_size = total_size;

    return length_size;
}

static int h264_mp4toannexb_init(AVBSFContext *ctx)
{
    H264BSFContext *s = ctx->priv_data;
    int extra_size = ctx->par_in->extradata_size;
    int ret;

    /* retrieve sps and pps NAL units from extradata */
    if (!extra_size                                               ||
        (extra_size >= 3 && AV_RB24(ctx->par_in->extradata) == 1) ||
        (extra_size >= 4 && AV_RB32(ctx->par_in->extradata) == 1)) {
        av_log(ctx, AV_LOG_VERBOSE,
               "The input looks like it is Annex B already\n");
    } else if (extra_size >= 7) {
        ret = h264_extradata_to_annexb(ctx, AV_INPUT_BUFFER_PADDING_SIZE);
        if (ret < 0)
            return ret;

        s->length_size      = ret;
        s->new_idr          = 1;
        s->idr_sps_seen     = 0;
        s->idr_pps_seen     = 0;
        s->extradata_parsed = 1;
    } else {
        av_log(ctx, AV_LOG_ERROR, "Invalid extradata size: %d\n", extra_size);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int h264_mp4toannexb_filter(AVBSFContext *ctx, AVPacket *opkt)
{
    H264BSFContext *s = ctx->priv_data;
    AVPacket *in;
    uint8_t unit_type, new_idr, sps_seen, pps_seen;
    const uint8_t *buf;
    const uint8_t *buf_end;
    uint8_t *out;
    uint64_t out_size;
    int ret;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    /* nothing to filter */
    if (!s->extradata_parsed) {
        av_packet_move_ref(opkt, in);
        av_packet_free(&in);
        return 0;
    }

    buf_end  = in->data + in->size;

#define LOG_ONCE(...) \
    if (j) \
        av_log(__VA_ARGS__)
    for (int j = 0; j < 2; j++) {
        buf      = in->data;
        new_idr  = s->new_idr;
        sps_seen = s->idr_sps_seen;
        pps_seen = s->idr_pps_seen;
        out_size = 0;

        do {
            uint32_t nal_size = 0;

            /* possible overread ok due to padding */
            for (int i = 0; i < s->length_size; i++)
                nal_size = (nal_size << 8) | buf[i];

            buf += s->length_size;

            /* This check requires the cast as the right side might
             * otherwise be promoted to an unsigned value. */
            if ((int64_t)nal_size > buf_end - buf) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            if (!nal_size)
                continue;

            unit_type = *buf & 0x1f;

            if (unit_type == H264_NAL_SPS) {
                sps_seen = new_idr = 1;
            } else if (unit_type == H264_NAL_PPS) {
                pps_seen = new_idr = 1;
                /* if SPS has not been seen yet, prepend the AVCC one to PPS */
                if (!sps_seen) {
                    if (!s->sps_size) {
                        LOG_ONCE(ctx, AV_LOG_WARNING, "SPS not present in the stream, nor in AVCC, stream may be unreadable\n");
                    } else {
                        count_or_copy(&out, &out_size, s->sps, s->sps_size, -1, j);
                        sps_seen = 1;
                    }
                }
            }

            /* If this is a new IDR picture following an IDR picture, reset the idr flag.
             * Just check first_mb_in_slice to be 0 as this is the simplest solution.
             * This could be checking idr_pic_id instead, but would complexify the parsing. */
            if (!new_idr && unit_type == H264_NAL_IDR_SLICE && (buf[1] & 0x80))
                new_idr = 1;

            /* prepend only to the first type 5 NAL unit of an IDR picture, if no sps/pps are already present */
            if (new_idr && unit_type == H264_NAL_IDR_SLICE && !sps_seen && !pps_seen) {
                if (ctx->par_out->extradata)
                    count_or_copy(&out, &out_size, ctx->par_out->extradata,
                                  ctx->par_out->extradata_size, -1, j);
                new_idr = 0;
            /* if only SPS has been seen, also insert PPS */
            } else if (new_idr && unit_type == H264_NAL_IDR_SLICE && sps_seen && !pps_seen) {
                if (!s->pps_size) {
                    LOG_ONCE(ctx, AV_LOG_WARNING, "PPS not present in the stream, nor in AVCC, stream may be unreadable\n");
                } else {
                    count_or_copy(&out, &out_size, s->pps, s->pps_size, -1, j);
                }
            }

            count_or_copy(&out, &out_size, buf, nal_size,
                          unit_type == H264_NAL_SPS || unit_type == H264_NAL_PPS, j);
            if (!new_idr && unit_type == H264_NAL_SLICE) {
                new_idr  = 1;
                sps_seen = 0;
                pps_seen = 0;
            }

            buf += nal_size;
        } while (buf < buf_end);

        if (!j) {
            if (out_size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            ret = av_new_packet(opkt, out_size);
            if (ret < 0)
                goto fail;
            out = opkt->data;
        }
    }
#undef LOG_ONCE

    av_assert1(out_size == opkt->size);

    s->new_idr      = new_idr;
    s->idr_sps_seen = sps_seen;
    s->idr_pps_seen = pps_seen;

    ret = av_packet_copy_props(opkt, in);
    if (ret < 0)
        goto fail;

fail:
    if (ret < 0)
        av_packet_unref(opkt);
    av_packet_free(&in);

    return ret;
}

static void h264_mp4toannexb_flush(AVBSFContext *ctx)
{
    H264BSFContext *s = ctx->priv_data;

    s->idr_sps_seen = 0;
    s->idr_pps_seen = 0;
    s->new_idr      = s->extradata_parsed;
}

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_H264, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_h264_mp4toannexb_bsf = {
    .name           = "h264_mp4toannexb",
    .priv_data_size = sizeof(H264BSFContext),
    .init           = h264_mp4toannexb_init,
    .filter         = h264_mp4toannexb_filter,
    .flush          = h264_mp4toannexb_flush,
    .codec_ids      = codec_ids,
};
