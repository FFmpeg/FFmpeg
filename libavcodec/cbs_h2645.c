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

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"

#include "bytestream.h"
#include "cbs.h"
#include "cbs_internal.h"
#include "cbs_h264.h"
#include "cbs_h265.h"
#include "golomb.h"
#include "h264.h"
#include "h264_sei.h"
#include "h2645_parse.h"
#include "hevc.h"


static int cbs_read_ue_golomb(CodedBitstreamContext *ctx, GetBitContext *gbc,
                              const char *name, uint32_t *write_to,
                              uint32_t range_min, uint32_t range_max)
{
    uint32_t value;
    int position, i, j;
    unsigned int k;
    char bits[65];

    position = get_bits_count(gbc);

    for (i = 0; i < 32; i++) {
        if (get_bits_left(gbc) < i + 1) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid ue-golomb code at "
                   "%s: bitstream ended.\n", name);
            return AVERROR_INVALIDDATA;
        }
        k = get_bits1(gbc);
        bits[i] = k ? '1' : '0';
        if (k)
            break;
    }
    if (i >= 32) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid ue-golomb code at "
               "%s: more than 31 zeroes.\n", name);
        return AVERROR_INVALIDDATA;
    }
    value = 1;
    for (j = 0; j < i; j++) {
        k = get_bits1(gbc);
        bits[i + j + 1] = k ? '1' : '0';
        value = value << 1 | k;
    }
    bits[i + j + 1] = 0;
    --value;

    if (ctx->trace_enable)
        ff_cbs_trace_syntax_element(ctx, position, name, bits, value);

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRIu32", but must be in [%"PRIu32",%"PRIu32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }

    *write_to = value;
    return 0;
}

static int cbs_read_se_golomb(CodedBitstreamContext *ctx, GetBitContext *gbc,
                              const char *name, int32_t *write_to,
                              int32_t range_min, int32_t range_max)
{
    int32_t value;
    int position, i, j;
    unsigned int k;
    uint32_t v;
    char bits[65];

    position = get_bits_count(gbc);

    for (i = 0; i < 32; i++) {
        if (get_bits_left(gbc) < i + 1) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid se-golomb code at "
                   "%s: bitstream ended.\n", name);
            return AVERROR_INVALIDDATA;
        }
        k = get_bits1(gbc);
        bits[i] = k ? '1' : '0';
        if (k)
            break;
    }
    if (i >= 32) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid se-golomb code at "
               "%s: more than 31 zeroes.\n", name);
        return AVERROR_INVALIDDATA;
    }
    v = 1;
    for (j = 0; j < i; j++) {
        k = get_bits1(gbc);
        bits[i + j + 1] = k ? '1' : '0';
        v = v << 1 | k;
    }
    bits[i + j + 1] = 0;
    if (v & 1)
        value = -(int32_t)(v / 2);
    else
        value = v / 2;

    if (ctx->trace_enable)
        ff_cbs_trace_syntax_element(ctx, position, name, bits, value);

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRId32", but must be in [%"PRId32",%"PRId32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }

    *write_to = value;
    return 0;
}

static int cbs_write_ue_golomb(CodedBitstreamContext *ctx, PutBitContext *pbc,
                               const char *name, uint32_t value,
                               uint32_t range_min, uint32_t range_max)
{
    int len;

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRIu32", but must be in [%"PRIu32",%"PRIu32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }
    av_assert0(value != UINT32_MAX);

    len = av_log2(value + 1);
    if (put_bits_left(pbc) < 2 * len + 1)
        return AVERROR(ENOSPC);

    if (ctx->trace_enable) {
        char bits[65];
        int i;

        for (i = 0; i < len; i++)
            bits[i] = '0';
        bits[len] = '1';
        for (i = 0; i < len; i++)
            bits[len + i + 1] = (value + 1) >> (len - i - 1) & 1 ? '1' : '0';
        bits[len + len + 1] = 0;

        ff_cbs_trace_syntax_element(ctx, put_bits_count(pbc), name, bits, value);
    }

    put_bits(pbc, len, 0);
    if (len + 1 < 32)
        put_bits(pbc, len + 1, value + 1);
    else
        put_bits32(pbc, value + 1);

    return 0;
}

static int cbs_write_se_golomb(CodedBitstreamContext *ctx, PutBitContext *pbc,
                               const char *name, int32_t value,
                               int32_t range_min, int32_t range_max)
{
    int len;
    uint32_t uvalue;

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRId32", but must be in [%"PRId32",%"PRId32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }
    av_assert0(value != INT32_MIN);

    if (value == 0)
        uvalue = 0;
    else if (value > 0)
        uvalue = 2 * (uint32_t)value - 1;
    else
        uvalue = 2 * (uint32_t)-value;

    len = av_log2(uvalue + 1);
    if (put_bits_left(pbc) < 2 * len + 1)
        return AVERROR(ENOSPC);

    if (ctx->trace_enable) {
        char bits[65];
        int i;

        for (i = 0; i < len; i++)
            bits[i] = '0';
        bits[len] = '1';
        for (i = 0; i < len; i++)
            bits[len + i + 1] = (uvalue + 1) >> (len - i - 1) & 1 ? '1' : '0';
        bits[len + len + 1] = 0;

        ff_cbs_trace_syntax_element(ctx, put_bits_count(pbc), name, bits, value);
    }

    put_bits(pbc, len, 0);
    if (len + 1 < 32)
        put_bits(pbc, len + 1, uvalue + 1);
    else
        put_bits32(pbc, uvalue + 1);

    return 0;
}

