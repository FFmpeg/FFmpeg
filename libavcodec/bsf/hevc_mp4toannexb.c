/*
 * HEVC MP4 to Annex B byte stream format filter
 * copyright (c) 2015 Anton Khirnov
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
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "libavcodec/bsf.h"
#include "libavcodec/bsf_internal.h"
#include "libavcodec/bytestream.h"
#include "libavcodec/defs.h"

#include "libavcodec/hevc/hevc.h"

#define MIN_HEVCC_LENGTH 23
#define MAX_EXTRADATA_NALS 2048
#define MAX_PS_CHAIN_DEPTH 16
#define MAX_PS_CHECKS_PER_PACKET 128

/* 32-bit FNV-1a hash algorithm constants */
#define FNV1A_32_OFFSET_BASIS 2166136261u
#define FNV1A_32_PRIME        16777619u

static inline uint32_t hash_ps(const uint8_t *data, size_t size)
{
    uint32_t h = FNV1A_32_OFFSET_BASIS;
    for (size_t i = 0; i < size; i++) {
        h ^= data[i];
        h *= FNV1A_32_PRIME;
    }
    return h;
}

typedef struct HEVCNALUnit {
    size_t   offset;
    size_t   size;
    int      type;
    uint32_t hash;
    int      matched;
    int      emit;
    int      next;
} HEVCNALUnit;

typedef struct HEVCBSFContext {
    uint8_t     *extradata;
    size_t       extradata_size;
    HEVCNALUnit *nals;
    int          nb_nals;
    int          nb_ps;
    int         *ps_buckets;
    int          ps_table_size;
    uint8_t      length_size;
    int          extradata_parsed;
    int          dup_ps_logged;
    unsigned     emitted_types;
    int          sps_seen;
} HEVCBSFContext;

