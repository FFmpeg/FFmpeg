/*
 * H.26L/H.264/AVC/JVT/14496-10/... parser
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * H.264 / AVC / MPEG4 part10 parser.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "libavutil/attributes.h"
#include "parser.h"
#include "h264data.h"
#include "golomb.h"
#include "internal.h"
#include "mpegutils.h"

#include <assert.h>

typedef struct H264ParseContext {
    H264Context h;
    ParseContext pc;
    int got_first;
} H264ParseContext;


static int h264_find_frame_end(H264ParseContext *p, const uint8_t *buf,
                               int buf_size)
{
    H264Context *h = &p->h;
    int i;
    uint32_t state;
    ParseContext *pc = &p->pc;
//    mb_addr= pc->mb_addr - 1;
    state = pc->state;
    if (state > 13)
        state = 7;

    for (i = 0; i < buf_size; i++) {
        if (state == 7) {
            i += h->h264dsp.startcode_find_candidate(buf + i, buf_size - i);
            if (i < buf_size)
                state = 2;
        } else if (state <= 2) {
            if (buf[i] == 1)
                state ^= 5;            // 2->7, 1->4, 0->5
            else if (buf[i])
                state = 7;
            else
                state >>= 1;           // 2->1, 1->0, 0->0
        } else if (state <= 5) {
            int nalu_type = buf[i] & 0x1F;
            if (nalu_type == NAL_SEI || nalu_type == NAL_SPS ||
                nalu_type == NAL_PPS || nalu_type == NAL_AUD) {
                if (pc->frame_start_found) {
                    i++;
                    goto found;
                }
            } else if (nalu_type == NAL_SLICE || nalu_type == NAL_DPA ||
                       nalu_type == NAL_IDR_SLICE) {
                if (pc->frame_start_found) {
                    state += 8;
                    continue;
                } else
                    pc->frame_start_found = 1;
            }
            state = 7;
        } else {
            // first_mb_in_slice is 0, probably the first nal of a new slice
            if (buf[i] & 0x80)
                goto found;
            state = 7;
        }
    }
    pc->state = state;
    return END_NOT_FOUND;

found:
    pc->state             = 7;
    pc->frame_start_found = 0;
    return i - (state & 5);
}

static int scan_mmco_reset(AVCodecParserContext *s)
{
    H264ParseContext *p = s->priv_data;
    H264Context      *h = &p->h;
    H264SliceContext *sl = &h->slice_ctx[0];

    sl->slice_type_nos = s->pict_type & 3;

    if (h->pps.redundant_pic_cnt_present)
        get_ue_golomb(&sl->gb); // redundant_pic_count

    if (ff_set_ref_count(h, sl) < 0)
        return AVERROR_INVALIDDATA;

    if (sl->slice_type_nos != AV_PICTURE_TYPE_I) {
        int list;
        for (list = 0; list < sl->list_count; list++) {
            if (get_bits1(&sl->gb)) {
                int index;
                for (index = 0; ; index++) {
                    unsigned int reordering_of_pic_nums_idc = get_ue_golomb_31(&sl->gb);

                    if (reordering_of_pic_nums_idc < 3)
                        get_ue_golomb(&sl->gb);
                    else if (reordering_of_pic_nums_idc > 3) {
                        av_log(h->avctx, AV_LOG_ERROR,
                               "illegal reordering_of_pic_nums_idc %d\n",
                               reordering_of_pic_nums_idc);
                        return AVERROR_INVALIDDATA;
                    } else
                        break;

                    if (index >= sl->ref_count[list]) {
                        av_log(h->avctx, AV_LOG_ERROR,
                               "reference count %d overflow\n", index);
                        return AVERROR_INVALIDDATA;
                    }
                }
            }
        }
    }

    if ((h->pps.weighted_pred && sl->slice_type_nos == AV_PICTURE_TYPE_P) ||
        (h->pps.weighted_bipred_idc == 1 && sl->slice_type_nos == AV_PICTURE_TYPE_B))
        ff_pred_weight_table(h, sl);

    if (get_bits1(&sl->gb)) { // adaptive_ref_pic_marking_mode_flag
        int i;
        for (i = 0; i < MAX_MMCO_COUNT; i++) {
            MMCOOpcode opcode = get_ue_golomb_31(&sl->gb);
            if (opcode > (unsigned) MMCO_LONG) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "illegal memory management control operation %d\n",
                       opcode);
                return AVERROR_INVALIDDATA;
            }
            if (opcode == MMCO_END)
               return 0;
            else if (opcode == MMCO_RESET)
                return 1;

            if (opcode == MMCO_SHORT2UNUSED || opcode == MMCO_SHORT2LONG)
                get_ue_golomb(&sl->gb);
            if (opcode == MMCO_SHORT2LONG || opcode == MMCO_LONG2UNUSED ||
                opcode == MMCO_LONG || opcode == MMCO_SET_MAX_LONG)
                get_ue_golomb_31(&sl->gb);
        }
    }

    return 0;
}

/**
 * Parse NAL units of found picture and decode some basic information.
 *
 * @param s parser context.
 * @param avctx codec context.
 * @param buf buffer with field/frame data.
 * @param buf_size size of the buffer.
 */
