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

#include "libavutil/mem.h"
#include "libavutil/refstruct.h"
#include "bytestream.h"
#include "cbs.h"
#include "cbs_internal.h"
#include "cbs_h2645.h"
#include "cbs_h264.h"
#include "cbs_sei.h"
#include "get_bits.h"

#define HEADER(name) do { \
        ff_cbs_trace_header(ctx, name); \
    } while (0)

#define CHECK(call) do { \
        err = (call); \
        if (err < 0) \
            return err; \
    } while (0)

#define FUNC_NAME2(rw, codec, name) cbs_ ## codec ## _ ## rw ## _ ## name
#define FUNC_NAME1(rw, codec, name) FUNC_NAME2(rw, codec, name)
#define FUNC_H264(name) FUNC_NAME1(READWRITE, h264, name)
#define FUNC_NAME2_EXPORT(rw, codec, name) ff_cbs_ ## codec ## _ ## rw ## _ ## name
#define FUNC_NAME1_EXPORT(rw, codec, name) FUNC_NAME2_EXPORT(rw, codec, name)
#define FUNC_SEI(name)  FUNC_NAME1_EXPORT(READWRITE, sei,  name)

#define SEI_FUNC(name, args) \
static int FUNC_H264(name) args;  \
static int FUNC_H264(name ## _internal)(CodedBitstreamContext *ctx, \
                                   RWContext *rw, void *cur,   \
                                   SEIMessageState *state)     \
{ \
    return FUNC_H264(name)(ctx, rw, cur, state); \
} \
static int FUNC_H264(name) args

#define SUBSCRIPTS(subs, ...) (subs > 0 ? ((int[subs + 1]){ subs, __VA_ARGS__ }) : NULL)

#define u(width, name, range_min, range_max) \
        xu(width, name, current->name, range_min, range_max, 0, )
#define flag(name) ub(1, name)
#define ue(name, range_min, range_max) \
        xue(name, current->name, range_min, range_max, 0, )
#define i(width, name, range_min, range_max) \
        xi(width, name, current->name, range_min, range_max, 0, )
#define ib(width, name) \
        xi(width, name, current->name, MIN_INT_BITS(width), MAX_INT_BITS(width), 0, )
#define se(name, range_min, range_max) \
        xse(name, current->name, range_min, range_max, 0, )

#define us(width, name, range_min, range_max, subs, ...) \
        xu(width, name, current->name, range_min, range_max, subs, __VA_ARGS__)
#define ubs(width, name, subs, ...) \
        xu(width, name, current->name, 0, MAX_UINT_BITS(width), subs, __VA_ARGS__)
#define flags(name, subs, ...) \
        xu(1, name, current->name, 0, 1, subs, __VA_ARGS__)
#define ues(name, range_min, range_max, subs, ...) \
        xue(name, current->name, range_min, range_max, subs, __VA_ARGS__)
#define is(width, name, range_min, range_max, subs, ...) \
        xi(width, name, current->name, range_min, range_max, subs, __VA_ARGS__)
#define ibs(width, name, subs, ...) \
        xi(width, name, current->name, MIN_INT_BITS(width), MAX_INT_BITS(width), subs, __VA_ARGS__)
#define ses(name, range_min, range_max, subs, ...) \
        xse(name, current->name, range_min, range_max, subs, __VA_ARGS__)

#define fixed(width, name, value) do { \
        av_unused uint32_t fixed_value = value; \
        xu(width, name, fixed_value, value, value, 0, ); \
    } while (0)


#define READ
#define READWRITE read
#define RWContext GetBitContext

#define ub(width, name) do { \
        uint32_t value; \
        CHECK(ff_cbs_read_simple_unsigned(ctx, rw, width, #name, \
                                          &value)); \
        current->name = value; \
    } while (0)