static int hevc_extradata_to_annexb(AVBSFContext *ctx,
                                    const uint8_t *extradata, size_t extradata_size,
                                    uint8_t **out_extradata, int *out_extradata_size)
{
    HEVCBSFContext *s = ctx->priv_data;
    GetByteContext gb, gb_scan;
    int length_size, num_arrays, i, j;
    int total_nals = 0;
    size_t total_size = 0;
    int ret = 0;

    uint8_t     *new_extradata = NULL;
    size_t       new_extradata_size = 0;
    HEVCNALUnit *new_nals = NULL;
    int          nb_nals = 0;
    int         *ps_buckets = NULL;
    int          ps_table_size = 0;
    int          nb_ps = 0;

    /* bytestream2 and AVCodecParameters.extradata_size are both int. */
    if (extradata_size > INT_MAX) {
        av_log(ctx, AV_LOG_ERROR, "Extradata size exceeds INT_MAX\n");
        return AVERROR_INVALIDDATA;
    }

    bytestream2_init(&gb, extradata, (int)extradata_size);

    bytestream2_skip(&gb, 21);
    length_size = (bytestream2_get_byte(&gb) & 3) + 1;
    num_arrays  = bytestream2_get_byte(&gb);

    /* First pass: validate hvcC arrays and compute exact size and NAL count upfront */
    gb_scan = gb;
    for (i = 0; i < num_arrays; i++) {
        int type, cnt;

        if (bytestream2_get_bytes_left(&gb_scan) < 3) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        type = bytestream2_get_byte(&gb_scan) & 0x3f;
        cnt  = bytestream2_get_be16(&gb_scan);

        if (!(type == HEVC_NAL_VPS || type == HEVC_NAL_SPS || type == HEVC_NAL_PPS ||
              type == HEVC_NAL_SEI_PREFIX || type == HEVC_NAL_SEI_SUFFIX)) {
            av_log(ctx, AV_LOG_ERROR, "Invalid NAL unit type in extradata: %d\n",
                   type);
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        if (cnt > MAX_EXTRADATA_NALS - total_nals) {
            av_log(ctx, AV_LOG_ERROR, "Too many NAL units in extradata: exceeds %d\n",
                   MAX_EXTRADATA_NALS);
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        for (j = 0; j < cnt; j++) {
            int nalu_len;

            if (bytestream2_get_bytes_left(&gb_scan) < 2) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            nalu_len = bytestream2_get_be16(&gb_scan);

            if (nalu_len < 2 ||
                nalu_len > bytestream2_get_bytes_left(&gb_scan) ||
                4 + AV_INPUT_BUFFER_PADDING_SIZE + nalu_len > SIZE_MAX - total_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            /* Each hvcC length prefix grows into a 4-byte start code. */
            if (total_size > (size_t)INT_MAX - 4 - (size_t)nalu_len) {
                av_log(ctx, AV_LOG_ERROR, "Annex B extradata size exceeds INT_MAX\n");
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            total_size += 4 + nalu_len;
            total_nals++;
            bytestream2_skip(&gb_scan, nalu_len);
        }
    }

    /* Single allocation for exact extradata buffer and NAL descriptor array */
    if (total_nals > 0) {
        new_extradata = av_malloc(total_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!new_extradata) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        new_nals = av_malloc_array(total_nals, sizeof(*new_nals));
        if (!new_nals) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    /* Second pass: copy NAL units and fill descriptor array without reallocations */
    for (i = 0; i < num_arrays; i++) {
        int type = bytestream2_get_byte(&gb) & 0x3f;
        int cnt  = bytestream2_get_be16(&gb);

        for (j = 0; j < cnt; j++) {
            const int nalu_len = bytestream2_get_be16(&gb);

            new_nals[nb_nals].offset  = new_extradata_size;
            new_nals[nb_nals].size    = 4 + nalu_len;
            new_nals[nb_nals].type    = type;
            new_nals[nb_nals].matched = 0;
            new_nals[nb_nals].emit    = 1;
            new_nals[nb_nals].hash    = 0;
            new_nals[nb_nals].next    = -1;
            nb_nals++;

            AV_WB32(new_extradata + new_extradata_size, 1); // add the startcode
            bytestream2_get_buffer(&gb, new_extradata + new_extradata_size + 4, nalu_len);
            new_extradata_size += 4 + nalu_len;
        }
    }
    if (new_extradata)
        memset(new_extradata + new_extradata_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    if (!new_extradata_size)
        av_log(ctx, AV_LOG_WARNING, "No parameter sets in the extradata\n");

    if (out_extradata && out_extradata_size) {
        if (new_extradata_size > INT_MAX) {
            av_log(ctx, AV_LOG_ERROR, "Annex B extradata size exceeds INT_MAX\n");
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        av_freep(out_extradata);
        *out_extradata =
            av_malloc(new_extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!(*out_extradata)) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        *out_extradata_size = (int)new_extradata_size;
        memcpy(*out_extradata, new_extradata, new_extradata_size);
        memset(*out_extradata + new_extradata_size, 0,
               AV_INPUT_BUFFER_PADDING_SIZE);
    }

    for (i = 0; i < nb_nals; i++) {
        if (new_nals[i].type >= HEVC_NAL_VPS && new_nals[i].type <= HEVC_NAL_PPS)
            nb_ps++;
    }

    if (nb_ps > 0) {
        ps_table_size = 1 << av_ceil_log2(FFMAX(32, nb_ps * 2));
        ps_buckets = av_malloc_array(ps_table_size, sizeof(*ps_buckets));
        if (!ps_buckets) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        for (i = 0; i < ps_table_size; i++)
            ps_buckets[i] = -1;

        for (i = 0; i < nb_nals; i++) {
            HEVCNALUnit *nal = &new_nals[i];
            if (nal->type >= HEVC_NAL_VPS && nal->type <= HEVC_NAL_PPS) {
                nal->hash = hash_ps(new_extradata + nal->offset + 4, nal->size - 4);
                int bucket = nal->hash & (ps_table_size - 1);
                nal->next = ps_buckets[bucket];
                ps_buckets[bucket] = i;
            }
        }
    }

    av_freep(&s->nals);
    av_freep(&s->extradata);
    av_freep(&s->ps_buckets);
    s->extradata        = new_extradata;
    s->extradata_size   = new_extradata_size;
    s->nals             = new_nals;
    s->nb_nals          = nb_nals;
    s->nb_ps            = nb_ps;
    s->ps_buckets       = ps_buckets;
    s->ps_table_size    = ps_table_size;
    s->length_size      = length_size;
    s->extradata_parsed = 1;
    s->dup_ps_logged    = 0;
    return 0;
fail:
    av_freep(&new_extradata);
    av_freep(&new_nals);
    av_freep(&ps_buckets);
    return ret;
}

static int hevc_mp4toannexb_init(AVBSFContext *ctx)
{
    int ret;

    if (ctx->par_in->extradata_size < MIN_HEVCC_LENGTH ||
        AV_RB24(ctx->par_in->extradata) == 1           ||
        AV_RB32(ctx->par_in->extradata) == 1) {
        av_log(ctx, AV_LOG_VERBOSE,
               "The input looks like it is Annex B already\n");
    } else {
        ret = hevc_extradata_to_annexb(ctx,
                                       ctx->par_in->extradata,
                                       ctx->par_in->extradata_size,
                                       &ctx->par_out->extradata,
                                       &ctx->par_out->extradata_size);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static void emit_extradata_type(HEVCBSFContext *s, uint8_t *dst, size_t *out_offset, int type)
{
    unsigned bit = 1U << (type - HEVC_NAL_VPS);
    if (s->emitted_types & bit)
        return;
    s->emitted_types |= bit;

    for (int i = 0; i < s->nb_nals; i++) {
        HEVCNALUnit *nal = &s->nals[i];
        if (nal->emit && nal->type == type) {
            memcpy(dst + *out_offset, s->extradata + nal->offset, nal->size);
            *out_offset += nal->size;
            nal->emit = 0;
            if (type == HEVC_NAL_SPS)
                s->sps_seen = 1;
        }
    }
}

static void flush_extradata_before(HEVCBSFContext *s, uint8_t *dst, size_t *out_offset,
                                   int nalu_type, int has_irap)
{
    if (!has_irap)
        return;

    switch (nalu_type) {
    case HEVC_NAL_AUD:
        break;
    case HEVC_NAL_VPS:
        emit_extradata_type(s, dst, out_offset, HEVC_NAL_VPS);
        break;
    case HEVC_NAL_SPS:
        emit_extradata_type(s, dst, out_offset, HEVC_NAL_VPS);
        emit_extradata_type(s, dst, out_offset, HEVC_NAL_SPS);
        break;
    case HEVC_NAL_PPS:
        emit_extradata_type(s, dst, out_offset, HEVC_NAL_VPS);
        emit_extradata_type(s, dst, out_offset, HEVC_NAL_SPS);
        emit_extradata_type(s, dst, out_offset, HEVC_NAL_PPS);
        break;
    case HEVC_NAL_SEI_PREFIX:
        emit_extradata_type(s, dst, out_offset, HEVC_NAL_VPS);
        emit_extradata_type(s, dst, out_offset, HEVC_NAL_SPS);
        if (s->sps_seen)
            emit_extradata_type(s, dst, out_offset, HEVC_NAL_PPS);
        break;
    case HEVC_NAL_SEI_SUFFIX:
        emit_extradata_type(s, dst, out_offset, HEVC_NAL_VPS);
        emit_extradata_type(s, dst, out_offset, HEVC_NAL_SPS);
        emit_extradata_type(s, dst, out_offset, HEVC_NAL_PPS);
        emit_extradata_type(s, dst, out_offset, HEVC_NAL_SEI_PREFIX);
        emit_extradata_type(s, dst, out_offset, HEVC_NAL_SEI_SUFFIX);
        break;
    default:
        if (nalu_type <= HEVC_NAL_RSV_VCL31) {
            emit_extradata_type(s, dst, out_offset, HEVC_NAL_VPS);
            emit_extradata_type(s, dst, out_offset, HEVC_NAL_SPS);
            emit_extradata_type(s, dst, out_offset, HEVC_NAL_PPS);
            emit_extradata_type(s, dst, out_offset, HEVC_NAL_SEI_PREFIX);
        }
        break;
    }
}

static int hevc_mp4toannexb_filter(AVBSFContext *ctx, AVPacket *out)
{
    HEVCBSFContext *s = ctx->priv_data;
    AVPacket *in;
    GetByteContext gb;

    int got_irap = 0, got_ps = 0, seen_irap_ps = 0;
    int i, ret = 0;
    size_t extradata_size = 0;
    size_t in_annexb_size = 0;
    size_t extra_insert_size = 0;
    size_t total_out_size = 0;
    size_t out_offset = 0;
    uint8_t *extradata = NULL;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    extradata =
        av_packet_get_side_data(in, AV_PKT_DATA_NEW_EXTRADATA, &extradata_size);
    if (extradata && extradata_size >= MIN_HEVCC_LENGTH &&
        ((extradata[0] == 1) ||
         (extradata[0] == 0 && (extradata[1] || extradata[2] > 1)))) {
        ret = hevc_extradata_to_annexb(ctx, extradata, extradata_size,
                                       NULL, NULL);
        if (ret < 0)
            goto fail;
        av_packet_side_data_remove(in->side_data, &in->side_data_elems,
                                   AV_PKT_DATA_NEW_EXTRADATA);
    }

    if (!s->extradata_parsed) {
        av_packet_move_ref(out, in);
        av_packet_free(&in);
        return 0;
    }

    bytestream2_init(&gb, in->data, in->size);

    for (i = 0; i < s->nb_nals; i++)
        s->nals[i].matched = 0;

    {
        int unmatched_ps = s->nb_ps;
        int ps_checks = 0;
        int max_inband_ps_level = 0;

        while (bytestream2_get_bytes_left(&gb)) {
            uint32_t nalu_size = 0;
            int      nalu_type;

            if (bytestream2_get_bytes_left(&gb) < s->length_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            for (i = 0; i < s->length_size; i++)
                nalu_size = (nalu_size << 8) | bytestream2_get_byte(&gb);

            if (nalu_size < 2 || nalu_size > bytestream2_get_bytes_left(&gb)) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            nalu_type = (bytestream2_peek_byte(&gb) >> 1) & 0x3f;
            /* Prefix SEI can flush dependent extradata before later VPS/SPS. */
            if (!got_irap && nalu_type == HEVC_NAL_SEI_PREFIX)
                max_inband_ps_level = HEVC_NAL_PPS - HEVC_NAL_VPS;
            if (!got_irap && nalu_type >= HEVC_NAL_VPS && nalu_type <= HEVC_NAL_PPS) {
                int inband_level = nalu_type - HEVC_NAL_VPS;
                if (inband_level < max_inband_ps_level) {
                    /* Inverted topological order (e.g. PPS before SPS).
                     * Earlier dependencies cannot be suppressed by this out-of-order PS. */
                } else {
                    max_inband_ps_level = inband_level;
                    if (unmatched_ps > 0 && s->ps_buckets &&
                        ps_checks < MAX_PS_CHECKS_PER_PACKET) {
                        uint32_t hash;
                        int idx, depth = 0;

                        ps_checks++;
                        hash = hash_ps(gb.buffer, nalu_size);
                        idx  = s->ps_buckets[hash & (s->ps_table_size - 1)];

                        while (idx >= 0 && depth++ < MAX_PS_CHAIN_DEPTH) {
                            HEVCNALUnit *nal = &s->nals[idx];
                            if (nal->type == nalu_type && (nal->size - 4) == nalu_size &&
                                nal->hash == hash &&
                                !memcmp(s->extradata + nal->offset + 4, gb.buffer, nalu_size)) {
                                if (!nal->matched) {
                                    nal->matched = 1;
                                    unmatched_ps--;
                                }
                            }
                            idx = nal->next;
                        }
                    }
                }
            }
            if (FFMIN(INT_MAX, SIZE_MAX) < 4ULL + nalu_size ||
                in_annexb_size > INT_MAX - 4 - nalu_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            in_annexb_size += 4 + nalu_size;

            bytestream2_skip(&gb, nalu_size);
            got_ps   |= !got_irap && (nalu_type >= HEVC_NAL_VPS && nalu_type <= HEVC_NAL_PPS);
            got_irap |= nalu_type >= HEVC_NAL_BLA_W_LP &&
                        nalu_type <= HEVC_NAL_RSV_IRAP_VCL23;
        }
    }
    seen_irap_ps = got_irap && got_ps;

    s->emitted_types = 0;
    s->sps_seen = 0;

    if (got_irap) {
        int omitted = 0;

        for (i = 0; i < s->nb_nals; i++) {
            HEVCNALUnit *nal = &s->nals[i];

            nal->emit = 1;
            if (seen_irap_ps && nal->matched &&
                nal->type >= HEVC_NAL_VPS && nal->type <= HEVC_NAL_PPS) {
                nal->emit = 0;
                omitted++;
            }

            if (nal->emit)
                extra_insert_size += nal->size;
        }
        if (omitted && !s->dup_ps_logged) {
            av_log(ctx, AV_LOG_VERBOSE,
                   "Omitting %d extradata parameter sets already present in-band\n",
                   omitted);
            s->dup_ps_logged = 1;
        }
    } else {
        for (i = 0; i < s->nb_nals; i++)
            s->nals[i].emit = 0;
    }

    if (extra_insert_size > INT_MAX - in_annexb_size) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }
    total_out_size = in_annexb_size + extra_insert_size;

    ret = av_new_packet(out, total_out_size);
    if (ret < 0)
        goto fail;

    bytestream2_init(&gb, in->data, in->size);

    while (bytestream2_get_bytes_left(&gb)) {
        uint32_t nalu_size = 0;
        int      nalu_type;

        for (i = 0; i < s->length_size; i++)
            nalu_size = (nalu_size << 8) | bytestream2_get_byte(&gb);

        nalu_type = (bytestream2_peek_byte(&gb) >> 1) & 0x3f;

        flush_extradata_before(s, out->data, &out_offset, nalu_type, got_irap);

        if (nalu_type == HEVC_NAL_SPS)
            s->sps_seen = 1;

        AV_WB32(out->data + out_offset, 1);
        out_offset += 4;
        bytestream2_get_buffer(&gb, out->data + out_offset, nalu_size);
        out_offset += nalu_size;
    }

    if (got_irap)
        emit_extradata_type(s, out->data, &out_offset, HEVC_NAL_SEI_SUFFIX);

    av_assert1(out_offset == out->size);

    ret = av_packet_copy_props(out, in);
    if (ret < 0)
        goto fail;

fail:
    if (ret < 0)
        av_packet_unref(out);
    av_packet_free(&in);

    return ret;
}

static void hevc_mp4toannexb_close(AVBSFContext *ctx)
{
    HEVCBSFContext *s = ctx->priv_data;
    av_freep(&s->nals);
    av_freep(&s->extradata);
    av_freep(&s->ps_buckets);
}

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_HEVC, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_hevc_mp4toannexb_bsf = {
    .p.name         = "hevc_mp4toannexb",
    .p.codec_ids    = codec_ids,
    .priv_data_size = sizeof(HEVCBSFContext),
    .init           = hevc_mp4toannexb_init,
    .filter         = hevc_mp4toannexb_filter,
    .close          = hevc_mp4toannexb_close,
};
