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

#include <string.h>

#include "config.h"

#include "libavutil/avassert.h"
#include "libavutil/buffer.h"
#include "libavutil/common.h"

#include "cbs.h"
#include "cbs_internal.h"


static const CodedBitstreamType *cbs_type_table[] = {
#if CONFIG_CBS_H264
    &ff_cbs_type_h264,
#endif
#if CONFIG_CBS_H265
    &ff_cbs_type_h265,
#endif
#if CONFIG_CBS_MPEG2
    &ff_cbs_type_mpeg2,
#endif
#if CONFIG_CBS_VP9
    &ff_cbs_type_vp9,
#endif
};

const enum AVCodecID ff_cbs_all_codec_ids[] = {
#if CONFIG_CBS_H264
    AV_CODEC_ID_H264,
#endif
#if CONFIG_CBS_H265
    AV_CODEC_ID_H265,
#endif
#if CONFIG_CBS_MPEG2
    AV_CODEC_ID_MPEG2VIDEO,
#endif
#if CONFIG_CBS_VP9
    AV_CODEC_ID_VP9,
#endif
    AV_CODEC_ID_NONE
};

int ff_cbs_init(CodedBitstreamContext **ctx_ptr,
                enum AVCodecID codec_id, void *log_ctx)
{
    CodedBitstreamContext *ctx;
    const CodedBitstreamType *type;
    int i;

    type = NULL;
    for (i = 0; i < FF_ARRAY_ELEMS(cbs_type_table); i++) {
        if (cbs_type_table[i]->codec_id == codec_id) {
            type = cbs_type_table[i];
            break;
        }
    }
    if (!type)
        return AVERROR(EINVAL);

    ctx = av_mallocz(sizeof(*ctx));
    if (!ctx)
        return AVERROR(ENOMEM);

    ctx->log_ctx = log_ctx;
    ctx->codec   = type;

    ctx->priv_data = av_mallocz(ctx->codec->priv_data_size);
    if (!ctx->priv_data) {
        av_freep(&ctx);
        return AVERROR(ENOMEM);
    }

    ctx->decompose_unit_types = NULL;

    ctx->trace_enable = 0;
    ctx->trace_level  = AV_LOG_TRACE;

    *ctx_ptr = ctx;
    return 0;
}

void ff_cbs_close(CodedBitstreamContext **ctx_ptr)
{
    CodedBitstreamContext *ctx = *ctx_ptr;

    if (!ctx)
        return;

    if (ctx->codec && ctx->codec->close)
        ctx->codec->close(ctx);

    av_freep(&ctx->priv_data);
    av_freep(ctx_ptr);
}

static void cbs_unit_uninit(CodedBitstreamContext *ctx,
                            CodedBitstreamUnit *unit)
{
    av_buffer_unref(&unit->content_ref);
    unit->content = NULL;

    av_buffer_unref(&unit->data_ref);
    unit->data             = NULL;
    unit->data_size        = 0;
    unit->data_bit_padding = 0;
}

void ff_cbs_fragment_uninit(CodedBitstreamContext *ctx,
                            CodedBitstreamFragment *frag)
{
    int i;

    for (i = 0; i < frag->nb_units; i++)
        cbs_unit_uninit(ctx, &frag->units[i]);
    av_freep(&frag->units);
    frag->nb_units = 0;

    av_buffer_unref(&frag->data_ref);
    frag->data             = NULL;
    frag->data_size        = 0;
    frag->data_bit_padding = 0;
}

static int cbs_read_fragment_content(CodedBitstreamContext *ctx,
                                     CodedBitstreamFragment *frag)
{
    int err, i, j;

    for (i = 0; i < frag->nb_units; i++) {
        CodedBitstreamUnit *unit = &frag->units[i];

        if (ctx->decompose_unit_types) {
            for (j = 0; j < ctx->nb_decompose_unit_types; j++) {
                if (ctx->decompose_unit_types[j] == unit->type)
                    break;
            }
            if (j >= ctx->nb_decompose_unit_types)
                continue;
        }

        av_buffer_unref(&unit->content_ref);
        unit->content = NULL;

        av_assert0(unit->data && unit->data_ref);

        err = ctx->codec->read_unit(ctx, unit);
        if (err == AVERROR(ENOSYS)) {
            av_log(ctx->log_ctx, AV_LOG_VERBOSE,
                   "Decomposition unimplemented for unit %d "
                   "(type %"PRIu32").\n", i, unit->type);
        } else if (err < 0) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Failed to read unit %d "
                   "(type %"PRIu32").\n", i, unit->type);
            return err;
        }
    }

    return 0;
}

