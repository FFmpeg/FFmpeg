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
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "cbs.h"
#include "cbs_internal.h"
#include "refstruct.h"


static const CodedBitstreamType *const cbs_type_table[] = {
#if CONFIG_CBS_AV1
    &ff_cbs_type_av1,
#endif
#if CONFIG_CBS_H264
    &ff_cbs_type_h264,
#endif
#if CONFIG_CBS_H265
    &ff_cbs_type_h265,
#endif
#if CONFIG_CBS_H266
    &ff_cbs_type_h266,
#endif
#if CONFIG_CBS_JPEG
    &ff_cbs_type_jpeg,
#endif
#if CONFIG_CBS_MPEG2
    &ff_cbs_type_mpeg2,
#endif
#if CONFIG_CBS_VP8
    &ff_cbs_type_vp8,
#endif
#if CONFIG_CBS_VP9
    &ff_cbs_type_vp9,
#endif
};

const enum AVCodecID ff_cbs_all_codec_ids[] = {
#if CONFIG_CBS_AV1
    AV_CODEC_ID_AV1,
#endif
#if CONFIG_CBS_H264
    AV_CODEC_ID_H264,
#endif
#if CONFIG_CBS_H265
    AV_CODEC_ID_H265,
#endif
#if CONFIG_CBS_H266
    AV_CODEC_ID_H266,
#endif
#if CONFIG_CBS_JPEG
    AV_CODEC_ID_MJPEG,
#endif
#if CONFIG_CBS_MPEG2
    AV_CODEC_ID_MPEG2VIDEO,
#endif
#if CONFIG_CBS_VP8
    AV_CODEC_ID_VP8,
#endif
#if CONFIG_CBS_VP9
    AV_CODEC_ID_VP9,
#endif
    AV_CODEC_ID_NONE
};

av_cold int ff_cbs_init(CodedBitstreamContext **ctx_ptr,
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
    ctx->codec   = type; /* Must be before any error */

    if (type->priv_data_size) {
        ctx->priv_data = av_mallocz(ctx->codec->priv_data_size);
        if (!ctx->priv_data) {
            av_freep(&ctx);
            return AVERROR(ENOMEM);
        }
        if (type->priv_class) {
            *(const AVClass **)ctx->priv_data = type->priv_class;
            av_opt_set_defaults(ctx->priv_data);
        }
    }

    ctx->decompose_unit_types = NULL;

    ctx->trace_enable  = 0;
    ctx->trace_level   = AV_LOG_TRACE;
    ctx->trace_context = ctx;

    *ctx_ptr = ctx;
    return 0;
}

av_cold void ff_cbs_flush(CodedBitstreamContext *ctx)
{
    if (ctx->codec->flush)
        ctx->codec->flush(ctx);
}

av_cold void ff_cbs_close(CodedBitstreamContext **ctx_ptr)
{
    CodedBitstreamContext *ctx = *ctx_ptr;

    if (!ctx)
        return;

    if (ctx->codec->close)
        ctx->codec->close(ctx);

    av_freep(&ctx->write_buffer);

    if (ctx->codec->priv_class && ctx->priv_data)
        av_opt_free(ctx->priv_data);

    av_freep(&ctx->priv_data);
    av_freep(ctx_ptr);
}

static void cbs_unit_uninit(CodedBitstreamUnit *unit)
{
    ff_refstruct_unref(&unit->content_ref);
    unit->content = NULL;

    av_buffer_unref(&unit->data_ref);
    unit->data             = NULL;
    unit->data_size        = 0;
    unit->data_bit_padding = 0;
}

void ff_cbs_fragment_reset(CodedBitstreamFragment *frag)
{
    int i;

    for (i = 0; i < frag->nb_units; i++)
        cbs_unit_uninit(&frag->units[i]);
    frag->nb_units = 0;

    av_buffer_unref(&frag->data_ref);
    frag->data             = NULL;
    frag->data_size        = 0;
    frag->data_bit_padding = 0;
}