static inline int parse_nal_units(AVCodecParserContext *s,
                                  AVCodecContext *avctx,
                                  const uint8_t *buf, int buf_size)
{
    H264ParseContext *p = s->priv_data;
    H264Context      *h = &p->h;
    H264SliceContext *sl = &h->slice_ctx[0];
    const uint8_t *buf_end = buf + buf_size;
    unsigned int pps_id;
    unsigned int slice_type;
    int state = -1, got_reset = 0;
    const uint8_t *ptr;
    int field_poc[2];

    /* set some sane default values */
    s->pict_type         = AV_PICTURE_TYPE_I;
    s->key_frame         = 0;
    s->picture_structure = AV_PICTURE_STRUCTURE_UNKNOWN;

    h->avctx = avctx;
    ff_h264_reset_sei(h);

    if (!buf_size)
        return 0;

    for (;;) {
        int src_length, dst_length, consumed;
        buf = avpriv_find_start_code(buf, buf_end, &state);
        if (buf >= buf_end)
            break;
        --buf;
        src_length = buf_end - buf;
        switch (state & 0x1f) {
        case NAL_SLICE:
        case NAL_IDR_SLICE:
            // Do not walk the whole buffer just to decode slice header
            if ((state & 0x1f) == NAL_IDR_SLICE || ((state >> 5) & 0x3) == 0) {
                /* IDR or disposable slice
                 * No need to decode many bytes because MMCOs shall not be present. */
                if (src_length > 60)
                    src_length = 60;
            } else {
                /* To decode up to MMCOs */
                if (src_length > 1000)
                    src_length = 1000;
            }
            break;
        }
        ptr = ff_h264_decode_nal(h, sl, buf, &dst_length, &consumed, src_length);
        if (!ptr || dst_length < 0)
            break;

        init_get_bits(&h->gb, ptr, 8 * dst_length);
        switch (h->nal_unit_type) {
        case NAL_SPS:
            ff_h264_decode_seq_parameter_set(h);
            break;
        case NAL_PPS:
            ff_h264_decode_picture_parameter_set(h, h->gb.size_in_bits);
            break;
        case NAL_SEI:
            ff_h264_decode_sei(h);
            break;
        case NAL_IDR_SLICE:
            s->key_frame = 1;

            h->prev_frame_num        = 0;
            h->prev_frame_num_offset = 0;
            h->prev_poc_msb          =
            h->prev_poc_lsb          = 0;
        /* fall through */
        case NAL_SLICE:
            init_get_bits(&sl->gb, ptr, 8 * dst_length);
            get_ue_golomb(&sl->gb);  // skip first_mb_in_slice
            slice_type   = get_ue_golomb_31(&sl->gb);
            s->pict_type = golomb_to_pict_type[slice_type % 5];
            if (h->sei_recovery_frame_cnt >= 0) {
                /* key frame, since recovery_frame_cnt is set */
                s->key_frame = 1;
            }
            pps_id = get_ue_golomb(&sl->gb);
            if (pps_id >= MAX_PPS_COUNT) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "pps_id %u out of range\n", pps_id);
                return -1;
            }
            if (!h->pps_buffers[pps_id]) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "non-existing PPS %u referenced\n", pps_id);
                return -1;
            }
            h->pps = *h->pps_buffers[pps_id];
            if (!h->sps_buffers[h->pps.sps_id]) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "non-existing SPS %u referenced\n", h->pps.sps_id);
                return -1;
            }
            h->sps       = *h->sps_buffers[h->pps.sps_id];
            h->frame_num = get_bits(&sl->gb, h->sps.log2_max_frame_num);

            s->coded_width  = 16 * h->sps.mb_width;
            s->coded_height = 16 * h->sps.mb_height;
            s->width        = s->coded_width  - (h->sps.crop_right + h->sps.crop_left);
            s->height       = s->coded_height - (h->sps.crop_top   + h->sps.crop_bottom);
            if (s->width <= 0 || s->height <= 0) {
                s->width  = s->coded_width;
                s->height = s->coded_height;
            }

            switch (h->sps.bit_depth_luma) {
            case 9:
                if (CHROMA444(h))      s->format = AV_PIX_FMT_YUV444P9;
                else if (CHROMA422(h)) s->format = AV_PIX_FMT_YUV422P9;
                else                   s->format = AV_PIX_FMT_YUV420P9;
                break;
            case 10:
                if (CHROMA444(h))      s->format = AV_PIX_FMT_YUV444P10;
                else if (CHROMA422(h)) s->format = AV_PIX_FMT_YUV422P10;
                else                   s->format = AV_PIX_FMT_YUV420P10;
                break;
            case 8:
                if (CHROMA444(h))      s->format = AV_PIX_FMT_YUV444P;
                else if (CHROMA422(h)) s->format = AV_PIX_FMT_YUV422P;
                else                   s->format = AV_PIX_FMT_YUV420P;
                break;
            default:
                s->format = AV_PIX_FMT_NONE;
            }

            avctx->profile = ff_h264_get_profile(&h->sps);
            avctx->level   = h->sps.level_idc;

            if (h->sps.frame_mbs_only_flag) {
                h->picture_structure = PICT_FRAME;
            } else {
                if (get_bits1(&sl->gb)) { // field_pic_flag
                    h->picture_structure = PICT_TOP_FIELD + get_bits1(&sl->gb); // bottom_field_flag
                } else {
                    h->picture_structure = PICT_FRAME;
                }
            }

            if (h->nal_unit_type == NAL_IDR_SLICE)
                get_ue_golomb(&sl->gb); /* idr_pic_id */
            if (h->sps.poc_type == 0) {
                h->poc_lsb = get_bits(&sl->gb, h->sps.log2_max_poc_lsb);

                if (h->pps.pic_order_present == 1 &&
                    h->picture_structure == PICT_FRAME)
                    h->delta_poc_bottom = get_se_golomb(&sl->gb);
            }

            if (h->sps.poc_type == 1 &&
                !h->sps.delta_pic_order_always_zero_flag) {
                h->delta_poc[0] = get_se_golomb(&sl->gb);

                if (h->pps.pic_order_present == 1 &&
                    h->picture_structure == PICT_FRAME)
                    h->delta_poc[1] = get_se_golomb(&sl->gb);
            }

            /* Decode POC of this picture.
             * The prev_ values needed for decoding POC of the next picture are not set here. */
            field_poc[0] = field_poc[1] = INT_MAX;
            ff_init_poc(h, field_poc, &s->output_picture_number);

            /* Continue parsing to check if MMCO_RESET is present.
             * FIXME: MMCO_RESET could appear in non-first slice.
             *        Maybe, we should parse all undisposable non-IDR slice of this
             *        picture until encountering MMCO_RESET in a slice of it. */
            if (h->nal_ref_idc && h->nal_unit_type != NAL_IDR_SLICE) {
                got_reset = scan_mmco_reset(s);
                if (got_reset < 0)
                    return got_reset;
            }

            /* Set up the prev_ values for decoding POC of the next picture. */
            h->prev_frame_num        = got_reset ? 0 : h->frame_num;
            h->prev_frame_num_offset = got_reset ? 0 : h->frame_num_offset;
            if (h->nal_ref_idc != 0) {
                if (!got_reset) {
                    h->prev_poc_msb = h->poc_msb;
                    h->prev_poc_lsb = h->poc_lsb;
                } else {
                    h->prev_poc_msb = 0;
                    h->prev_poc_lsb =
                        h->picture_structure == PICT_BOTTOM_FIELD ? 0 : field_poc[0];
                }
            }

            if (h->sps.pic_struct_present_flag) {
                switch (h->sei_pic_struct) {
                case SEI_PIC_STRUCT_TOP_FIELD:
                case SEI_PIC_STRUCT_BOTTOM_FIELD:
                    s->repeat_pict = 0;
                    break;
                case SEI_PIC_STRUCT_FRAME:
                case SEI_PIC_STRUCT_TOP_BOTTOM:
                case SEI_PIC_STRUCT_BOTTOM_TOP:
                    s->repeat_pict = 1;
                    break;
                case SEI_PIC_STRUCT_TOP_BOTTOM_TOP:
                case SEI_PIC_STRUCT_BOTTOM_TOP_BOTTOM:
                    s->repeat_pict = 2;
                    break;
                case SEI_PIC_STRUCT_FRAME_DOUBLING:
                    s->repeat_pict = 3;
                    break;
                case SEI_PIC_STRUCT_FRAME_TRIPLING:
                    s->repeat_pict = 5;
                    break;
                default:
                    s->repeat_pict = h->picture_structure == PICT_FRAME ? 1 : 0;
                    break;
                }
            } else {
                s->repeat_pict = h->picture_structure == PICT_FRAME ? 1 : 0;
            }

            if (h->picture_structure == PICT_FRAME) {
                s->picture_structure = AV_PICTURE_STRUCTURE_FRAME;
                if (h->sps.pic_struct_present_flag) {
                    switch (h->sei_pic_struct) {
                    case SEI_PIC_STRUCT_TOP_BOTTOM:
                    case SEI_PIC_STRUCT_TOP_BOTTOM_TOP:
                        s->field_order = AV_FIELD_TT;
                        break;
                    case SEI_PIC_STRUCT_BOTTOM_TOP:
                    case SEI_PIC_STRUCT_BOTTOM_TOP_BOTTOM:
                        s->field_order = AV_FIELD_BB;
                        break;
                    default:
                        s->field_order = AV_FIELD_PROGRESSIVE;
                        break;
                    }
                } else {
                    if (field_poc[0] < field_poc[1])
                        s->field_order = AV_FIELD_TT;
                    else if (field_poc[0] > field_poc[1])
                        s->field_order = AV_FIELD_BB;
                    else
                        s->field_order = AV_FIELD_PROGRESSIVE;
                }
            } else {
                if (h->picture_structure == PICT_TOP_FIELD)
                    s->picture_structure = AV_PICTURE_STRUCTURE_TOP_FIELD;
                else
                    s->picture_structure = AV_PICTURE_STRUCTURE_BOTTOM_FIELD;
                s->field_order = AV_FIELD_UNKNOWN;
            }

            return 0; /* no need to evaluate the rest */
        }
        buf += consumed;
    }
    /* didn't find a picture! */
    av_log(h->avctx, AV_LOG_ERROR, "missing picture in access unit\n");
    return -1;
}

