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

#include "libavutil/intmath.h"
#include "libavutil/mem.h"
#include "libavutil/refstruct.h"
#include "bytestream.h"
#include "cbs.h"
#include "cbs_internal.h"
#include "cbs_h2645.h"
#include "cbs_h266.h"
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
#define FUNC_H266(name) FUNC_NAME1(READWRITE, h266, name)
#define FUNC_NAME2_EXPORT(rw, codec, name) ff_cbs_ ## codec ## _ ## rw ## _ ## name
#define FUNC_NAME1_EXPORT(rw, codec, name) FUNC_NAME2_EXPORT(rw, codec, name)
#define FUNC_SEI(name)  FUNC_NAME1_EXPORT(READWRITE, sei,  name)

#define SEI_FUNC(name, args) \
static int FUNC_H266(name) args;  \
static int FUNC_H266(name ## _internal)(CodedBitstreamContext *ctx, \
                                   RWContext *rw, void *cur,   \
                                   SEIMessageState *state)     \
{ \
    return FUNC_H266(name)(ctx, rw, cur, state); \
} \
static int FUNC_H266(name) args

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

#define FUNC(name) FUNC_H266(name)
#include "cbs_h266_syntax_template.c"
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

#define FUNC(name) FUNC_H266(name)
#include "cbs_h266_syntax_template.c"
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