av_cold void ff_cbs_fragment_free(CodedBitstreamFragment *frag)
{
    ff_cbs_fragment_reset(frag);

    av_freep(&frag->units);
    frag->nb_units_allocated = 0;
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

        ff_refstruct_unref(&unit->content_ref);
        unit->content = NULL;

        av_assert0(unit->data && unit->data_ref);

        err = ctx->codec->read_unit(ctx, unit);
        if (err == AVERROR(ENOSYS)) {
            av_log(ctx->log_ctx, AV_LOG_VERBOSE,
                   "Decomposition unimplemented for unit %d "
                   "(type %"PRIu32").\n", i, unit->type);
        } else if (err == AVERROR(EAGAIN)) {
            av_log(ctx->log_ctx, AV_LOG_VERBOSE,
                   "Skipping decomposition of unit %d "
                   "(type %"PRIu32").\n", i, unit->type);
            ff_refstruct_unref(&unit->content_ref);
            unit->content = NULL;
        } else if (err < 0) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Failed to read unit %d "
                   "(type %"PRIu32").\n", i, unit->type);
            return err;
        }
    }

    return 0;
}

static int cbs_fill_fragment_data(CodedBitstreamFragment *frag,
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

static int cbs_read_data(CodedBitstreamContext *ctx,
                         CodedBitstreamFragment *frag,
                         AVBufferRef *buf,
                         const uint8_t *data, size_t size,
                         int header)
{
    int err;

    if (buf) {
        frag->data_ref = av_buffer_ref(buf);
        if (!frag->data_ref)
            return AVERROR(ENOMEM);

        frag->data      = (uint8_t *)data;
        frag->data_size = size;

    } else {
        err = cbs_fill_fragment_data(frag, data, size);
        if (err < 0)
            return err;
    }

    err = ctx->codec->split_fragment(ctx, frag, header);
    if (err < 0)
        return err;

    return cbs_read_fragment_content(ctx, frag);
}

int ff_cbs_read_extradata(CodedBitstreamContext *ctx,
                          CodedBitstreamFragment *frag,
                          const AVCodecParameters *par)
{
    return cbs_read_data(ctx, frag, NULL,
                         par->extradata,
                         par->extradata_size, 1);
}

int ff_cbs_read_extradata_from_codec(CodedBitstreamContext *ctx,
                                     CodedBitstreamFragment *frag,
                                     const AVCodecContext *avctx)
{
    return cbs_read_data(ctx, frag, NULL,
                         avctx->extradata,
                         avctx->extradata_size, 1);
}

int ff_cbs_read_packet(CodedBitstreamContext *ctx,
                       CodedBitstreamFragment *frag,
                       const AVPacket *pkt)
{
    return cbs_read_data(ctx, frag, pkt->buf,
                         pkt->data, pkt->size, 0);
}

int ff_cbs_read_packet_side_data(CodedBitstreamContext *ctx,
                                 CodedBitstreamFragment *frag,
                                 const AVPacket *pkt)
{
    size_t side_data_size;
    const uint8_t *side_data =
        av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA,
                                &side_data_size);

    return cbs_read_data(ctx, frag, NULL,
                         side_data, side_data_size, 1);
}

int ff_cbs_read(CodedBitstreamContext *ctx,
                CodedBitstreamFragment *frag,
                const uint8_t *data, size_t size)
{
    return cbs_read_data(ctx, frag, NULL,
                         data, size, 0);
}

/**
 * Allocate a new internal data buffer of the given size in the unit.
 *
 * The data buffer will have input padding.
 */