#define HEADER(name) do { \
        ff_cbs_trace_header(ctx, name); \
    } while (0)

#define CHECK(call) do { \
        err = (call); \
        if (err < 0) \
            return err; \
    } while (0)

#define FUNC_NAME(rw, codec, name) cbs_ ## codec ## _ ## rw ## _ ## name
#define FUNC_H264(rw, name) FUNC_NAME(rw, h264, name)
#define FUNC_H265(rw, name) FUNC_NAME(rw, h265, name)


#define READ
#define READWRITE read
#define RWContext GetBitContext

#define xu(width, name, var, range_min, range_max) do { \
        uint32_t value = range_min; \
        CHECK(ff_cbs_read_unsigned(ctx, rw, width, #name, \
                                   &value, range_min, range_max)); \
        var = value; \
    } while (0)
#define xue(name, var, range_min, range_max) do { \
        uint32_t value = range_min; \
        CHECK(cbs_read_ue_golomb(ctx, rw, #name, \
                                 &value, range_min, range_max)); \
        var = value; \
    } while (0)
#define xse(name, var, range_min, range_max) do { \
        int32_t value = range_min; \
        CHECK(cbs_read_se_golomb(ctx, rw, #name, \
                                 &value, range_min, range_max)); \
        var = value; \
    } while (0)


#define u(width, name, range_min, range_max) \
        xu(width, name, current->name, range_min, range_max)
#define flag(name) u(1, name, 0, 1)
#define ue(name, range_min, range_max) \
        xue(name, current->name, range_min, range_max)
#define se(name, range_min, range_max) \
        xse(name, current->name, range_min, range_max)

#define infer(name, value) do { \
        current->name = value; \
    } while (0)

static int cbs_h2645_read_more_rbsp_data(GetBitContext *gbc)
{
    int bits_left = get_bits_left(gbc);
    if (bits_left > 8)
        return 1;
    if (show_bits(gbc, bits_left) == 1 << (bits_left - 1))
        return 0;
    return 1;
}

#define more_rbsp_data(var) ((var) = cbs_h2645_read_more_rbsp_data(rw))

#define byte_alignment(rw) (get_bits_count(rw) % 8)

#define allocate(name, size) do { \
        name = av_mallocz(size); \
        if (!name) \
            return AVERROR(ENOMEM); \
    } while (0)

#define FUNC(name) FUNC_H264(READWRITE, name)
#include "cbs_h264_syntax_template.c"
#undef FUNC

#define FUNC(name) FUNC_H265(READWRITE, name)
#include "cbs_h265_syntax_template.c"
#undef FUNC

#undef READ
#undef READWRITE
#undef RWContext
#undef xu
#undef xue
#undef xse
#undef u
#undef flag
#undef ue
#undef se
#undef infer
#undef more_rbsp_data
#undef byte_alignment
#undef allocate


#define WRITE
#define READWRITE write
#define RWContext PutBitContext

#define xu(width, name, var, range_min, range_max) do { \
        uint32_t value = var; \
        CHECK(ff_cbs_write_unsigned(ctx, rw, width, #name, \
                                    value, range_min, range_max)); \
    } while (0)
#define xue(name, var, range_min, range_max) do { \
        uint32_t value = var; \
        CHECK(cbs_write_ue_golomb(ctx, rw, #name, \
                                  value, range_min, range_max)); \
    } while (0)
#define xse(name, var, range_min, range_max) do { \
        int32_t value = var; \
        CHECK(cbs_write_se_golomb(ctx, rw, #name, \
                                  value, range_min, range_max)); \
    } while (0)

#define u(width, name, range_min, range_max) \
        xu(width, name, current->name, range_min, range_max)
#define flag(name) u(1, name, 0, 1)
#define ue(name, range_min, range_max) \
        xue(name, current->name, range_min, range_max)
#define se(name, range_min, range_max) \
        xse(name, current->name, range_min, range_max)

#define infer(name, value) do { \
        if (current->name != (value)) { \
            av_log(ctx->log_ctx, AV_LOG_WARNING, "Warning: " \
                   "%s does not match inferred value: " \
                   "%"PRId64", but should be %"PRId64".\n", \
                   #name, (int64_t)current->name, (int64_t)(value)); \
        } \
    } while (0)

#define more_rbsp_data(var) (var)

#define byte_alignment(rw) (put_bits_count(rw) % 8)

#define allocate(name, size) do { \
        if (!name) { \
            av_log(ctx->log_ctx, AV_LOG_ERROR, "%s must be set " \
                   "for writing.\n", #name); \
            return AVERROR_INVALIDDATA; \
        } \
    } while (0)

#define FUNC(name) FUNC_H264(READWRITE, name)
#include "cbs_h264_syntax_template.c"
#undef FUNC

#define FUNC(name) FUNC_H265(READWRITE, name)
#include "cbs_h265_syntax_template.c"
#undef FUNC

#undef WRITE
#undef READWRITE
#undef RWContext
#undef xu
#undef xue
#undef xse
#undef u
#undef flag
#undef ue
#undef se
#undef infer
#undef more_rbsp_data
#undef byte_alignment
#undef allocate