#define xu(width, name, var, range_min, range_max, subs, ...) do { \
        uint32_t value; \
        CHECK(ff_cbs_read_unsigned(ctx, rw, width, #name, \
                                   SUBSCRIPTS(subs, __VA_ARGS__), \
                                   &value, range_min, range_max)); \
        var = value; \
    } while (0)
#define xue(name, var, range_min, range_max, subs, ...) do { \
        uint32_t value; \
        CHECK(ff_cbs_read_ue_golomb(ctx, rw, #name, \
                                 SUBSCRIPTS(subs, __VA_ARGS__), \
                                 &value, range_min, range_max)); \
        var = value; \
    } while (0)
#define xi(width, name, var, range_min, range_max, subs, ...) do { \
        int32_t value; \
        CHECK(ff_cbs_read_signed(ctx, rw, width, #name, \
                                 SUBSCRIPTS(subs, __VA_ARGS__), \
                                 &value, range_min, range_max)); \
        var = value; \
    } while (0)
#define xse(name, var, range_min, range_max, subs, ...) do { \
        int32_t value; \
        CHECK(ff_cbs_read_se_golomb(ctx, rw, #name, \
                                 SUBSCRIPTS(subs, __VA_ARGS__), \
                                 &value, range_min, range_max)); \
        var = value; \
    } while (0)


#define infer(name, value) do { \
        current->name = value; \
    } while (0)

#define more_rbsp_data(var) ((var) = ff_cbs_h2645_read_more_rbsp_data(rw))

#define bit_position(rw)   (get_bits_count(rw))
#define byte_alignment(rw) (get_bits_count(rw) % 8)

#define allocate(name, size) do { \
        name ## _ref = av_buffer_allocz(size + \
                                        AV_INPUT_BUFFER_PADDING_SIZE); \
        if (!name ## _ref) \
            return AVERROR(ENOMEM); \
        name = name ## _ref->data; \
    } while (0)

#define FUNC(name) FUNC_H264(name)
#include "cbs_h264_syntax_template.c"
#undef FUNC


#undef READ
#undef READWRITE
#undef RWContext
#undef ub
#undef xu
#undef xi
#undef xue
#undef xse
#undef infer
#undef more_rbsp_data
#undef bit_position
#undef byte_alignment
#undef allocate
#undef allocate_struct


#define WRITE
#define READWRITE write
#define RWContext PutBitContext

#define ub(width, name) do { \
        uint32_t value = current->name; \
        CHECK(ff_cbs_write_simple_unsigned(ctx, rw, width, #name, \
                                           value)); \
    } while (0)
#define xu(width, name, var, range_min, range_max, subs, ...) do { \
        uint32_t value = var; \
        CHECK(ff_cbs_write_unsigned(ctx, rw, width, #name, \
                                    SUBSCRIPTS(subs, __VA_ARGS__), \
                                    value, range_min, range_max)); \
    } while (0)
#define xue(name, var, range_min, range_max, subs, ...) do { \
        uint32_t value = var; \
        CHECK(ff_cbs_write_ue_golomb(ctx, rw, #name, \
                                  SUBSCRIPTS(subs, __VA_ARGS__), \
                                  value, range_min, range_max)); \
    } while (0)
#define xi(width, name, var, range_min, range_max, subs, ...) do { \
        int32_t value = var; \
        CHECK(ff_cbs_write_signed(ctx, rw, width, #name, \
                                  SUBSCRIPTS(subs, __VA_ARGS__), \
                                  value, range_min, range_max)); \
    } while (0)
#define xse(name, var, range_min, range_max, subs, ...) do { \
        int32_t value = var; \
        CHECK(ff_cbs_write_se_golomb(ctx, rw, #name, \
                                  SUBSCRIPTS(subs, __VA_ARGS__), \
                                  value, range_min, range_max)); \
    } while (0)