static int cbs_alloc_unit_data(CodedBitstreamUnit *unit,
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

static int cbs_write_unit_data(CodedBitstreamContext *ctx,
                               CodedBitstreamUnit *unit)
{
    PutBitContext pbc;
    int ret;

    if (!ctx->write_buffer) {
        // Initial write buffer size is 1MB.
        ctx->write_buffer_size = 1024 * 1024;

    reallocate_and_try_again:
        ret = av_reallocp(&ctx->write_buffer, ctx->write_buffer_size);
        if (ret < 0) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Unable to allocate a "
                   "sufficiently large write buffer (last attempt "
                   "%"SIZE_SPECIFIER" bytes).\n", ctx->write_buffer_size);
            return ret;
        }
    }

    init_put_bits(&pbc, ctx->write_buffer, ctx->write_buffer_size);

    ret = ctx->codec->write_unit(ctx, unit, &pbc);
    if (ret < 0) {
        if (ret == AVERROR(ENOSPC)) {
            // Overflow.
            if (ctx->write_buffer_size == INT_MAX / 8)
                return AVERROR(ENOMEM);
            ctx->write_buffer_size = FFMIN(2 * ctx->write_buffer_size, INT_MAX / 8);
            goto reallocate_and_try_again;
        }
        // Write failed for some other reason.
        return ret;
    }

    // Overflow but we didn't notice.
    av_assert0(put_bits_count(&pbc) <= 8 * ctx->write_buffer_size);

    if (put_bits_count(&pbc) % 8)
        unit->data_bit_padding = 8 - put_bits_count(&pbc) % 8;
    else
        unit->data_bit_padding = 0;

    flush_put_bits(&pbc);

    ret = cbs_alloc_unit_data(unit, put_bytes_output(&pbc));
    if (ret < 0)
        return ret;

    memcpy(unit->data, ctx->write_buffer, unit->data_size);

    return 0;
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

        err = cbs_write_unit_data(ctx, unit);
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
    par->extradata_size = 0;

    if (!frag->data_size)
        return 0;

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

    av_buffer_unref(&pkt->buf);

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

void ff_cbs_trace_read_log(void *trace_context,
                           GetBitContext *gbc, int length,
                           const char *str, const int *subscripts,
                           int64_t value)
{
    CodedBitstreamContext *ctx = trace_context;
    char name[256];
    char bits[256];
    size_t name_len, bits_len;
    int pad, subs, i, j, k, n;
    int position;

    av_assert0(value >= INT_MIN && value <= UINT32_MAX);

    position = get_bits_count(gbc);

    av_assert0(length < 256);
    for (i = 0; i < length; i++)
        bits[i] = get_bits1(gbc) ? '1' : '0';
    bits[length] = 0;

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
    bits_len = length;

    if (name_len + bits_len > 60)
        pad = bits_len + 2;
    else
        pad = 61 - name_len;

    av_log(ctx->log_ctx, ctx->trace_level, "%-10d  %s%*s = %"PRId64"\n",
           position, name, pad, bits, value);
}

void ff_cbs_trace_write_log(void *trace_context,
                            PutBitContext *pbc, int length,
                            const char *str, const int *subscripts,
                            int64_t value)
{
    CodedBitstreamContext *ctx = trace_context;

    // Ensure that the syntax element is written to the output buffer,
    // make a GetBitContext pointed at the start position, then call the
    // read log function which can read the bits back to log them.

    GetBitContext gbc;
    int position;

    if (length > 0) {
        PutBitContext flush;
        flush = *pbc;
        flush_put_bits(&flush);
    }

    position = put_bits_count(pbc);
    av_assert0(position >= length);

    init_get_bits(&gbc, pbc->buf, position);

    skip_bits_long(&gbc, position - length);

    ff_cbs_trace_read_log(ctx, &gbc, length, str, subscripts, value);
}

static av_always_inline int cbs_read_unsigned(CodedBitstreamContext *ctx,
                                              GetBitContext *gbc,
                                              int width, const char *name,
                                              const int *subscripts,
                                              uint32_t *write_to,
                                              uint32_t range_min,
                                              uint32_t range_max)
{
    uint32_t value;

    CBS_TRACE_READ_START();

    av_assert0(width > 0 && width <= 32);

    if (get_bits_left(gbc) < width) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid value at "
               "%s: bitstream ended.\n", name);
        return AVERROR_INVALIDDATA;
    }

    value = get_bits_long(gbc, width);

    CBS_TRACE_READ_END();

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRIu32", but must be in [%"PRIu32",%"PRIu32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }

    *write_to = value;
    return 0;
}

int ff_cbs_read_unsigned(CodedBitstreamContext *ctx, GetBitContext *gbc,
                         int width, const char *name,
                         const int *subscripts, uint32_t *write_to,
                         uint32_t range_min, uint32_t range_max)
{
    return cbs_read_unsigned(ctx, gbc, width, name, subscripts,
                             write_to, range_min, range_max);
}