static int h264_parse(AVCodecParserContext *s,
                      AVCodecContext *avctx,
                      const uint8_t **poutbuf, int *poutbuf_size,
                      const uint8_t *buf, int buf_size)
{
    H264ParseContext *p = s->priv_data;
    H264Context      *h = &p->h;
    ParseContext *pc = &p->pc;
    int next;

    if (!p->got_first) {
        p->got_first = 1;
        if (avctx->extradata_size) {
            h->avctx = avctx;
            // must be done like in the decoder.
            // otherwise opening the parser, creating extradata,
            // and then closing and opening again
            // will cause has_b_frames to be always set.
            // NB: estimate_timings_from_pts behaves exactly like this.
            if (!avctx->has_b_frames)
                h->low_delay = 1;
            ff_h264_decode_extradata(h);
        }
    }

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
    } else {
        next = h264_find_frame_end(p, buf, buf_size);

        if (ff_combine_frame(pc, next, &buf, &buf_size) < 0) {
            *poutbuf      = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }

        if (next < 0 && next != END_NOT_FOUND) {
            assert(pc->last_index + next >= 0);
            h264_find_frame_end(p, &pc->buffer[pc->last_index + next], -next); // update state
        }
    }

    parse_nal_units(s, avctx, buf, buf_size);

    if (h->sei_cpb_removal_delay >= 0) {
        s->dts_sync_point    = h->sei_buffering_period_present;
        s->dts_ref_dts_delta = h->sei_cpb_removal_delay;
        s->pts_dts_delta     = h->sei_dpb_output_delay;
    } else {
        s->dts_sync_point    = INT_MIN;
        s->dts_ref_dts_delta = INT_MIN;
        s->pts_dts_delta     = INT_MIN;
    }

    if (s->flags & PARSER_FLAG_ONCE) {
        s->flags &= PARSER_FLAG_COMPLETE_FRAMES;
    }

    *poutbuf      = buf;
    *poutbuf_size = buf_size;
    return next;
}