#define infer(name, value) do { \
        if (current->name != (value)) { \
            av_log(ctx->log_ctx, AV_LOG_ERROR, \
                   "%s does not match inferred value: " \
                   "%"PRId64", but should be %"PRId64".\n", \
                   #name, (int64_t)current->name, (int64_t)(value)); \
            return AVERROR_INVALIDDATA; \
        } \
    } while (0)

#define more_rbsp_data(var) (var)

#define bit_position(rw)   (put_bits_count(rw))
#define byte_alignment(rw) (put_bits_count(rw) % 8)

#define allocate(name, size) do { \
        if (!name) { \
            av_log(ctx->log_ctx, AV_LOG_ERROR, "%s must be set " \
                   "for writing.\n", #name); \
            return AVERROR_INVALIDDATA; \
        } \
    } while (0)

#define FUNC(name) FUNC_H264(name)
#include "cbs_h264_syntax_template.c"
#undef FUNC

#undef WRITE
#undef READWRITE
#undef RWContext
#undef ub
#undef xu
#undef xi
#undef xue
#undef xse
#undef u
#undef i
#undef flag
#undef ue
#undef se
#undef infer
#undef more_rbsp_data
#undef bit_position
#undef byte_alignment
#undef allocate



static int cbs_h264_split_fragment(CodedBitstreamContext *ctx,
                                   CodedBitstreamFragment *frag,
                                   int header)
{
    enum AVCodecID codec_id = ctx->codec->codec_id;
    CodedBitstreamH264Context *priv = ctx->priv_data;
    CodedBitstreamH2645Context *h2645 = &priv->common;
    GetByteContext gbc;
    int err;

    av_assert0(frag->data && frag->nb_units == 0);
    if (frag->data_size == 0)
        return 0;

    if (header && frag->data[0]) {
        // AVCC header.
        size_t size, start, end;
        int i, count, version;

        h2645->mp4 = 1;

        bytestream2_init(&gbc, frag->data, frag->data_size);

        if (bytestream2_get_bytes_left(&gbc) < 6)
            return AVERROR_INVALIDDATA;

        version = bytestream2_get_byte(&gbc);
        if (version != 1) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid AVCC header: "
                   "first byte %u.\n", version);
            return AVERROR_INVALIDDATA;
        }

        bytestream2_skip(&gbc, 3);
        h2645->nal_length_size = (bytestream2_get_byte(&gbc) & 3) + 1;

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

        err = ff_h2645_packet_split(&h2645->read_packet,
                                    frag->data + start, end - start,
                                    ctx->log_ctx, 2, AV_CODEC_ID_H264,
                                    H2645_FLAG_IS_NALFF | H2645_FLAG_SMALL_PADDING | H2645_FLAG_USE_REF);
        if (err < 0) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Failed to split AVCC SPS array.\n");
            return err;
        }
        err = ff_cbs_h2645_fragment_add_nals(ctx, frag, &h2645->read_packet);
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

        err = ff_h2645_packet_split(&h2645->read_packet,
                                    frag->data + start, end - start,
                                    ctx->log_ctx, 2, AV_CODEC_ID_H264,
                                    H2645_FLAG_IS_NALFF | H2645_FLAG_SMALL_PADDING | H2645_FLAG_USE_REF);
        if (err < 0) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Failed to split AVCC PPS array.\n");
            return err;
        }
        err = ff_cbs_h2645_fragment_add_nals(ctx, frag, &h2645->read_packet);
        if (err < 0)
            return err;

        if (bytestream2_get_bytes_left(&gbc) > 0) {
            av_log(ctx->log_ctx, AV_LOG_WARNING, "%u bytes left at end of AVCC "
                   "header.\n", bytestream2_get_bytes_left(&gbc));
        }
    } else {
        int flags = (H2645_FLAG_IS_NALFF * !!h2645->mp4) | H2645_FLAG_SMALL_PADDING | H2645_FLAG_USE_REF;
        // Annex B, or later MP4 with already-known parameters.

        err = ff_h2645_packet_split(&h2645->read_packet,
                                    frag->data, frag->data_size,
                                    ctx->log_ctx,
                                    h2645->nal_length_size,
                                    codec_id, flags);
        if (err < 0)
            return err;

        err = ff_cbs_h2645_fragment_add_nals(ctx, frag, &h2645->read_packet);
        if (err < 0)
            return err;
    }

    return 0;
}

