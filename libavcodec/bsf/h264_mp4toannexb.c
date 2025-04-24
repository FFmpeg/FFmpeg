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

#include "bsf.h"
#include "bsf_internal.h"
#include "bytestream.h"
#include "defs.h"
#include "h264.h"
#include "sei.h"

typedef struct H264BSFContext {
    uint8_t *sps;
    uint8_t *pps;
    int      sps_size;
    int      pps_size;
    unsigned sps_buf_size;
    unsigned pps_buf_size;
    uint8_t  length_size;
    uint8_t  new_idr;
    uint8_t  idr_sps_seen;
    uint8_t  idr_pps_seen;
    int      extradata_parsed;
} H264BSFContext;

enum PsSource {
    PS_OUT_OF_BAND = -1,
    PS_NONE = 0,
    PS_IN_BAND = 1,
};

static void count_or_copy(uint8_t **out, uint64_t *out_size,
                          const uint8_t *in, int in_size, enum PsSource ps, int copy)
{
    uint8_t start_code_size;

    if (ps == PS_OUT_OF_BAND)
        /* start code already present in out-of-band ps data, so don't need to
         * add it manually again
         */
        start_code_size = 0;
    else if (ps == PS_IN_BAND || *out_size == 0)
        start_code_size = 4;
    else
        start_code_size = 3;

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

static int h264_extradata_to_annexb(AVBSFContext *ctx,
                                    uint8_t *extradata, int extradata_size)
{
    H264BSFContext *s = ctx->priv_data;
    GetByteContext ogb, *gb = &ogb;
    uint16_t unit_size;
    uint32_t total_size                 = 0;
    uint8_t *out                        = NULL, unit_nb, sps_done = 0;
    static const uint8_t nalu_header[4] = { 0, 0, 0, 1 };
    const int padding                   = AV_INPUT_BUFFER_PADDING_SIZE;
    int length_size, pps_offset = 0;

    if (extradata_size < 7) {
        av_log(ctx, AV_LOG_ERROR, "Invalid extradata size: %d\n", extradata_size);
        return AVERROR_INVALIDDATA;
    }

    bytestream2_init(gb, extradata, extradata_size);

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
        uint8_t *sps;

        s->sps_size = pps_offset;
        sps = av_fast_realloc(s->sps, &s->sps_buf_size, s->sps_size);
        if (!sps) {
            av_free(out);
            return AVERROR(ENOMEM);
        }
        s->sps = sps;
        memcpy(s->sps, out, s->sps_size);
    } else {
        av_log(ctx, AV_LOG_WARNING,
               "Warning: SPS NALU missing or invalid. "
               "The resulting stream may not play.\n");
    }
    if (pps_offset < total_size) {
        uint8_t *pps;

        s->pps_size = total_size - pps_offset;
        pps = av_fast_realloc(s->pps, &s->pps_buf_size, s->pps_size);
        if (!pps) {
            av_freep(&s->sps);
            av_free(out);
            return AVERROR(ENOMEM);
        }
        s->pps = pps;
        memcpy(s->pps, out + pps_offset, s->pps_size);
    } else {
        av_log(ctx, AV_LOG_WARNING,
               "Warning: PPS NALU missing or invalid. "
               "The resulting stream may not play.\n");
    }

    av_freep(&ctx->par_out->extradata);
    ctx->par_out->extradata      = out;
    ctx->par_out->extradata_size = total_size;

    s->length_size      = length_size;
    s->new_idr          = 1;
    s->idr_sps_seen     = 0;
    s->idr_pps_seen     = 0;
    s->extradata_parsed = 1;

    return 0;
}

static int h264_mp4toannexb_save_ps(uint8_t **dst, int *dst_size,
                                    unsigned *dst_buf_size,
                                    const uint8_t *nal, uint32_t nal_size,
                                    int first)
{
    static const uint8_t nalu_header[4] = { 0, 0, 0, 1 };
    const int start_code_size = sizeof(nalu_header);
    uint8_t *ptr;
    uint32_t size;

    if (first)
        size = 0;
    else
        size = *dst_size;

    ptr = av_fast_realloc(*dst, dst_buf_size, size + nal_size + start_code_size);
    if (!ptr)
        return AVERROR(ENOMEM);

    memcpy(ptr + size, nalu_header, start_code_size);
    size += start_code_size;
    memcpy(ptr + size, nal, nal_size);
    size += nal_size;

    *dst = ptr;
    *dst_size = size;
    return 0;
}

static int h264_mp4toannexb_filter_ps(H264BSFContext *s,
                                      const uint8_t *buf,
                                      const uint8_t *buf_end)
{
    int sps_count = 0;
    int pps_count = 0;
    uint8_t unit_type;

    do {
        uint32_t nal_size = 0;

        /* possible overread ok due to padding */
        for (int i = 0; i < s->length_size; i++)
            nal_size = (nal_size << 8) | buf[i];

        buf += s->length_size;

        /* This check requires the cast as the right side might
         * otherwise be promoted to an unsigned value. */
        if ((int64_t)nal_size > buf_end - buf)
            return AVERROR_INVALIDDATA;

        if (!nal_size)
            continue;

        unit_type = *buf & 0x1f;

        if (unit_type == H264_NAL_SPS) {
            h264_mp4toannexb_save_ps(&s->sps, &s->sps_size, &s->sps_buf_size, buf,
                                   nal_size, !sps_count);
            sps_count++;
        } else if (unit_type == H264_NAL_PPS) {
            h264_mp4toannexb_save_ps(&s->pps, &s->pps_size, &s->pps_buf_size, buf,
                                   nal_size, !pps_count);
            pps_count++;
        }

        buf += nal_size;
    } while (buf < buf_end);

    return 0;
}