int ff_cbs_read_simple_unsigned(CodedBitstreamContext *ctx, GetBitContext *gbc,
                                int width, const char *name, uint32_t *write_to)
{
    return cbs_read_unsigned(ctx, gbc, width, name, NULL,
                             write_to, 0, UINT32_MAX);
}

int ff_cbs_write_unsigned(CodedBitstreamContext *ctx, PutBitContext *pbc,
                          int width, const char *name,
                          const int *subscripts, uint32_t value,
                          uint32_t range_min, uint32_t range_max)
{
    CBS_TRACE_WRITE_START();

    av_assert0(width > 0 && width <= 32);

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRIu32", but must be in [%"PRIu32",%"PRIu32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }

    if (put_bits_left(pbc) < width)
        return AVERROR(ENOSPC);

    if (width < 32)
        put_bits(pbc, width, value);
    else
        put_bits32(pbc, value);

    CBS_TRACE_WRITE_END();

    return 0;
}

int ff_cbs_write_simple_unsigned(CodedBitstreamContext *ctx, PutBitContext *pbc,
                                 int width, const char *name, uint32_t value)
{
    return ff_cbs_write_unsigned(ctx, pbc, width, name, NULL,
                                 value, 0, MAX_UINT_BITS(width));
}

int ff_cbs_read_signed(CodedBitstreamContext *ctx, GetBitContext *gbc,
                       int width, const char *name,
                       const int *subscripts, int32_t *write_to,
                       int32_t range_min, int32_t range_max)
{
    int32_t value;

    CBS_TRACE_READ_START();

    av_assert0(width > 0 && width <= 32);

    if (get_bits_left(gbc) < width) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid value at "
               "%s: bitstream ended.\n", name);
        return AVERROR_INVALIDDATA;
    }

    value = get_sbits_long(gbc, width);

    CBS_TRACE_READ_END();

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRId32", but must be in [%"PRId32",%"PRId32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }

    *write_to = value;
    return 0;
}

int ff_cbs_write_signed(CodedBitstreamContext *ctx, PutBitContext *pbc,
                        int width, const char *name,
                        const int *subscripts, int32_t value,
                        int32_t range_min, int32_t range_max)
{
    CBS_TRACE_WRITE_START();

    av_assert0(width > 0 && width <= 32);

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRId32", but must be in [%"PRId32",%"PRId32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }

    if (put_bits_left(pbc) < width)
        return AVERROR(ENOSPC);

    if (width < 32)
        put_sbits(pbc, width, value);
    else
        put_bits32(pbc, value);

    CBS_TRACE_WRITE_END();

    return 0;
}


static int cbs_insert_unit(CodedBitstreamFragment *frag,
                           int position)
{
    CodedBitstreamUnit *units;

    if (frag->nb_units < frag->nb_units_allocated) {
        units = frag->units;

        if (position < frag->nb_units)
            memmove(units + position + 1, units + position,
                    (frag->nb_units - position) * sizeof(*units));
    } else {
        units = av_malloc_array(frag->nb_units*2 + 1, sizeof(*units));
        if (!units)
            return AVERROR(ENOMEM);

        frag->nb_units_allocated = 2*frag->nb_units_allocated + 1;

        if (position > 0)
            memcpy(units, frag->units, position * sizeof(*units));

        if (position < frag->nb_units)
            memcpy(units + position + 1, frag->units + position,
                   (frag->nb_units - position) * sizeof(*units));
    }

    memset(units + position, 0, sizeof(*units));

    if (units != frag->units) {
        av_free(frag->units);
        frag->units = units;
    }

    ++frag->nb_units;

    return 0;
}

int ff_cbs_insert_unit_content(CodedBitstreamFragment *frag,
                               int position,
                               CodedBitstreamUnitType type,
                               void *content,
                               void *content_ref)
{
    CodedBitstreamUnit *unit;
    int err;

    if (position == -1)
        position = frag->nb_units;
    av_assert0(position >= 0 && position <= frag->nb_units);

    err = cbs_insert_unit(frag, position);
    if (err < 0)
        return err;

    if (content_ref) {
        // Create our own reference out of the user-supplied one.
        content_ref = ff_refstruct_ref(content_ref);
    }

    unit = &frag->units[position];
    unit->type        = type;
    unit->content     = content;
    unit->content_ref = content_ref;

    return 0;
}