static int h264_split(AVCodecContext *avctx,
                      const uint8_t *buf, int buf_size)
{
    int i;
    uint32_t state = -1;
    int has_sps    = 0;

    for (i = 0; i <= buf_size; i++) {
        if ((state & 0xFFFFFF1F) == 0x107)
            has_sps = 1;
        /*  if((state&0xFFFFFF1F) == 0x101 ||
         *     (state&0xFFFFFF1F) == 0x102 ||
         *     (state&0xFFFFFF1F) == 0x105) {
         *  }
         */
        if ((state & 0xFFFFFF00) == 0x100 && (state & 0xFFFFFF1F) != 0x106 &&
            (state & 0xFFFFFF1F) != 0x107 && (state & 0xFFFFFF1F) != 0x108 &&
            (state & 0xFFFFFF1F) != 0x109 && (state & 0xFFFFFF1F) != 0x10d &&
            (state & 0xFFFFFF1F) != 0x10f) {
            if (has_sps) {
                while (i > 4 && buf[i - 5] == 0)
                    i--;
                return i - 4;
            }
        }
        if (i < buf_size)
            state = (state << 8) | buf[i];
    }
    return 0;
}

static void h264_close(AVCodecParserContext *s)
{
    H264ParseContext *p = s->priv_data;
    H264Context      *h = &p->h;
    ParseContext *pc = &p->pc;

    av_free(pc->buffer);
    ff_h264_free_context(h);
}

static av_cold int init(AVCodecParserContext *s)
{
    H264ParseContext *p = s->priv_data;
    H264Context      *h = &p->h;

    h->slice_ctx = av_mallocz(sizeof(*h->slice_ctx));
    if (!h->slice_ctx)
        return 0;
    h->nb_slice_ctx = 1;

    h->slice_context_count = 1;
    ff_h264dsp_init(&h->h264dsp, 8, 1);
    return 0;
}

AVCodecParser ff_h264_parser = {
    .codec_ids      = { AV_CODEC_ID_H264 },
    .priv_data_size = sizeof(H264ParseContext),
    .parser_init    = init,
    .parser_parse   = h264_parse,
    .parser_close   = h264_close,
    .split          = h264_split,
};