static void cbs_h264_free_sei(H264RawSEI *sei)
{
    int i;
    for (i = 0; i < sei->payload_count; i++) {
        H264RawSEIPayload *payload = &sei->payload[i];

        switch (payload->payload_type) {
        case H264_SEI_TYPE_BUFFERING_PERIOD:
        case H264_SEI_TYPE_PIC_TIMING:
        case H264_SEI_TYPE_RECOVERY_POINT:
        case H264_SEI_TYPE_DISPLAY_ORIENTATION:
            break;
        case H264_SEI_TYPE_USER_DATA_REGISTERED:
            av_freep(&payload->payload.user_data_registered.data);
            break;
        case H264_SEI_TYPE_USER_DATA_UNREGISTERED:
            av_freep(&payload->payload.user_data_unregistered.data);
            break;
        default:
            av_freep(&payload->payload.other.data);
            break;
        }
    }
}

static void cbs_h264_free_slice(H264RawSlice *slice)
{
    av_freep(&slice->data);
}

static void cbs_h264_free_nal_unit(CodedBitstreamUnit *unit)
{
    switch (unit->type) {
    case H264_NAL_SEI:
        cbs_h264_free_sei(unit->content);
        break;
    case H264_NAL_IDR_SLICE:
    case H264_NAL_SLICE:
        cbs_h264_free_slice(unit->content);
        break;
    }
    av_freep(&unit->content);
}

static void cbs_h265_free_nal_unit(CodedBitstreamUnit *unit)
{
    switch (unit->type) {
    case HEVC_NAL_VPS:
        av_freep(&((H265RawVPS*)unit->content)->extension_data.data);
        break;
    case HEVC_NAL_SPS:
        av_freep(&((H265RawSPS*)unit->content)->extension_data.data);
        break;
    case HEVC_NAL_PPS:
        av_freep(&((H265RawPPS*)unit->content)->extension_data.data);
        break;
    case HEVC_NAL_TRAIL_N:
    case HEVC_NAL_TRAIL_R:
    case HEVC_NAL_TSA_N:
    case HEVC_NAL_TSA_R:
    case HEVC_NAL_STSA_N:
    case HEVC_NAL_STSA_R:
    case HEVC_NAL_RADL_N:
    case HEVC_NAL_RADL_R:
    case HEVC_NAL_RASL_N:
    case HEVC_NAL_RASL_R:
    case HEVC_NAL_BLA_W_LP:
    case HEVC_NAL_BLA_W_RADL:
    case HEVC_NAL_BLA_N_LP:
    case HEVC_NAL_IDR_W_RADL:
    case HEVC_NAL_IDR_N_LP:
    case HEVC_NAL_CRA_NUT:
        av_freep(&((H265RawSlice*)unit->content)->data);
        break;
    }
    av_freep(&unit->content);
}

static int cbs_h2645_fragment_add_nals(CodedBitstreamContext *ctx,
                                       CodedBitstreamFragment *frag,
                                       const H2645Packet *packet)
{
    int err, i;

    for (i = 0; i < packet->nb_nals; i++) {
        const H2645NAL *nal = &packet->nals[i];
        size_t size = nal->size;
        uint8_t *data;

        // Remove trailing zeroes.
        while (size > 0 && nal->data[size - 1] == 0)
            --size;
        av_assert0(size > 0);

        data = av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!data)
            return AVERROR(ENOMEM);
        memcpy(data, nal->data, size);
        memset(data + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

        err = ff_cbs_insert_unit_data(ctx, frag, -1, nal->type,
                                      data, size);
        if (err < 0) {
            av_freep(&data);
            return err;
        }
    }

    return 0;
}