static int cbs_fill_fragment_data(CodedBitstreamContext *ctx,
                                  CodedBitstreamFragment *frag,
                                  const uint8_t *data, size_t size)
{
    av_assert0(!frag->data && !frag->data_ref);

    frag->data_ref =
        av_buffer_alloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!frag->data_ref)
        return AVERROR(ENOMEM);

    frag->data      = frag->data_ref->data;
    frag->data_size = size;

    memcpy(frag->data, data, size);
    memset(frag->data + size, 0,
           AV_INPUT_BUFFER_PADDING_SIZE);

    return 0;
}

int ff_cbs_read_extradata(CodedBitstreamContext *ctx,
                          CodedBitstreamFragment *frag,
                          const AVCodecParameters *par)
{
    int err;

    memset(frag, 0, sizeof(*frag));

    err = cbs_fill_fragment_data(ctx, frag, par->extradata,
                                 par->extradata_size);
    if (err < 0)
        return err;

    err = ctx->codec->split_fragment(ctx, frag, 1);
    if (err < 0)
        return err;

    return cbs_read_fragment_content(ctx, frag);
}

int ff_cbs_read_packet(CodedBitstreamContext *ctx,
                       CodedBitstreamFragment *frag,
                       const AVPacket *pkt)
{
    int err;

    memset(frag, 0, sizeof(*frag));

    if (pkt->buf) {
        frag->data_ref = av_buffer_ref(pkt->buf);
        if (!frag->data_ref)
            return AVERROR(ENOMEM);

        frag->data      = pkt->data;
        frag->data_size = pkt->size;

    } else {
        err = cbs_fill_fragment_data(ctx, frag, pkt->data, pkt->size);
        if (err < 0)
            return err;
    }

    err = ctx->codec->split_fragment(ctx, frag, 0);
    if (err < 0)
        return err;

    return cbs_read_fragment_content(ctx, frag);
}

int ff_cbs_read(CodedBitstreamContext *ctx,
                CodedBitstreamFragment *frag,
                const uint8_t *data, size_t size)
{
    int err;

    memset(frag, 0, sizeof(*frag));

    err = cbs_fill_fragment_data(ctx, frag, data, size);
    if (err < 0)
        return err;

    err = ctx->codec->split_fragment(ctx, frag, 0);
    if (err < 0)
        return err;

    return cbs_read_fragment_content(ctx, frag);
}


int ff_cbs_write_fragment_data(CodedBitstreamContext *ctx,
                               CodedBitstreamFragment *frag)
{
    int err, i;

    for (i = 0; i < frag->nb_units; i++) {
        CodedBitstreamUnit *unit = &frag->units[i];

        if (!unit->content)
            continue;

        av_buffer_unref(&unit->data_ref);
        unit->data = NULL;

        err = ctx->codec->write_unit(ctx, unit);
        if (err < 0) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Failed to write unit %d "
                   "(type %"PRIu32").\n", i, unit->type);
            return err;
        }
        av_assert0(unit->data && unit->data_ref);
    }

    av_buffer_unref(&frag->data_ref);
    frag->data = NULL;

    err = ctx->codec->assemble_fragment(ctx, frag);
    if (err < 0) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Failed to assemble fragment.\n");
        return err;
    }
    av_assert0(frag->data && frag->data_ref);

    return 0;
}

int ff_cbs_write_extradata(CodedBitstreamContext *ctx,
                           AVCodecParameters *par,
                           CodedBitstreamFragment *frag)
{
    int err;

    err = ff_cbs_write_fragment_data(ctx, frag);
    if (err < 0)
        return err;

    av_freep(&par->extradata);

    par->extradata = av_malloc(frag->data_size +
                               AV_INPUT_BUFFER_PADDING_SIZE);
    if (!par->extradata)
        return AVERROR(ENOMEM);

    memcpy(par->extradata, frag->data, frag->data_size);
    memset(par->extradata + frag->data_size, 0,
           AV_INPUT_BUFFER_PADDING_SIZE);
    par->extradata_size = frag->data_size;

    return 0;
}