static int cbs_h266_split_fragment(CodedBitstreamContext *ctx,
                                   CodedBitstreamFragment *frag,
                                   int header)
{
    enum AVCodecID codec_id = ctx->codec->codec_id;
    CodedBitstreamH266Context *priv = ctx->priv_data;
    CodedBitstreamH2645Context *h2645 = &priv->common;
    GetByteContext gbc;
    int err;

    av_assert0(frag->data && frag->nb_units == 0);
    if (frag->data_size == 0)
        return 0;

    if(header && frag->data[0]) {
        // VVCC header.
        int ptl_present_flag, num_arrays;
        int b, i, j;

        h2645->mp4 = 1;

        bytestream2_init(&gbc, frag->data, frag->data_size);

        b = bytestream2_get_byte(&gbc);
        h2645->nal_length_size = ((b >> 1) & 3) + 1;
        ptl_present_flag = b & 1;

        if(ptl_present_flag) {
            int num_sublayers, num_bytes_constraint_info, num_sub_profiles;
            num_sublayers = (bytestream2_get_be16u(&gbc) >> 4) & 7;
            bytestream2_skip(&gbc, 1);

            // begin VvcPTLRecord(num_sublayers);
            num_bytes_constraint_info = bytestream2_get_byte(&gbc) & 0x3f;
            bytestream2_skip(&gbc, 2 + num_bytes_constraint_info);
            if(num_sublayers > 1) {
                int count_present_flags = 0;
                b = bytestream2_get_byte(&gbc);
                for(i = num_sublayers - 2; i >= 0; i--) {
                    if((b >> (7 - (num_sublayers - 2 - i))) & 0x01)
                        count_present_flags++;
                }
                bytestream2_skip(&gbc, count_present_flags);
            }
            num_sub_profiles = bytestream2_get_byte(&gbc);
            bytestream2_skip(&gbc, num_sub_profiles * 4);
            // end VvcPTLRecord(num_sublayers);

            bytestream2_skip(&gbc, 3 * 2);
        }

        num_arrays = bytestream2_get_byte(&gbc);
        for(j = 0; j < num_arrays; j++) {
            size_t start, end, size;
            int nal_unit_type = bytestream2_get_byte(&gbc) & 0x1f;
            unsigned int num_nalus = 1;
            if(nal_unit_type != VVC_DCI_NUT && nal_unit_type != VVC_OPI_NUT)
                num_nalus = bytestream2_get_be16(&gbc);

            start = bytestream2_tell(&gbc);
            for(i = 0; i < num_nalus; i++) {
                if (bytestream2_get_bytes_left(&gbc) < 2)
                    return AVERROR_INVALIDDATA;
                size = bytestream2_get_be16(&gbc);
                if (bytestream2_get_bytes_left(&gbc) < size)
                    return AVERROR_INVALIDDATA;
                bytestream2_skip(&gbc, size);
            }
            end = bytestream2_tell(&gbc);

            err = ff_h2645_packet_split(&h2645->read_packet,
                                        frag->data + start, end - start,
                                        ctx->log_ctx, 2, AV_CODEC_ID_VVC,
                                        H2645_FLAG_IS_NALFF | H2645_FLAG_SMALL_PADDING | H2645_FLAG_USE_REF);
            if (err < 0) {
                av_log(ctx->log_ctx, AV_LOG_ERROR, "Failed to split "
                       "VVCC array %d (%d NAL units of type %d).\n",
                       i, num_nalus, nal_unit_type);
                return err;
            }
            err = ff_cbs_h2645_fragment_add_nals(ctx, frag, &h2645->read_packet);
            if (err < 0)
                return err;
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

#define cbs_h266_replace_ps(ps_name, ps_var, id_element) \
static int cbs_h266_replace_ ## ps_var(CodedBitstreamContext *ctx, \
                                       CodedBitstreamUnit *unit)  \
{ \
    CodedBitstreamH266Context *priv = ctx->priv_data; \
    H266Raw ## ps_name *ps_var = unit->content; \
    unsigned int id = ps_var->id_element; \
    int err = ff_cbs_make_unit_refcounted(ctx, unit); \
    if (err < 0) \
        return err; \
    av_assert0(unit->content_ref); \
    av_refstruct_replace(&priv->ps_var[id], unit->content_ref); \
    return 0; \
}

cbs_h266_replace_ps(VPS, vps, vps_video_parameter_set_id)
cbs_h266_replace_ps(PPS, pps, pps_pic_parameter_set_id)

static int cbs_h266_replace_sps(CodedBitstreamContext *ctx,
                                CodedBitstreamUnit *unit)
{
    CodedBitstreamH266Context *priv = ctx->priv_data;
    H266RawSPS *sps = unit->content;
    unsigned int id = sps->sps_seq_parameter_set_id;
    int err = ff_cbs_make_unit_refcounted(ctx, unit);
    if (err < 0)
        return err;
    av_assert0(unit->content_ref);
    if (priv->sps[id] && memcmp(priv->sps[id], unit->content_ref, sizeof(*priv->sps[id]))) {
        for (unsigned int i = 0; i < VVC_MAX_PPS_COUNT; i++) {
            if (priv->pps[i] && priv->pps[i]->pps_seq_parameter_set_id == id)
                av_refstruct_unref(&priv->pps[i]);
        }
    }
    av_refstruct_replace(&priv->sps[id], unit->content_ref);
    return 0;
}

static int cbs_h266_replace_ph(CodedBitstreamContext *ctx,
                               CodedBitstreamUnit *unit,
                               H266RawPictureHeader *ph)
{
    CodedBitstreamH266Context *h266 = ctx->priv_data;
    int err;

    err = ff_cbs_make_unit_refcounted(ctx, unit);
    if (err < 0)
        return err;
    av_assert0(unit->content_ref);
    av_refstruct_replace(&h266->ph_ref, unit->content_ref);
    h266->ph = ph;
    return 0;
}

static int cbs_h266_read_nal_unit(CodedBitstreamContext *ctx,
                                  CodedBitstreamUnit *unit)
{
    GetBitContext gbc;
    int err;
    CodedBitstreamH266Context *h266 = ctx->priv_data;

    err = init_get_bits8(&gbc, unit->data, unit->data_size);
    if (err < 0)
        return err;

    err = ff_cbs_alloc_unit_content(ctx, unit);
    if (err < 0)
        return err;

    switch (unit->type) {
    case VVC_DCI_NUT:
        {
            err = cbs_h266_read_dci(ctx, &gbc, unit->content);

            if (err < 0)
                return err;
        }
        break;
    case VVC_OPI_NUT:
        {
            err = cbs_h266_read_opi(ctx, &gbc, unit->content);

            if (err < 0)
                return err;
        }
        break;
    case VVC_VPS_NUT:
        {
            H266RawVPS *vps = unit->content;

            err = cbs_h266_read_vps(ctx, &gbc, vps);
            if (err < 0)
                return err;

            err = cbs_h266_replace_vps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;
    case VVC_SPS_NUT:
        {
            H266RawSPS *sps = unit->content;

            err = cbs_h266_read_sps(ctx, &gbc, sps);
            if (err < 0)
                return err;

            err = cbs_h266_replace_sps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case VVC_PPS_NUT:
        {
            H266RawPPS *pps = unit->content;

            err = cbs_h266_read_pps(ctx, &gbc, pps);
            if (err < 0)
                return err;

            err = cbs_h266_replace_pps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case VVC_PREFIX_APS_NUT:
    case VVC_SUFFIX_APS_NUT:
        {
            err = cbs_h266_read_aps(ctx, &gbc, unit->content,
                                    unit->type == VVC_PREFIX_APS_NUT);

            if (err < 0)
                return err;
        }
        break;
    case VVC_PH_NUT:
        {
            H266RawPH *ph = unit->content;
            err = cbs_h266_read_ph(ctx, &gbc, ph);
            if (err < 0)
                return err;
            err = cbs_h266_replace_ph(ctx, unit, &ph->ph_picture_header);
            if (err < 0)
                return err;
        }
        break;

    case VVC_TRAIL_NUT:
    case VVC_STSA_NUT:
    case VVC_RADL_NUT:
    case VVC_RASL_NUT:
    case VVC_IDR_W_RADL:
    case VVC_IDR_N_LP:
    case VVC_CRA_NUT:
    case VVC_GDR_NUT:
        {
            H266RawSlice *slice = unit->content;
            int pos, len;

            err = cbs_h266_read_slice_header(ctx, &gbc, &slice->header);
            if (err < 0)
                return err;

            if (!ff_cbs_h2645_read_more_rbsp_data(&gbc))
                return AVERROR_INVALIDDATA;

            pos = get_bits_count(&gbc);
            len = unit->data_size;

            if (slice->header.sh_picture_header_in_slice_header_flag) {
                err = cbs_h266_replace_ph(ctx, unit, &slice->header.sh_picture_header);
                if (err < 0)
                    return err;
                slice->ph_ref = NULL;
            } else {
                slice->ph_ref = av_refstruct_ref(h266->ph_ref);
            }
            slice->ph     = h266->ph;
            slice->pps    = av_refstruct_ref(h266->pps[slice->ph->ph_pic_parameter_set_id]);
            slice->sps    = av_refstruct_ref(h266->sps[slice->pps->pps_seq_parameter_set_id]);

            slice->header_size = pos / 8;
            slice->data_size = len - pos / 8;
            slice->data_ref  = av_buffer_ref(unit->data_ref);
            if (!slice->data_ref)
                return AVERROR(ENOMEM);
            slice->data = unit->data + pos / 8;
            slice->data_bit_start = pos % 8;
        }
        break;

    case VVC_AUD_NUT:
        {
            err = cbs_h266_read_aud(ctx, &gbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case VVC_PREFIX_SEI_NUT:
    case VVC_SUFFIX_SEI_NUT:
        {
            err = cbs_h266_read_sei(ctx, &gbc, unit->content,
                                    unit->type == VVC_PREFIX_SEI_NUT);

            if (err < 0)
                return err;
        }
        break;

    default:
        return AVERROR(ENOSYS);
    }
    return 0;
}

static int cbs_h266_write_nal_unit(CodedBitstreamContext *ctx,
                                   CodedBitstreamUnit *unit,
                                   PutBitContext *pbc)
{
    int err;

    switch (unit->type) {
    case VVC_DCI_NUT:
        {
            H266RawDCI *dci = unit->content;

            err = cbs_h266_write_dci(ctx, pbc, dci);
            if (err < 0)
                return err;
        }
        break;
    case VVC_OPI_NUT:
        {
            H266RawOPI *opi = unit->content;

            err = cbs_h266_write_opi(ctx, pbc, opi);
            if (err < 0)
                return err;
        }
        break;
    case VVC_VPS_NUT:
        {
            H266RawVPS *vps = unit->content;

            err = cbs_h266_write_vps(ctx, pbc, vps);
            if (err < 0)
                return err;

            err = cbs_h266_replace_vps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;
    case VVC_SPS_NUT:
        {
            H266RawSPS *sps = unit->content;

            err = cbs_h266_write_sps(ctx, pbc, sps);
            if (err < 0)
                return err;

            err = cbs_h266_replace_sps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case VVC_PPS_NUT:
        {
            H266RawPPS *pps = unit->content;

            err = cbs_h266_write_pps(ctx, pbc, pps);
            if (err < 0)
                return err;

            err = cbs_h266_replace_pps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case VVC_PREFIX_APS_NUT:
    case VVC_SUFFIX_APS_NUT:
        {
            err = cbs_h266_write_aps(ctx, pbc, unit->content,
                                     unit->type == VVC_PREFIX_APS_NUT);
            if (err < 0)
                return err;
        }
        break;
    case VVC_PH_NUT:
        {
            H266RawPH *ph = unit->content;
            err = cbs_h266_write_ph(ctx, pbc, ph);
            if (err < 0)
                return err;

            err = cbs_h266_replace_ph(ctx, unit, &ph->ph_picture_header);
            if (err < 0)
                return err;
        }
        break;

    case VVC_TRAIL_NUT:
    case VVC_STSA_NUT:
    case VVC_RADL_NUT:
    case VVC_RASL_NUT:
    case VVC_IDR_W_RADL:
    case VVC_IDR_N_LP:
    case VVC_CRA_NUT:
    case VVC_GDR_NUT:
        {
            H266RawSlice *slice = unit->content;

            err = cbs_h266_write_slice_header(ctx, pbc, &slice->header);
            if (err < 0)
                return err;

            if (slice->header.sh_picture_header_in_slice_header_flag) {
                err = cbs_h266_replace_ph(ctx, unit, &slice->header.sh_picture_header);
                if (err < 0)
                    return err;
            }

            if (slice->data) {
                err = ff_cbs_h2645_write_slice_data(ctx, pbc, slice->data,
                                                 slice->data_size,
                                                 slice->data_bit_start);
                if (err < 0)
                    return err;
            } else {
                // No slice data - that was just the header.
            }
        }
        break;

    case VVC_AUD_NUT:
        {
            err = cbs_h266_write_aud(ctx, pbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case VVC_PREFIX_SEI_NUT:
    case VVC_SUFFIX_SEI_NUT:
        {
            err = cbs_h266_write_sei(ctx, pbc, unit->content,
                                     unit->type == VVC_PREFIX_SEI_NUT);

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

static av_cold void cbs_h266_flush(CodedBitstreamContext *ctx)
{
    CodedBitstreamH266Context *h266 = ctx->priv_data;

    for (int i = 0; i < FF_ARRAY_ELEMS(h266->vps); i++)
        av_refstruct_unref(&h266->vps[i]);
    for (int i = 0; i < FF_ARRAY_ELEMS(h266->sps); i++)
        av_refstruct_unref(&h266->sps[i]);
    for (int i = 0; i < FF_ARRAY_ELEMS(h266->pps); i++)
        av_refstruct_unref(&h266->pps[i]);
    av_refstruct_unref(&h266->ph_ref);
}

static av_cold void cbs_h266_close(CodedBitstreamContext *ctx)
{
    CodedBitstreamH266Context *h266 = ctx->priv_data;

    cbs_h266_flush(ctx);
    ff_h2645_packet_uninit(&h266->common.read_packet);
}

static void cbs_h266_free_slice(AVRefStructOpaque unused, void *content)
{
    H266RawSlice *slice = content;
    av_buffer_unref(&slice->data_ref);
    av_refstruct_unref(&slice->sps);
    av_refstruct_unref(&slice->pps);
    av_refstruct_unref(&slice->ph_ref);
}


static void cbs_h266_free_sei(AVRefStructOpaque unused, void *content)
{
    H266RawSEI *sei = content;
    ff_cbs_sei_free_message_list(&sei->message_list);
}

static CodedBitstreamUnitTypeDescriptor cbs_h266_unit_types[] = {
    CBS_UNIT_TYPE_INTERNAL_REF(VVC_DCI_NUT, H266RawDCI, extension_data.data),
    CBS_UNIT_TYPE_INTERNAL_REF(VVC_OPI_NUT, H266RawOPI, extension_data.data),
    CBS_UNIT_TYPE_INTERNAL_REF(VVC_VPS_NUT, H266RawVPS, extension_data.data),
    {
        .nb_unit_types     = 1,
        .unit_type.list[0] = VVC_SPS_NUT,
        .content_type      = CBS_CONTENT_TYPE_INTERNAL_REFS,
        .content_size      = sizeof(H266RawSPS),
        .type.ref          = {
            .nb_offsets = 2,
            .offsets    = { offsetof(H266RawSPS, extension_data.data),
                            offsetof(H266RawSPS, vui.extension_data.data) }
        },
    },
    CBS_UNIT_TYPE_INTERNAL_REF(VVC_PPS_NUT, H266RawPPS, extension_data.data),
    CBS_UNIT_TYPE_INTERNAL_REF(VVC_PREFIX_APS_NUT, H266RawAPS, extension_data.data),
    CBS_UNIT_TYPE_INTERNAL_REF(VVC_SUFFIX_APS_NUT, H266RawAPS, extension_data.data),

    CBS_UNIT_TYPE_POD(VVC_PH_NUT , H266RawPH),
    CBS_UNIT_TYPE_POD(VVC_AUD_NUT, H266RawAUD),

    CBS_UNIT_TYPES_COMPLEX((VVC_TRAIL_NUT, VVC_STSA_NUT, VVC_RADL_NUT),
                           H266RawSlice, cbs_h266_free_slice),
    CBS_UNIT_TYPES_COMPLEX((VVC_RASL_NUT, VVC_IDR_W_RADL, VVC_IDR_N_LP),
                           H266RawSlice, cbs_h266_free_slice),
    CBS_UNIT_TYPES_COMPLEX((VVC_CRA_NUT, VVC_GDR_NUT),
                           H266RawSlice, cbs_h266_free_slice),

    CBS_UNIT_TYPES_COMPLEX((VVC_PREFIX_SEI_NUT, VVC_SUFFIX_SEI_NUT),
                           H266RawSEI, cbs_h266_free_sei),

    CBS_UNIT_TYPE_END_OF_LIST
};

const CodedBitstreamType ff_cbs_type_h266 = {
    .codec_id          = AV_CODEC_ID_VVC,

    .priv_data_size    = sizeof(CodedBitstreamH266Context),

    .unit_types        = cbs_h266_unit_types,

    .split_fragment    = &cbs_h266_split_fragment,
    .read_unit         = &cbs_h266_read_nal_unit,
    .write_unit        = &cbs_h266_write_nal_unit,
    .assemble_fragment = &ff_cbs_h2645_assemble_fragment,

    .flush             = &cbs_h266_flush,
    .close             = &cbs_h266_close,
};