static int cbs_h2645_split_fragment(CodedBitstreamContext *ctx,
                                    CodedBitstreamFragment *frag,
                                    int header)
{
    enum AVCodecID codec_id = ctx->codec->codec_id;
    CodedBitstreamH2645Context *priv = ctx->priv_data;
    GetByteContext gbc;
    int err;

    av_assert0(frag->data && frag->nb_units == 0);
    if (frag->data_size == 0)
        return 0;

    if (header && frag->data[0] && codec_id == AV_CODEC_ID_H264) {
        // AVCC header.
        size_t size, start, end;
        int i, count, version;

        priv->mp4 = 1;

        bytestream2_init(&gbc, frag->data, frag->data_size);

        if (bytestream2_get_bytes_left(&gbc) < 6)
            return AVERROR_INVALIDDATA;

        version = bytestream2_get_byte(&gbc);
        if (version != 1) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid AVCC header: "
                   "first byte %u.", version);
            return AVERROR_INVALIDDATA;
        }

        bytestream2_skip(&gbc, 3);
        priv->nal_length_size = (bytestream2_get_byte(&gbc) & 3) + 1;

        // SPS array.
        count = bytestream2_get_byte(&gbc) & 0x1f;
        start = bytestream2_tell(&gbc);
        for (i = 0; i < count; i++) {
            if (bytestream2_get_bytes_left(&gbc) < 2 * (count - i))
                return AVERROR_INVALIDDATA;
            size = bytestream2_get_be16(&gbc);
            if (bytestream2_get_bytes_left(&gbc) < size)
                return AVERROR_INVALIDDATA;
            bytestream2_skip(&gbc, size);
        }
        end = bytestream2_tell(&gbc);

        err = ff_h2645_packet_split(&priv->read_packet,
                                    frag->data + start, end - start,
                                    ctx->log_ctx, 1, 2, AV_CODEC_ID_H264, 1);
        if (err < 0) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Failed to split AVCC SPS array.\n");
            return err;
        }
        err = cbs_h2645_fragment_add_nals(ctx, frag, &priv->read_packet);
        if (err < 0)
            return err;

        // PPS array.
        count = bytestream2_get_byte(&gbc);
        start = bytestream2_tell(&gbc);
        for (i = 0; i < count; i++) {
            if (bytestream2_get_bytes_left(&gbc) < 2 * (count - i))
                return AVERROR_INVALIDDATA;
            size = bytestream2_get_be16(&gbc);
            if (bytestream2_get_bytes_left(&gbc) < size)
                return AVERROR_INVALIDDATA;
            bytestream2_skip(&gbc, size);
        }
        end = bytestream2_tell(&gbc);

        err = ff_h2645_packet_split(&priv->read_packet,
                                    frag->data + start, end - start,
                                    ctx->log_ctx, 1, 2, AV_CODEC_ID_H264, 1);
        if (err < 0) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Failed to split AVCC PPS array.\n");
            return err;
        }
        err = cbs_h2645_fragment_add_nals(ctx, frag, &priv->read_packet);
        if (err < 0)
            return err;

        if (bytestream2_get_bytes_left(&gbc) > 0) {
            av_log(ctx->log_ctx, AV_LOG_WARNING, "%u bytes left at end of AVCC "
                   "header.\n", bytestream2_get_bytes_left(&gbc));
        }

    } else if (header && frag->data[0] && codec_id == AV_CODEC_ID_HEVC) {
        // HVCC header.
        size_t size, start, end;
        int i, j, nb_arrays, nal_unit_type, nb_nals, version;

        priv->mp4 = 1;

        bytestream2_init(&gbc, frag->data, frag->data_size);

        if (bytestream2_get_bytes_left(&gbc) < 23)
            return AVERROR_INVALIDDATA;

        version = bytestream2_get_byte(&gbc);
        if (version != 1) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid HVCC header: "
                   "first byte %u.", version);
            return AVERROR_INVALIDDATA;
        }

        bytestream2_skip(&gbc, 20);
        priv->nal_length_size = (bytestream2_get_byte(&gbc) & 3) + 1;

        nb_arrays = bytestream2_get_byte(&gbc);
        for (i = 0; i < nb_arrays; i++) {
            nal_unit_type = bytestream2_get_byte(&gbc) & 0x3f;
            nb_nals = bytestream2_get_be16(&gbc);

            start = bytestream2_tell(&gbc);
            for (j = 0; j < nb_nals; j++) {
                if (bytestream2_get_bytes_left(&gbc) < 2)
                    return AVERROR_INVALIDDATA;
                size = bytestream2_get_be16(&gbc);
                if (bytestream2_get_bytes_left(&gbc) < size)
                    return AVERROR_INVALIDDATA;
                bytestream2_skip(&gbc, size);
            }
            end = bytestream2_tell(&gbc);

            err = ff_h2645_packet_split(&priv->read_packet,
                                        frag->data + start, end - start,
                                        ctx->log_ctx, 1, 2, AV_CODEC_ID_HEVC, 1);
            if (err < 0) {
                av_log(ctx->log_ctx, AV_LOG_ERROR, "Failed to split "
                       "HVCC array %d (%d NAL units of type %d).\n",
                       i, nb_nals, nal_unit_type);
                return err;
            }
            err = cbs_h2645_fragment_add_nals(ctx, frag, &priv->read_packet);
            if (err < 0)
                return err;
        }

    } else {
        // Annex B, or later MP4 with already-known parameters.

        err = ff_h2645_packet_split(&priv->read_packet,
                                    frag->data, frag->data_size,
                                    ctx->log_ctx,
                                    priv->mp4, priv->nal_length_size,
                                    codec_id, 1);
        if (err < 0)
            return err;

        err = cbs_h2645_fragment_add_nals(ctx, frag, &priv->read_packet);
        if (err < 0)
            return err;
    }

    return 0;
}