int ff_cbs_write_packet(CodedBitstreamContext *ctx,
                        AVPacket *pkt,
                        CodedBitstreamFragment *frag)
{
    AVBufferRef *buf;
    int err;

    err = ff_cbs_write_fragment_data(ctx, frag);
    if (err < 0)
        return err;

    buf = av_buffer_ref(frag->data_ref);
    if (!buf)
        return AVERROR(ENOMEM);

    av_init_packet(pkt);
    pkt->buf  = buf;
    pkt->data = frag->data;
    pkt->size = frag->data_size;

    return 0;
}


void ff_cbs_trace_header(CodedBitstreamContext *ctx,
                         const char *name)
{
    if (!ctx->trace_enable)
        return;

    av_log(ctx->log_ctx, ctx->trace_level, "%s\n", name);
}

void ff_cbs_trace_syntax_element(CodedBitstreamContext *ctx, int position,
                                 const char *str, const int *subscripts,
                                 const char *bits, int64_t value)
{
    char name[256];
    size_t name_len, bits_len;
    int pad, subs, i, j, k, n;

    if (!ctx->trace_enable)
        return;

    av_assert0(value >= INT_MIN && value <= UINT32_MAX);

    subs = subscripts ? subscripts[0] : 0;
    n = 0;
    for (i = j = 0; str[i];) {
        if (str[i] == '[') {
            if (n < subs) {
                ++n;
                k = snprintf(name + j, sizeof(name) - j, "[%d", subscripts[n]);
                av_assert0(k > 0 && j + k < sizeof(name));
                j += k;
                for (++i; str[i] && str[i] != ']'; i++);
                av_assert0(str[i] == ']');
            } else {
                while (str[i] && str[i] != ']')
                    name[j++] = str[i++];
                av_assert0(str[i] == ']');
            }
        } else {
            av_assert0(j + 1 < sizeof(name));
            name[j++] = str[i++];
        }
    }
    av_assert0(j + 1 < sizeof(name));
    name[j] = 0;
    av_assert0(n == subs);

    name_len = strlen(name);
    bits_len = strlen(bits);

    if (name_len + bits_len > 60)
        pad = bits_len + 2;
    else
        pad = 61 - name_len;

    av_log(ctx->log_ctx, ctx->trace_level, "%-10d  %s%*s = %"PRId64"\n",
           position, name, pad, bits, value);
}

int ff_cbs_read_unsigned(CodedBitstreamContext *ctx, GetBitContext *gbc,
                         int width, const char *name,
                         const int *subscripts, uint32_t *write_to,
                         uint32_t range_min, uint32_t range_max)
{
    uint32_t value;
    int position;

    av_assert0(width > 0 && width <= 32);

    if (get_bits_left(gbc) < width) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid value at "
               "%s: bitstream ended.\n", name);
        return AVERROR_INVALIDDATA;
    }

    if (ctx->trace_enable)
        position = get_bits_count(gbc);

    value = get_bits_long(gbc, width);

    if (ctx->trace_enable) {
        char bits[33];
        int i;
        for (i = 0; i < width; i++)
            bits[i] = value >> (width - i - 1) & 1 ? '1' : '0';
        bits[i] = 0;

        ff_cbs_trace_syntax_element(ctx, position, name, subscripts,
                                    bits, value);
    }

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRIu32", but must be in [%"PRIu32",%"PRIu32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }

    *write_to = value;
    return 0;
}

int ff_cbs_write_unsigned(CodedBitstreamContext *ctx, PutBitContext *pbc,
                          int width, const char *name,
                          const int *subscripts, uint32_t value,
                          uint32_t range_min, uint32_t range_max)
{
    av_assert0(width > 0 && width <= 32);

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRIu32", but must be in [%"PRIu32",%"PRIu32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }

    if (put_bits_left(pbc) < width)
        return AVERROR(ENOSPC);

    if (ctx->trace_enable) {
        char bits[33];
        int i;
        for (i = 0; i < width; i++)
            bits[i] = value >> (width - i - 1) & 1 ? '1' : '0';
        bits[i] = 0;

        ff_cbs_trace_syntax_element(ctx, put_bits_count(pbc),
                                    name, subscripts, bits, value);
    }

    if (width < 32)
        put_bits(pbc, width, value);
    else
        put_bits32(pbc, value);

    return 0;
}