static int h264_mp4toannexb_init(AVBSFContext *ctx)
{
    int extra_size = ctx->par_in->extradata_size;

    /* retrieve sps and pps NAL units from extradata */
    if (!extra_size                                               ||
        (extra_size >= 3 && AV_RB24(ctx->par_in->extradata) == 1) ||
        (extra_size >= 4 && AV_RB32(ctx->par_in->extradata) == 1)) {
        av_log(ctx, AV_LOG_VERBOSE,
               "The input looks like it is Annex B already\n");
        return 0;
    }
    return h264_extradata_to_annexb(ctx,
                                    ctx->par_in->extradata,
                                    ctx->par_in->extradata_size);
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
    size_t extradata_size;
    uint8_t *extradata;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    extradata = av_packet_get_side_data(in, AV_PKT_DATA_NEW_EXTRADATA,
                                        &extradata_size);
    if (extradata && extradata[0] == 1) {
        ret = h264_extradata_to_annexb(ctx, extradata, extradata_size);
        if (ret < 0)
            goto fail;
        av_packet_side_data_remove(in->side_data, &in->side_data_elems,
                                   AV_PKT_DATA_NEW_EXTRADATA);
    }

    /* nothing to filter */
    if (!s->extradata_parsed) {
        av_packet_move_ref(opkt, in);
        av_packet_free(&in);
        return 0;
    }

    buf_end  = in->data + in->size;
    ret = h264_mp4toannexb_filter_ps(s, in->data, buf_end);
    if (ret < 0)
        goto fail;

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
            enum PsSource ps;

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
                        count_or_copy(&out, &out_size, s->sps, s->sps_size, PS_OUT_OF_BAND, j);
                        sps_seen = 1;
                    }
                }
            }

            /* If this is a new IDR picture following an IDR picture, reset the idr flag.
             * Just check first_mb_in_slice to be 0 as this is the simplest solution.
             * This could be checking idr_pic_id instead, but would complexify the parsing. */
            if (!new_idr && unit_type == H264_NAL_IDR_SLICE && (buf[1] & 0x80))
                new_idr = 1;

            /* If this is a buffering period SEI without a corresponding sps/pps
             * then prepend any existing sps/pps before the SEI */
            if (unit_type == H264_NAL_SEI && buf[1] == SEI_TYPE_BUFFERING_PERIOD &&
                !sps_seen && !pps_seen) {
                if (s->sps_size) {
                    count_or_copy(&out, &out_size, s->sps, s->sps_size, PS_OUT_OF_BAND, j);
                    sps_seen = 1;
                }
                if (s->pps_size) {
                    count_or_copy(&out, &out_size, s->pps, s->pps_size, PS_OUT_OF_BAND, j);
                    pps_seen = 1;
                }
            }

            /* prepend only to the first type 5 NAL unit of an IDR picture, if no sps/pps are already present */
            if (new_idr && unit_type == H264_NAL_IDR_SLICE && !sps_seen && !pps_seen) {
                if (s->sps_size)
                    count_or_copy(&out, &out_size, s->sps, s->sps_size, PS_OUT_OF_BAND, j);
                if (s->pps_size)
                    count_or_copy(&out, &out_size, s->pps, s->pps_size, PS_OUT_OF_BAND, j);
                new_idr = 0;
            /* if only SPS has been seen, also insert PPS */
            } else if (new_idr && unit_type == H264_NAL_IDR_SLICE && sps_seen && !pps_seen) {
                if (!s->pps_size) {
                    LOG_ONCE(ctx, AV_LOG_WARNING, "PPS not present in the stream, nor in AVCC, stream may be unreadable\n");
                } else {
                    count_or_copy(&out, &out_size, s->pps, s->pps_size, PS_OUT_OF_BAND, j);
                }
            }

            if (unit_type == H264_NAL_SPS || unit_type == H264_NAL_PPS)
                ps = PS_IN_BAND;
            else
                ps = PS_NONE;
            count_or_copy(&out, &out_size, buf, nal_size, ps, j);
            if (unit_type == H264_NAL_SLICE) {
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

static void h264_mp4toannexb_close(AVBSFContext *ctx)
{
    H264BSFContext *s = ctx->priv_data;

    av_freep(&s->sps);
    av_freep(&s->pps);
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

const FFBitStreamFilter ff_h264_mp4toannexb_bsf = {
    .p.name         = "h264_mp4toannexb",
    .p.codec_ids    = codec_ids,
    .priv_data_size = sizeof(H264BSFContext),
    .init           = h264_mp4toannexb_init,
    .filter         = h264_mp4toannexb_filter,
    .close          = h264_mp4toannexb_close,
    .flush          = h264_mp4toannexb_flush,
};