static int cbs_insert_unit_data(CodedBitstreamFragment *frag,
                                CodedBitstreamUnitType type,
                                uint8_t *data, size_t data_size,
                                AVBufferRef *data_buf,
                                int position)
{
    CodedBitstreamUnit *unit;
    AVBufferRef *data_ref;
    int err;

    av_assert0(position >= 0 && position <= frag->nb_units);

    if (data_buf)
        data_ref = av_buffer_ref(data_buf);
    else
        data_ref = av_buffer_create(data, data_size, NULL, NULL, 0);
    if (!data_ref) {
        if (!data_buf)
            av_free(data);
        return AVERROR(ENOMEM);
    }

    err = cbs_insert_unit(frag, position);
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

int ff_cbs_append_unit_data(CodedBitstreamFragment *frag,
                            CodedBitstreamUnitType type,
                            uint8_t *data, size_t data_size,
                            AVBufferRef *data_buf)
{
    return cbs_insert_unit_data(frag, type,
                                data, data_size, data_buf,
                                frag->nb_units);
}

void ff_cbs_delete_unit(CodedBitstreamFragment *frag,
                        int position)
{
    av_assert0(0 <= position && position < frag->nb_units
                             && "Unit to be deleted not in fragment.");

    cbs_unit_uninit(&frag->units[position]);

    --frag->nb_units;

    if (frag->nb_units > 0)
        memmove(frag->units + position,
                frag->units + position + 1,
                (frag->nb_units - position) * sizeof(*frag->units));
}

static void cbs_default_free_unit_content(FFRefStructOpaque opaque, void *content)
{
    const CodedBitstreamUnitTypeDescriptor *desc = opaque.c;

    for (int i = 0; i < desc->type.ref.nb_offsets; i++) {
        void **ptr = (void**)((char*)content + desc->type.ref.offsets[i]);
        av_buffer_unref((AVBufferRef**)(ptr + 1));
    }
}

static const CodedBitstreamUnitTypeDescriptor
    *cbs_find_unit_type_desc(CodedBitstreamContext *ctx,
                             CodedBitstreamUnit *unit)
{
    const CodedBitstreamUnitTypeDescriptor *desc;
    int i, j;

    if (!ctx->codec->unit_types)
        return NULL;

    for (i = 0;; i++) {
        desc = &ctx->codec->unit_types[i];
        if (desc->nb_unit_types == 0)
            break;
        if (desc->nb_unit_types == CBS_UNIT_TYPE_RANGE) {
            if (unit->type >= desc->unit_type.range.start &&
                unit->type <= desc->unit_type.range.end)
                return desc;
        } else {
            for (j = 0; j < desc->nb_unit_types; j++) {
                if (desc->unit_type.list[j] == unit->type)
                    return desc;
            }
        }
    }
    return NULL;
}

static void *cbs_alloc_content(const CodedBitstreamUnitTypeDescriptor *desc)
{
    return ff_refstruct_alloc_ext_c(desc->content_size, 0,
                                    (FFRefStructOpaque){ .c = desc },
                                    desc->content_type == CBS_CONTENT_TYPE_COMPLEX
                                            ? desc->type.complex.content_free
                                            : cbs_default_free_unit_content);
}

int ff_cbs_alloc_unit_content(CodedBitstreamContext *ctx,
                              CodedBitstreamUnit *unit)
{
    const CodedBitstreamUnitTypeDescriptor *desc;

    av_assert0(!unit->content && !unit->content_ref);

    desc = cbs_find_unit_type_desc(ctx, unit);
    if (!desc)
        return AVERROR(ENOSYS);

    unit->content_ref = cbs_alloc_content(desc);
    if (!unit->content_ref)
        return AVERROR(ENOMEM);
    unit->content = unit->content_ref;

    return 0;
}

static int cbs_clone_noncomplex_unit_content(void **clonep,
                                             const CodedBitstreamUnit *unit,
                                             const CodedBitstreamUnitTypeDescriptor *desc)
{
    const uint8_t *src;
    uint8_t *copy;
    int err, i;

    av_assert0(unit->content);
    src = unit->content;

    copy = cbs_alloc_content(desc);
    if (!copy)
        return AVERROR(ENOMEM);
    memcpy(copy, src, desc->content_size);
    for (int i = 0; i < desc->type.ref.nb_offsets; i++) {
        void **ptr = (void**)(copy + desc->type.ref.offsets[i]);
        /* Zero all the AVBufferRefs as they are owned by src. */
        *(ptr + 1) = NULL;
    }

    for (i = 0; i < desc->type.ref.nb_offsets; i++) {
        const uint8_t *const *src_ptr = (const uint8_t* const*)(src + desc->type.ref.offsets[i]);
        const AVBufferRef *src_buf = *(AVBufferRef**)(src_ptr + 1);
        uint8_t **copy_ptr = (uint8_t**)(copy + desc->type.ref.offsets[i]);
        AVBufferRef **copy_buf = (AVBufferRef**)(copy_ptr + 1);

        if (!*src_ptr) {
            av_assert0(!src_buf);
            continue;
        }
        if (!src_buf) {
            // We can't handle a non-refcounted pointer here - we don't
            // have enough information to handle whatever structure lies
            // at the other end of it.
            err = AVERROR(EINVAL);
            goto fail;
        }

        *copy_buf = av_buffer_ref(src_buf);
        if (!*copy_buf) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
    }
    *clonep = copy;

    return 0;

fail:
    ff_refstruct_unref(&copy);
    return err;
}

/*
 * On success, unit->content and unit->content_ref are updated with
 * the new content; unit is untouched on failure.
 * Any old content_ref is simply overwritten and not freed.
 */
static int cbs_clone_unit_content(CodedBitstreamContext *ctx,
                                  CodedBitstreamUnit *unit)
{
    const CodedBitstreamUnitTypeDescriptor *desc;
    void *new_content;
    int err;

    desc = cbs_find_unit_type_desc(ctx, unit);
    if (!desc)
        return AVERROR(ENOSYS);

    switch (desc->content_type) {
    case CBS_CONTENT_TYPE_INTERNAL_REFS:
        err = cbs_clone_noncomplex_unit_content(&new_content, unit, desc);
        break;

    case CBS_CONTENT_TYPE_COMPLEX:
        if (!desc->type.complex.content_clone)
            return AVERROR_PATCHWELCOME;
        err = desc->type.complex.content_clone(&new_content, unit);
        break;

    default:
        av_assert0(0 && "Invalid content type.");
    }

    if (err < 0)
        return err;

    unit->content_ref = new_content;
    unit->content     = new_content;
    return 0;
}

int ff_cbs_make_unit_refcounted(CodedBitstreamContext *ctx,
                                CodedBitstreamUnit *unit)
{
    av_assert0(unit->content);
    if (unit->content_ref)
        return 0;
    return cbs_clone_unit_content(ctx, unit);
}

int ff_cbs_make_unit_writable(CodedBitstreamContext *ctx,
                              CodedBitstreamUnit *unit)
{
    void *ref = unit->content_ref;
    int err;

    av_assert0(unit->content);
    if (ref && ff_refstruct_exclusive(ref))
        return 0;

    err = cbs_clone_unit_content(ctx, unit);
    if (err < 0)
        return err;
    ff_refstruct_unref(&ref);
    return 0;
}

void ff_cbs_discard_units(CodedBitstreamContext *ctx,
                          CodedBitstreamFragment *frag,
                          enum AVDiscard skip,
                          int flags)
{
    if (!ctx->codec->discarded_unit)
        return;

    for (int i = frag->nb_units - 1; i >= 0; i--) {
        if (ctx->codec->discarded_unit(ctx, &frag->units[i], skip)) {
            // discard all units
            if (!(flags & DISCARD_FLAG_KEEP_NON_VCL)) {
                ff_cbs_fragment_free(frag);
                return;
            }

            ff_cbs_delete_unit(frag, i);
        }
    }
}