int ff_cbs_alloc_unit_content(CodedBitstreamContext *ctx,
                              CodedBitstreamUnit *unit,
                              size_t size,
                              void (*free)(void *opaque, uint8_t *data))
{
    av_assert0(!unit->content && !unit->content_ref);

    unit->content = av_mallocz(size);
    if (!unit->content)
        return AVERROR(ENOMEM);

    unit->content_ref = av_buffer_create(unit->content, size,
                                         free, ctx, 0);
    if (!unit->content_ref) {
        av_freep(&unit->content);
        return AVERROR(ENOMEM);
    }

    return 0;
}

int ff_cbs_alloc_unit_data(CodedBitstreamContext *ctx,
                           CodedBitstreamUnit *unit,
                           size_t size)
{
    av_assert0(!unit->data && !unit->data_ref);

    unit->data_ref = av_buffer_alloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!unit->data_ref)
        return AVERROR(ENOMEM);

    unit->data      = unit->data_ref->data;
    unit->data_size = size;

    memset(unit->data + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    return 0;
}

static int cbs_insert_unit(CodedBitstreamContext *ctx,
                           CodedBitstreamFragment *frag,
                           int position)
{
    CodedBitstreamUnit *units;

    units = av_malloc_array(frag->nb_units + 1, sizeof(*units));
    if (!units)
        return AVERROR(ENOMEM);

    if (position > 0)
        memcpy(units, frag->units, position * sizeof(*units));
    if (position < frag->nb_units)
        memcpy(units + position + 1, frag->units + position,
               (frag->nb_units - position) * sizeof(*units));

    memset(units + position, 0, sizeof(*units));

    av_freep(&frag->units);
    frag->units = units;
    ++frag->nb_units;

    return 0;
}

int ff_cbs_insert_unit_content(CodedBitstreamContext *ctx,
                               CodedBitstreamFragment *frag,
                               int position,
                               CodedBitstreamUnitType type,
                               void *content,
                               AVBufferRef *content_buf)
{
    CodedBitstreamUnit *unit;
    AVBufferRef *content_ref;
    int err;

    if (position == -1)
        position = frag->nb_units;
    av_assert0(position >= 0 && position <= frag->nb_units);

    if (content_buf) {
        content_ref = av_buffer_ref(content_buf);
        if (!content_ref)
            return AVERROR(ENOMEM);
    } else {
        content_ref = NULL;
    }

    err = cbs_insert_unit(ctx, frag, position);
    if (err < 0) {
        av_buffer_unref(&content_ref);
        return err;
    }

    unit = &frag->units[position];
    unit->type        = type;
    unit->content     = content;
    unit->content_ref = content_ref;

    return 0;
}

int ff_cbs_insert_unit_data(CodedBitstreamContext *ctx,
                            CodedBitstreamFragment *frag,
                            int position,
                            CodedBitstreamUnitType type,
                            uint8_t *data, size_t data_size,
                            AVBufferRef *data_buf)
{
    CodedBitstreamUnit *unit;
    AVBufferRef *data_ref;
    int err;

    if (position == -1)
        position = frag->nb_units;
    av_assert0(position >= 0 && position <= frag->nb_units);

    if (data_buf)
        data_ref = av_buffer_ref(data_buf);
    else
        data_ref = av_buffer_create(data, data_size, NULL, NULL, 0);
    if (!data_ref)
        return AVERROR(ENOMEM);

    err = cbs_insert_unit(ctx, frag, position);
    if (err < 0) {
        av_buffer_unref(&data_ref);
        return err;
    }

    unit = &frag->units[position];
    unit->type      = type;
    unit->data      = data;
    unit->data_size = data_size;
    unit->data_ref  = data_ref;

    return 0;
}

int ff_cbs_delete_unit(CodedBitstreamContext *ctx,
                       CodedBitstreamFragment *frag,
                       int position)
{
    if (position < 0 || position >= frag->nb_units)
        return AVERROR(EINVAL);

    cbs_unit_uninit(ctx, &frag->units[position]);

    --frag->nb_units;

    if (frag->nb_units == 0) {
        av_freep(&frag->units);

    } else {
        memmove(frag->units + position,
                frag->units + position + 1,
                (frag->nb_units - position) * sizeof(*frag->units));

        // Don't bother reallocating the unit array.
    }

    return 0;
}