#define cbs_h2645_replace_ps(ps_name, ps_var, id_element) \
static int cbs_h264_replace_ ## ps_var(CodedBitstreamContext *ctx, \
                                                  CodedBitstreamUnit *unit)  \
{ \
    CodedBitstreamH264Context *priv = ctx->priv_data; \
    H264Raw ## ps_name *ps_var = unit->content; \
    unsigned int id = ps_var->id_element; \
    int err = ff_cbs_make_unit_refcounted(ctx, unit); \
    if (err < 0) \
        return err; \
    if (priv->ps_var[id] == priv->active_ ## ps_var) \
        priv->active_ ## ps_var = NULL ; \
    av_assert0(unit->content_ref); \
    av_refstruct_replace(&priv->ps_var[id], unit->content_ref); \
    return 0; \
}

cbs_h2645_replace_ps(SPS, sps, seq_parameter_set_id)
cbs_h2645_replace_ps(PPS, pps, pic_parameter_set_id)

static int cbs_h264_read_nal_unit(CodedBitstreamContext *ctx,
                                  CodedBitstreamUnit *unit)
{
    GetBitContext gbc;
    int err;

    err = init_get_bits(&gbc, unit->data, 8 * unit->data_size);
    if (err < 0)
        return err;

    err = ff_cbs_alloc_unit_content(ctx, unit);
    if (err < 0)
        return err;

    switch (unit->type) {
    case H264_NAL_SPS:
        {
            H264RawSPS *sps = unit->content;

            err = cbs_h264_read_sps(ctx, &gbc, sps);
            if (err < 0)
                return err;

            err = cbs_h264_replace_sps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_SPS_EXT:
        {
            err = cbs_h264_read_sps_extension(ctx, &gbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_PPS:
        {
            H264RawPPS *pps = unit->content;

            err = cbs_h264_read_pps(ctx, &gbc, pps);
            if (err < 0)
                return err;

            err = cbs_h264_replace_pps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_SLICE:
    case H264_NAL_IDR_SLICE:
    case H264_NAL_AUXILIARY_SLICE:
        {
            H264RawSlice *slice = unit->content;
            int pos, len;

            err = cbs_h264_read_slice_header(ctx, &gbc, &slice->header);
            if (err < 0)
                return err;

            if (!ff_cbs_h2645_read_more_rbsp_data(&gbc))
                return AVERROR_INVALIDDATA;

            pos = get_bits_count(&gbc);
            len = unit->data_size;

            slice->data_size = len - pos / 8;
            slice->data_ref  = av_buffer_ref(unit->data_ref);
            if (!slice->data_ref)
                return AVERROR(ENOMEM);
            slice->data = unit->data + pos / 8;
            slice->data_bit_start = pos % 8;
        }
        break;

    case H264_NAL_AUD:
        {
            err = cbs_h264_read_aud(ctx, &gbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_SEI:
        {
            err = cbs_h264_read_sei(ctx, &gbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_FILLER_DATA:
        {
            err = cbs_h264_read_filler(ctx, &gbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_END_SEQUENCE:
    case H264_NAL_END_STREAM:
        {
            err = (unit->type == H264_NAL_END_SEQUENCE ?
                   cbs_h264_read_end_of_sequence :
                   cbs_h264_read_end_of_stream)(ctx, &gbc, unit->content);
            if (err < 0)
                return err;
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

            err = cbs_h264_replace_sps(ctx, unit);
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

            err = cbs_h264_replace_pps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_SLICE:
    case H264_NAL_IDR_SLICE:
    case H264_NAL_AUXILIARY_SLICE:
        {
            H264RawSlice *slice = unit->content;

            err = cbs_h264_write_slice_header(ctx, pbc, &slice->header);
            if (err < 0)
                return err;

            if (slice->data) {
                err = ff_cbs_h2645_write_slice_data(ctx, pbc, slice->data,
                                                 slice->data_size,
                                                 slice->data_bit_start);
                if (err < 0)
                    return err;
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

    case H264_NAL_FILLER_DATA:
        {
            err = cbs_h264_write_filler(ctx, pbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_END_SEQUENCE:
        {
            err = cbs_h264_write_end_of_sequence(ctx, pbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_END_STREAM:
        {
            err = cbs_h264_write_end_of_stream(ctx, pbc, unit->content);
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

static int cbs_h264_discarded_nal_unit(CodedBitstreamContext *ctx,
                                       const CodedBitstreamUnit *unit,
                                       enum AVDiscard skip)
{
    H264RawNALUnitHeader *header;
    H264RawSliceHeader *slice;
    int slice_type_i, slice_type_b, slice_type_si;

    if (skip <= AVDISCARD_DEFAULT)
        return 0;

    // keep non-VCL
    if (unit->type != H264_NAL_SLICE &&
        unit->type != H264_NAL_IDR_SLICE &&
        unit->type != H264_NAL_AUXILIARY_SLICE)
        return 0;

    if (skip >= AVDISCARD_ALL)
        return 1;

    if (skip >= AVDISCARD_NONKEY && unit->type != H264_NAL_IDR_SLICE)
        return 1;

    header = (H264RawNALUnitHeader *)unit->content;
    if (!header) {
        av_log(ctx->log_ctx, AV_LOG_WARNING,
                "h264 nal unit header is null, missing decompose?\n");
        return 0;
    }

    if (skip >= AVDISCARD_NONREF && !header->nal_ref_idc)
        return 1;

    slice = (H264RawSliceHeader *)unit->content;
    if (!slice) {
        av_log(ctx->log_ctx, AV_LOG_WARNING,
                "h264 slice header is null, missing decompose?\n");
        return 0;
    }

    slice_type_i  = slice->slice_type % 5 == 2;
    slice_type_b  = slice->slice_type % 5 == 1;
    slice_type_si = slice->slice_type % 5 == 4;

    if (skip >= AVDISCARD_BIDIR && slice_type_b)
        return 1;
    if (skip >= AVDISCARD_NONINTRA && !slice_type_i && !slice_type_si)
        return 1;

    return 0;
}

static av_cold void cbs_h264_flush(CodedBitstreamContext *ctx)
{
    CodedBitstreamH264Context *h264 = ctx->priv_data;

    for (int i = 0; i < FF_ARRAY_ELEMS(h264->sps); i++)
        av_refstruct_unref(&h264->sps[i]);
    for (int i = 0; i < FF_ARRAY_ELEMS(h264->pps); i++)
        av_refstruct_unref(&h264->pps[i]);

    h264->active_sps = NULL;
    h264->active_pps = NULL;
    h264->last_slice_nal_unit_type = 0;
}

static av_cold void cbs_h264_close(CodedBitstreamContext *ctx)
{
    CodedBitstreamH264Context *h264 = ctx->priv_data;
    int i;

    ff_h2645_packet_uninit(&h264->common.read_packet);

    for (i = 0; i < FF_ARRAY_ELEMS(h264->sps); i++)
        av_refstruct_unref(&h264->sps[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(h264->pps); i++)
        av_refstruct_unref(&h264->pps[i]);
}

static void cbs_h264_free_sei(AVRefStructOpaque unused, void *content)
{
    H264RawSEI *sei = content;
    ff_cbs_sei_free_message_list(&sei->message_list);
}

static CodedBitstreamUnitTypeDescriptor cbs_h264_unit_types[] = {
    CBS_UNIT_TYPE_POD(H264_NAL_SPS,     H264RawSPS),
    CBS_UNIT_TYPE_POD(H264_NAL_SPS_EXT, H264RawSPSExtension),

    CBS_UNIT_TYPE_INTERNAL_REF(H264_NAL_PPS, H264RawPPS, slice_group_id),

    CBS_UNIT_TYPES_INTERNAL_REF((H264_NAL_IDR_SLICE,
                                 H264_NAL_SLICE,
                                 H264_NAL_AUXILIARY_SLICE), H264RawSlice, data),

    CBS_UNIT_TYPE_POD(H264_NAL_AUD,          H264RawAUD),
    CBS_UNIT_TYPE_POD(H264_NAL_FILLER_DATA,  H264RawFiller),
    CBS_UNIT_TYPE_POD(H264_NAL_END_SEQUENCE, H264RawNALUnitHeader),
    CBS_UNIT_TYPE_POD(H264_NAL_END_STREAM,   H264RawNALUnitHeader),

    CBS_UNIT_TYPE_COMPLEX(H264_NAL_SEI, H264RawSEI, &cbs_h264_free_sei),

    CBS_UNIT_TYPE_END_OF_LIST
};

const CodedBitstreamType ff_cbs_type_h264 = {
    .codec_id          = AV_CODEC_ID_H264,

    .priv_data_size    = sizeof(CodedBitstreamH264Context),

    .unit_types        = cbs_h264_unit_types,

    .split_fragment    = &cbs_h264_split_fragment,
    .read_unit         = &cbs_h264_read_nal_unit,
    .write_unit        = &cbs_h264_write_nal_unit,
    .discarded_unit    = &cbs_h264_discarded_nal_unit,
    .assemble_fragment = &ff_cbs_h2645_assemble_fragment,

    .flush             = &cbs_h264_flush,
    .close             = &cbs_h264_close,
};

// Macro for the read/write pair.
#define SEI_MESSAGE_RW(codec, name) \
    .read  = cbs_ ## codec ## _read_  ## name ## _internal, \
    .write = cbs_ ## codec ## _write_ ## name ## _internal

const SEIMessageTypeDescriptor ff_cbs_sei_h264_types[] = {
    {
        SEI_TYPE_BUFFERING_PERIOD,
        1, 0,
        sizeof(H264RawSEIBufferingPeriod),
        SEI_MESSAGE_RW(h264, sei_buffering_period),
    },
    {
        SEI_TYPE_PIC_TIMING,
        1, 0,
        sizeof(H264RawSEIPicTiming),
        SEI_MESSAGE_RW(h264, sei_pic_timing),
    },
    {
        SEI_TYPE_PAN_SCAN_RECT,
        1, 0,
        sizeof(H264RawSEIPanScanRect),
        SEI_MESSAGE_RW(h264, sei_pan_scan_rect),
    },
    {
        SEI_TYPE_RECOVERY_POINT,
        1, 0,
        sizeof(H264RawSEIRecoveryPoint),
        SEI_MESSAGE_RW(h264, sei_recovery_point),
    },
    {
        SEI_TYPE_FILM_GRAIN_CHARACTERISTICS,
        1, 0,
        sizeof(H264RawFilmGrainCharacteristics),
        SEI_MESSAGE_RW(h264, film_grain_characteristics),
    },
    {
        SEI_TYPE_FRAME_PACKING_ARRANGEMENT,
        1, 0,
        sizeof(H264RawSEIFramePackingArrangement),
        SEI_MESSAGE_RW(h264, sei_frame_packing_arrangement),
    },
    {
        SEI_TYPE_DISPLAY_ORIENTATION,
        1, 0,
        sizeof(H264RawSEIDisplayOrientation),
        SEI_MESSAGE_RW(h264, sei_display_orientation),
    },
    SEI_MESSAGE_TYPE_END
};