#define cbs_h2645_replace_ps(h26n, ps_name, ps_var, id_element) \
static int cbs_h26 ## h26n ## _replace_ ## ps_var(CodedBitstreamContext *ctx, \
                                                  const H26 ## h26n ## Raw ## ps_name *ps_var)  \
{ \
    CodedBitstreamH26 ## h26n ## Context *priv = ctx->priv_data; \
    unsigned int id = ps_var->id_element; \
    if (id > FF_ARRAY_ELEMS(priv->ps_var)) { \
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid " #ps_name \
               " id : %d.\n", id); \
        return AVERROR_INVALIDDATA; \
    } \
    av_freep(&priv->ps_var[id]); \
    priv->ps_var[id] = av_malloc(sizeof(*ps_var)); \
    if (!priv->ps_var[id]) \
        return AVERROR(ENOMEM); \
    memcpy(priv->ps_var[id], ps_var, sizeof(*ps_var)); \
    return 0; \
}

cbs_h2645_replace_ps(4, SPS, sps, seq_parameter_set_id)
cbs_h2645_replace_ps(4, PPS, pps, pic_parameter_set_id)
cbs_h2645_replace_ps(5, VPS, vps, vps_video_parameter_set_id)
cbs_h2645_replace_ps(5, SPS, sps, sps_seq_parameter_set_id)
cbs_h2645_replace_ps(5, PPS, pps, pps_pic_parameter_set_id)

static int cbs_h264_read_nal_unit(CodedBitstreamContext *ctx,
                                  CodedBitstreamUnit *unit)
{
    GetBitContext gbc;
    int err;

    err = init_get_bits(&gbc, unit->data, 8 * unit->data_size);
    if (err < 0)
        return err;

    switch (unit->type) {
    case H264_NAL_SPS:
        {
            H264RawSPS *sps;

            sps = av_mallocz(sizeof(*sps));
            if (!sps)
                return AVERROR(ENOMEM);
            err = cbs_h264_read_sps(ctx, &gbc, sps);
            if (err >= 0)
                err = cbs_h264_replace_sps(ctx, sps);
            if (err < 0) {
                av_free(sps);
                return err;
            }

            unit->content = sps;
        }
        break;

    case H264_NAL_SPS_EXT:
        {
            H264RawSPSExtension *sps_ext;

            sps_ext = av_mallocz(sizeof(*sps_ext));
            if (!sps_ext)
                return AVERROR(ENOMEM);
            err = cbs_h264_read_sps_extension(ctx, &gbc, sps_ext);
            if (err < 0) {
                av_free(sps_ext);
                return err;
            }

            unit->content = sps_ext;
        }
        break;

    case H264_NAL_PPS:
        {
            H264RawPPS *pps;

            pps = av_mallocz(sizeof(*pps));
            if (!pps)
                return AVERROR(ENOMEM);
            err = cbs_h264_read_pps(ctx, &gbc, pps);
            if (err >= 0)
                err = cbs_h264_replace_pps(ctx, pps);
            if (err < 0) {
                av_free(pps);
                return err;
            }

            unit->content = pps;
        }
        break;

    case H264_NAL_SLICE:
    case H264_NAL_IDR_SLICE:
    case H264_NAL_AUXILIARY_SLICE:
        {
            H264RawSlice *slice;
            int pos, len;

            slice = av_mallocz(sizeof(*slice));
            if (!slice)
                return AVERROR(ENOMEM);
            err = cbs_h264_read_slice_header(ctx, &gbc, &slice->header);
            if (err < 0) {
                av_free(slice);
                return err;
            }

            pos = get_bits_count(&gbc);
            len = unit->data_size;
            if (!unit->data[len - 1]) {
                int z;
                for (z = 0; z < len && !unit->data[len - z - 1]; z++);
                av_log(ctx->log_ctx, AV_LOG_DEBUG, "Deleted %d trailing zeroes "
                       "from slice data.\n", z);
                len -= z;
            }

            slice->data_size = len - pos / 8;
            slice->data = av_malloc(slice->data_size +
                                    AV_INPUT_BUFFER_PADDING_SIZE);
            if (!slice->data) {
                av_free(slice);
                return AVERROR(ENOMEM);
            }
            memcpy(slice->data,
                   unit->data + pos / 8, slice->data_size);
            memset(slice->data + slice->data_size, 0,
                   AV_INPUT_BUFFER_PADDING_SIZE);
            slice->data_bit_start = pos % 8;

            unit->content = slice;
        }
        break;

    case H264_NAL_AUD:
        {
            H264RawAUD *aud;

            aud = av_mallocz(sizeof(*aud));
            if (!aud)
                return AVERROR(ENOMEM);
            err = cbs_h264_read_aud(ctx, &gbc, aud);
            if (err < 0) {
                av_free(aud);
                return err;
            }

            unit->content = aud;
        }
        break;

    case H264_NAL_SEI:
        {
            H264RawSEI *sei;

            sei = av_mallocz(sizeof(*sei));
            if (!sei)
                return AVERROR(ENOMEM);
            err = cbs_h264_read_sei(ctx, &gbc, sei);
            if (err < 0) {
                cbs_h264_free_sei(sei);
                av_free(sei);
                return err;
            }

            unit->content = sei;
        }
        break;

    default:
        return AVERROR(ENOSYS);
    }

    return 0;
}

static int cbs_h265_read_nal_unit(CodedBitstreamContext *ctx,
                                  CodedBitstreamUnit *unit)
{
    GetBitContext gbc;
    int err;

    err = init_get_bits(&gbc, unit->data, 8 * unit->data_size);
    if (err < 0)
        return err;

    switch (unit->type) {
    case HEVC_NAL_VPS:
        {
            H265RawVPS *vps;

            vps = av_mallocz(sizeof(*vps));
            if (!vps)
                return AVERROR(ENOMEM);
            err = cbs_h265_read_vps(ctx, &gbc, vps);
            if (err >= 0)
                err = cbs_h265_replace_vps(ctx, vps);
            if (err < 0) {
                av_free(vps);
                return err;
            }

            unit->content = vps;
        }
        break;
    case HEVC_NAL_SPS:
        {
            H265RawSPS *sps;

            sps = av_mallocz(sizeof(*sps));
            if (!sps)
                return AVERROR(ENOMEM);
            err = cbs_h265_read_sps(ctx, &gbc, sps);
            if (err >= 0)
                err = cbs_h265_replace_sps(ctx, sps);
            if (err < 0) {
                av_free(sps);
                return err;
            }

            unit->content = sps;
        }
        break;

    case HEVC_NAL_PPS:
        {
            H265RawPPS *pps;

            pps = av_mallocz(sizeof(*pps));
            if (!pps)
                return AVERROR(ENOMEM);
            err = cbs_h265_read_pps(ctx, &gbc, pps);
            if (err >= 0)
                err = cbs_h265_replace_pps(ctx, pps);
            if (err < 0) {
                av_free(pps);
                return err;
            }

            unit->content = pps;
        }
        break;

    case HEVC_NAL_TRAIL_N:
    case HEVC_NAL_TRAIL_R:
    case HEVC_NAL_TSA_N:
    case HEVC_NAL_TSA_R:
    case HEVC_NAL_STSA_N:
    case HEVC_NAL_STSA_R:
    case HEVC_NAL_RADL_N:
    case HEVC_NAL_RADL_R:
    case HEVC_NAL_RASL_N:
    case HEVC_NAL_RASL_R:
    case HEVC_NAL_BLA_W_LP:
    case HEVC_NAL_BLA_W_RADL:
    case HEVC_NAL_BLA_N_LP:
    case HEVC_NAL_IDR_W_RADL:
    case HEVC_NAL_IDR_N_LP:
    case HEVC_NAL_CRA_NUT:
        {
            H265RawSlice *slice;
            int pos, len;

            slice = av_mallocz(sizeof(*slice));
            if (!slice)
                return AVERROR(ENOMEM);
            err = cbs_h265_read_slice_segment_header(ctx, &gbc, &slice->header);
            if (err < 0) {
                av_free(slice);
                return err;
            }

            pos = get_bits_count(&gbc);
            len = unit->data_size;
            if (!unit->data[len - 1]) {
                int z;
                for (z = 0; z < len && !unit->data[len - z - 1]; z++);
                av_log(ctx->log_ctx, AV_LOG_DEBUG, "Deleted %d trailing zeroes "
                       "from slice data.\n", z);
                len -= z;
            }

            slice->data_size = len - pos / 8;
            slice->data = av_malloc(slice->data_size +
                                    AV_INPUT_BUFFER_PADDING_SIZE);
            if (!slice->data) {
                av_free(slice);
                return AVERROR(ENOMEM);
            }
            memcpy(slice->data,
                   unit->data + pos / 8, slice->data_size);
            memset(slice->data + slice->data_size, 0,
                   AV_INPUT_BUFFER_PADDING_SIZE);
            slice->data_bit_start = pos % 8;

            unit->content = slice;
        }
        break;

    case HEVC_NAL_AUD:
        {
            H265RawAUD *aud;

            aud = av_mallocz(sizeof(*aud));
            if (!aud)
                return AVERROR(ENOMEM);
            err = cbs_h265_read_aud(ctx, &gbc, aud);
            if (err < 0) {
                av_free(aud);
                return err;
            }

            unit->content = aud;
        }
        break;

    default:
        return AVERROR(ENOSYS);
    }

    return 0;
}

static int cbs_h264_write_nal_unit(CodedBitstreamContext *ctx,
                                   CodedBitstreamUnit *unit,
                                   PutBitContext *pbc)
{
    int err;

    switch (unit->type) {
    case H264_NAL_SPS:
        {
            H264RawSPS *sps = unit->content;

            err = cbs_h264_write_sps(ctx, pbc, sps);
            if (err < 0)
                return err;

            err = cbs_h264_replace_sps(ctx, sps);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_SPS_EXT:
        {
            H264RawSPSExtension *sps_ext = unit->content;

            err = cbs_h264_write_sps_extension(ctx, pbc, sps_ext);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_PPS:
        {
            H264RawPPS *pps = unit->content;

            err = cbs_h264_write_pps(ctx, pbc, pps);
            if (err < 0)
                return err;

            err = cbs_h264_replace_pps(ctx, pps);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_SLICE:
    case H264_NAL_IDR_SLICE:
    case H264_NAL_AUXILIARY_SLICE:
        {
            H264RawSlice *slice = unit->content;
            GetBitContext gbc;
            int bits_left, end, zeroes;

            err = cbs_h264_write_slice_header(ctx, pbc, &slice->header);
            if (err < 0)
                return err;

            if (slice->data) {
                if (slice->data_size * 8 + 8 > put_bits_left(pbc))
                    return AVERROR(ENOSPC);

                init_get_bits(&gbc, slice->data, slice->data_size * 8);
                skip_bits_long(&gbc, slice->data_bit_start);

                // Copy in two-byte blocks, but stop before copying the
                // rbsp_stop_one_bit in the final byte.
                while (get_bits_left(&gbc) > 23)
                    put_bits(pbc, 16, get_bits(&gbc, 16));

                bits_left = get_bits_left(&gbc);
                end = get_bits(&gbc, bits_left);

                // rbsp_stop_one_bit must be present here.
                av_assert0(end);
                zeroes = ff_ctz(end);
                if (bits_left > zeroes + 1)
                    put_bits(pbc, bits_left - zeroes - 1,
                             end >> (zeroes + 1));
                put_bits(pbc, 1, 1);
                while (put_bits_count(pbc) % 8 != 0)
                    put_bits(pbc, 1, 0);
            } else {
                // No slice data - that was just the header.
                // (Bitstream may be unaligned!)
            }
        }
        break;

    case H264_NAL_AUD:
        {
            err = cbs_h264_write_aud(ctx, pbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_SEI:
        {
            err = cbs_h264_write_sei(ctx, pbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    default:
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Write unimplemented for "
               "NAL unit type %"PRIu32".\n", unit->type);
        return AVERROR_PATCHWELCOME;
    }

    return 0;
}

static int cbs_h265_write_nal_unit(CodedBitstreamContext *ctx,
                                   CodedBitstreamUnit *unit,
                                   PutBitContext *pbc)
{
    int err;

    switch (unit->type) {
    case HEVC_NAL_VPS:
        {
            H265RawVPS *vps = unit->content;

            err = cbs_h265_write_vps(ctx, pbc, vps);
            if (err < 0)
                return err;

            err = cbs_h265_replace_vps(ctx, vps);
            if (err < 0)
                return err;
        }
        break;

    case HEVC_NAL_SPS:
        {
            H265RawSPS *sps = unit->content;

            err = cbs_h265_write_sps(ctx, pbc, sps);
            if (err < 0)
                return err;

            err = cbs_h265_replace_sps(ctx, sps);
            if (err < 0)
                return err;
        }
        break;

    case HEVC_NAL_PPS:
        {
            H265RawPPS *pps = unit->content;

            err = cbs_h265_write_pps(ctx, pbc, pps);
            if (err < 0)
                return err;

            err = cbs_h265_replace_pps(ctx, pps);
            if (err < 0)
                return err;
        }
        break;

    case HEVC_NAL_TRAIL_N:
    case HEVC_NAL_TRAIL_R:
    case HEVC_NAL_TSA_N:
    case HEVC_NAL_TSA_R:
    case HEVC_NAL_STSA_N:
    case HEVC_NAL_STSA_R:
    case HEVC_NAL_RADL_N:
    case HEVC_NAL_RADL_R:
    case HEVC_NAL_RASL_N:
    case HEVC_NAL_RASL_R:
    case HEVC_NAL_BLA_W_LP:
    case HEVC_NAL_BLA_W_RADL:
    case HEVC_NAL_BLA_N_LP:
    case HEVC_NAL_IDR_W_RADL:
    case HEVC_NAL_IDR_N_LP:
    case HEVC_NAL_CRA_NUT:
        {
            H265RawSlice *slice = unit->content;
            GetBitContext gbc;
            int bits_left, end, zeroes;

            err = cbs_h265_write_slice_segment_header(ctx, pbc, &slice->header);
            if (err < 0)
                return err;

            if (slice->data) {
                if (slice->data_size * 8 + 8 > put_bits_left(pbc))
                    return AVERROR(ENOSPC);

                init_get_bits(&gbc, slice->data, slice->data_size * 8);
                skip_bits_long(&gbc, slice->data_bit_start);

                // Copy in two-byte blocks, but stop before copying the
                // rbsp_stop_one_bit in the final byte.
                while (get_bits_left(&gbc) > 23)
                    put_bits(pbc, 16, get_bits(&gbc, 16));

                bits_left = get_bits_left(&gbc);
                end = get_bits(&gbc, bits_left);

                // rbsp_stop_one_bit must be present here.
                av_assert0(end);
                zeroes = ff_ctz(end);
                if (bits_left > zeroes + 1)
                    put_bits(pbc, bits_left - zeroes - 1,
                             end >> (zeroes + 1));
                put_bits(pbc, 1, 1);
                while (put_bits_count(pbc) % 8 != 0)
                    put_bits(pbc, 1, 0);
            } else {
                // No slice data - that was just the header.
            }
        }
        break;

    case HEVC_NAL_AUD:
        {
            err = cbs_h265_write_aud(ctx, pbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    default:
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Write unimplemented for "
               "NAL unit type %"PRIu32".\n", unit->type);
        return AVERROR_PATCHWELCOME;
    }

    return 0;
}

static int cbs_h2645_write_nal_unit(CodedBitstreamContext *ctx,
                                    CodedBitstreamUnit *unit)
{
    CodedBitstreamH2645Context *priv = ctx->priv_data;
    enum AVCodecID codec_id = ctx->codec->codec_id;
    PutBitContext pbc;
    int err;

    if (!priv->write_buffer) {
        // Initial write buffer size is 1MB.
        priv->write_buffer_size = 1024 * 1024;

    reallocate_and_try_again:
        err = av_reallocp(&priv->write_buffer, priv->write_buffer_size);
        if (err < 0) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Unable to allocate a "
                   "sufficiently large write buffer (last attempt "
                   "%zu bytes).\n", priv->write_buffer_size);
            return err;
        }
    }

    init_put_bits(&pbc, priv->write_buffer, priv->write_buffer_size);

    if (codec_id == AV_CODEC_ID_H264)
        err = cbs_h264_write_nal_unit(ctx, unit, &pbc);
    else
        err = cbs_h265_write_nal_unit(ctx, unit, &pbc);

    if (err == AVERROR(ENOSPC)) {
        // Overflow.
        priv->write_buffer_size *= 2;
        goto reallocate_and_try_again;
    }
    // Overflow but we didn't notice.
    av_assert0(put_bits_count(&pbc) <= 8 * priv->write_buffer_size);

    if (err < 0) {
        // Write failed for some other reason.
        return err;
    }

    if (put_bits_count(&pbc) % 8)
        unit->data_bit_padding = 8 - put_bits_count(&pbc) % 8;
    else
        unit->data_bit_padding = 0;

    unit->data_size = (put_bits_count(&pbc) + 7) / 8;
    flush_put_bits(&pbc);

    err = av_reallocp(&unit->data, unit->data_size);
    if (err < 0)
        return err;

    memcpy(unit->data, priv->write_buffer, unit->data_size);

    return 0;
}

static int cbs_h2645_assemble_fragment(CodedBitstreamContext *ctx,
                                       CodedBitstreamFragment *frag)
{
    uint8_t *data;
    size_t max_size, dp, sp;
    int err, i, zero_run;

    for (i = 0; i < frag->nb_units; i++) {
        // Data should already all have been written when we get here.
        av_assert0(frag->units[i].data);
    }

    max_size = 0;
    for (i = 0; i < frag->nb_units; i++) {
        // Start code + content with worst-case emulation prevention.
        max_size += 3 + frag->units[i].data_size * 3 / 2;
    }

    data = av_malloc(max_size);
    if (!data)
        return AVERROR(ENOMEM);

    dp = 0;
    for (i = 0; i < frag->nb_units; i++) {
        CodedBitstreamUnit *unit = &frag->units[i];

        if (unit->data_bit_padding > 0) {
            if (i < frag->nb_units - 1)
                av_log(ctx->log_ctx, AV_LOG_WARNING, "Probably invalid "
                       "unaligned padding on non-final NAL unit.\n");
            else
                frag->data_bit_padding = unit->data_bit_padding;
        }

        if ((ctx->codec->codec_id == AV_CODEC_ID_H264 &&
             (unit->type == H264_NAL_SPS ||
              unit->type == H264_NAL_PPS)) ||
            (ctx->codec->codec_id == AV_CODEC_ID_HEVC &&
             (unit->type == HEVC_NAL_VPS ||
              unit->type == HEVC_NAL_SPS ||
              unit->type == HEVC_NAL_PPS)) ||
            i == 0 /* (Assume this is the start of an access unit.) */) {
            // zero_byte
            data[dp++] = 0;
        }
        // start_code_prefix_one_3bytes
        data[dp++] = 0;
        data[dp++] = 0;
        data[dp++] = 1;

        zero_run = 0;
        for (sp = 0; sp < unit->data_size; sp++) {
            if (zero_run < 2) {
                if (unit->data[sp] == 0)
                    ++zero_run;
                else
                    zero_run = 0;
            } else {
                if ((unit->data[sp] & ~3) == 0) {
                    // emulation_prevention_three_byte
                    data[dp++] = 3;
                }
                zero_run = unit->data[sp] == 0;
            }
            data[dp++] = unit->data[sp];
        }
    }

    av_assert0(dp <= max_size);
    err = av_reallocp(&data, dp);
    if (err)
        return err;

    frag->data = data;
    frag->data_size = dp;

    return 0;
}

static void cbs_h264_close(CodedBitstreamContext *ctx)
{
    CodedBitstreamH264Context *h264 = ctx->priv_data;
    int i;

    ff_h2645_packet_uninit(&h264->common.read_packet);

    av_freep(&h264->common.write_buffer);

    for (i = 0; i < FF_ARRAY_ELEMS(h264->sps); i++)
        av_freep(&h264->sps[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(h264->pps); i++)
        av_freep(&h264->pps[i]);
}

static void cbs_h265_close(CodedBitstreamContext *ctx)
{
    CodedBitstreamH265Context *h265 = ctx->priv_data;
    int i;

    ff_h2645_packet_uninit(&h265->common.read_packet);

    av_freep(&h265->common.write_buffer);

    for (i = 0; i < FF_ARRAY_ELEMS(h265->vps); i++)
        av_freep(&h265->vps[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(h265->sps); i++)
        av_freep(&h265->sps[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(h265->pps); i++)
        av_freep(&h265->pps[i]);
}

const CodedBitstreamType ff_cbs_type_h264 = {
    .codec_id          = AV_CODEC_ID_H264,

    .priv_data_size    = sizeof(CodedBitstreamH264Context),

    .split_fragment    = &cbs_h2645_split_fragment,
    .read_unit         = &cbs_h264_read_nal_unit,
    .write_unit        = &cbs_h2645_write_nal_unit,
    .assemble_fragment = &cbs_h2645_assemble_fragment,

    .free_unit         = &cbs_h264_free_nal_unit,
    .close             = &cbs_h264_close,
};

const CodedBitstreamType ff_cbs_type_h265 = {
    .codec_id          = AV_CODEC_ID_HEVC,

    .priv_data_size    = sizeof(CodedBitstreamH265Context),

    .split_fragment    = &cbs_h2645_split_fragment,
    .read_unit         = &cbs_h265_read_nal_unit,
    .write_unit        = &cbs_h2645_write_nal_unit,
    .assemble_fragment = &cbs_h2645_assemble_fragment,

    .free_unit         = &cbs_h265_free_nal_unit,
    .close             = &cbs_h265_close,
};
