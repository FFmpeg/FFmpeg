/*
 * H.26L/H.264/AVC/JVT/14496-10/... parser
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * H.264 / AVC / MPEG4 part10 parser.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#define UNCHECKED_BITSTREAM_READER 1

#include "libavutil/attributes.h"
#include "parser.h"
#include "h264data.h"
#include "golomb.h"
#include "internal.h"


static int h264_find_frame_end(H264Context *h, const uint8_t *buf,
                               int buf_size)
{
    int i, j;
    uint32_t state;
    ParseContext *pc = &h->parse_context;
    int next_avc= h->is_avc ? 0 : buf_size;

//    mb_addr= pc->mb_addr - 1;
    state = pc->state;
    if (state > 13)
        state = 7;

    if (h->is_avc && !h->nal_length_size)
        av_log(h->avctx, AV_LOG_ERROR, "AVC-parser: nal length size invalid\n");

    for (i = 0; i < buf_size; i++) {
        if (i >= next_avc) {
            int nalsize = 0;
            i = next_avc;
            for (j = 0; j < h->nal_length_size; j++)
                nalsize = (nalsize << 8) | buf[i++];
            if (nalsize <= 0 || nalsize > buf_size - i) {
                av_log(h->avctx, AV_LOG_ERROR, "AVC-parser: nal size %d remaining %d\n", nalsize, buf_size - i);
                return buf_size;
            }
            next_avc = i + nalsize;
            state    = 5;
        }

        if (state == 7) {
#if HAVE_FAST_UNALIGNED
            /* we check i < buf_size instead of i + 3 / 7 because it is
             * simpler and there must be FF_INPUT_BUFFER_PADDING_SIZE
             * bytes at the end.
             */
#    if HAVE_FAST_64BIT
            while (i < next_avc &&
                   !((~*(const uint64_t *)(buf + i) &
                      (*(const uint64_t *)(buf + i) - 0x0101010101010101ULL)) &
                      0x8080808080808080ULL))
                i += 8;
#    else
            while (i < next_avc &&
                   !((~*(const uint32_t *)(buf + i) &
                      (*(const uint32_t *)(buf + i) - 0x01010101U)) &
                      0x80808080U))
                i += 4;
#    endif
#endif
            for (; i < next_avc; i++)
                if (!buf[i]) {
                    state = 2;
                    break;
                }
        } else if (state <= 2) {
            if (buf[i] == 1)
                state ^= 5;            // 2->7, 1->4, 0->5
            else if (buf[i])
                state = 7;
            else
                state >>= 1;           // 2->1, 1->0, 0->0
        } else if (state <= 5) {
            int v = buf[i] & 0x1F;
            if (v == 6 || v == 7 || v == 8 || v == 9) {
                if (pc->frame_start_found) {
                    i++;
                    goto found;
                }
            } else if (v == 1 || v == 2 || v == 5) {
                state += 8;
                continue;
            }
            state = 7;
        } else {
            h->parse_history[h->parse_history_count++]= buf[i];
            if (h->parse_history_count>3) {
                unsigned int mb, last_mb= h->parse_last_mb;
                GetBitContext gb;

                init_get_bits(&gb, h->parse_history, 8*h->parse_history_count);
                h->parse_history_count=0;
                mb= get_ue_golomb_long(&gb);
                last_mb= h->parse_last_mb;
                h->parse_last_mb= mb;
                if (pc->frame_start_found) {
                    if (mb <= last_mb)
                        goto found;
                } else
                    pc->frame_start_found = 1;
                state = 7;
            }
        }
    }
    pc->state = state;
    if (h->is_avc)
        return next_avc;
    return END_NOT_FOUND;

found:
    pc->state             = 7;
    pc->frame_start_found = 0;
    if (h->is_avc)
        return next_avc;
    return i - (state & 5) - 3 * (state > 7);
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
    H264Context *h         = s->priv_data;
    const uint8_t *buf_end = buf + buf_size;
    unsigned int pps_id;
    unsigned int slice_type;
    int state = -1;
    const uint8_t *ptr;
    int q264 = buf_size >=4 && !memcmp("Q264", buf, 4);
    int field_poc[2];

    /* set some sane default values */
    s->pict_type         = AV_PICTURE_TYPE_I;
    s->key_frame         = 0;
    s->picture_structure = AV_PICTURE_STRUCTURE_UNKNOWN;

    h->avctx                        = avctx;
    h->sei_recovery_frame_cnt       = -1;
    h->sei_dpb_output_delay         = 0;
    h->sei_cpb_removal_delay        = -1;
    h->sei_buffering_period_present = 0;

    if (!buf_size)
        return 0;

    for (;;) {
        int src_length, dst_length, consumed, nalsize = 0;
        if (h->is_avc) {
            int i;
            if (h->nal_length_size >= buf_end - buf) break;
            nalsize = 0;
            for (i = 0; i < h->nal_length_size; i++)
                nalsize = (nalsize << 8) | *buf++;
            if (nalsize <= 0 || nalsize > buf_end - buf) {
                av_log(h->avctx, AV_LOG_ERROR, "AVC: nal size %d\n", nalsize);
                break;
            }
            src_length = nalsize;
        } else {
        buf = avpriv_find_start_code(buf, buf_end, &state);
        if (buf >= buf_end)
            break;
        --buf;
        src_length = buf_end - buf;
        }
        switch (state & 0x1f) {
        case NAL_SLICE:
        case NAL_IDR_SLICE:
            // Do not walk the whole buffer just to decode slice header
            if (src_length > 20)
                src_length = 20;
            break;
        }
        ptr = ff_h264_decode_nal(h, buf, &dst_length, &consumed, src_length);
        if (ptr == NULL || dst_length < 0)
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
            get_ue_golomb_long(&h->gb);  // skip first_mb_in_slice
            slice_type   = get_ue_golomb_31(&h->gb);
            s->pict_type = golomb_to_pict_type[slice_type % 5];
            if (h->sei_recovery_frame_cnt >= 0) {
                /* key frame, since recovery_frame_cnt is set */
                s->key_frame = 1;
            }
            pps_id = get_ue_golomb(&h->gb);
            if (pps_id >= MAX_PPS_COUNT) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "pps_id out of range\n");
                return -1;
            }
            if (!h->pps_buffers[pps_id]) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "non-existing PPS referenced\n");
                return -1;
            }
            h->pps = *h->pps_buffers[pps_id];
            if (!h->sps_buffers[h->pps.sps_id]) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "non-existing SPS referenced\n");
                return -1;
            }
            h->sps       = *h->sps_buffers[h->pps.sps_id];
            h->frame_num = get_bits(&h->gb, h->sps.log2_max_frame_num);

            avctx->profile = ff_h264_get_profile(&h->sps);
            avctx->level   = h->sps.level_idc;

            if (h->sps.frame_mbs_only_flag) {
                h->picture_structure = PICT_FRAME;
            } else {
                if (get_bits1(&h->gb)) { // field_pic_flag
                    h->picture_structure = PICT_TOP_FIELD + get_bits1(&h->gb); // bottom_field_flag
                } else {
                    h->picture_structure = PICT_FRAME;
                }
            }

            if (h->nal_unit_type == NAL_IDR_SLICE)
                get_ue_golomb(&h->gb); /* idr_pic_id */
            if (h->sps.poc_type == 0) {
                h->poc_lsb = get_bits(&h->gb, h->sps.log2_max_poc_lsb);

                if (h->pps.pic_order_present == 1 &&
                    h->picture_structure == PICT_FRAME)
                    h->delta_poc_bottom = get_se_golomb(&h->gb);
            }

            if (h->sps.poc_type == 1 &&
                !h->sps.delta_pic_order_always_zero_flag) {
                h->delta_poc[0] = get_se_golomb(&h->gb);

                if (h->pps.pic_order_present == 1 &&
                    h->picture_structure == PICT_FRAME)
                    h->delta_poc[1] = get_se_golomb(&h->gb);
            }

            ff_init_poc(h, field_poc, NULL);

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
        buf += h->is_avc ? nalsize : consumed;
    }
    if (q264)
        return 0;
    /* didn't find a picture! */
    av_log(h->avctx, AV_LOG_ERROR, "missing picture in access unit with size %d\n", buf_size);
    return -1;
}

static int h264_parse(AVCodecParserContext *s,
                      AVCodecContext *avctx,
                      const uint8_t **poutbuf, int *poutbuf_size,
                      const uint8_t *buf, int buf_size)
{
    H264Context *h   = s->priv_data;
    ParseContext *pc = &h->parse_context;
    int next;

    if (!h->got_first) {
        h->got_first = 1;
        if (avctx->extradata_size) {
            h->avctx = avctx;
            // must be done like in decoder, otherwise opening the parser,
            // letting it create extradata and then closing and opening again
            // will cause has_b_frames to be always set.
            // Note that estimate_timings_from_pts does exactly this.
            if (!avctx->has_b_frames)
                h->low_delay = 1;
            ff_h264_decode_extradata(h, avctx->extradata, avctx->extradata_size);
        }
    }

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
    } else {
        next = h264_find_frame_end(h, buf, buf_size);

        if (ff_combine_frame(pc, next, &buf, &buf_size) < 0) {
            *poutbuf      = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }

        if (next < 0 && next != END_NOT_FOUND) {
            av_assert1(pc->last_index + next >= 0);
            h264_find_frame_end(h, &pc->buffer[pc->last_index + next], -next); // update state
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
        /*  if ((state&0xFFFFFF1F) == 0x101 ||
         *     (state&0xFFFFFF1F) == 0x102 ||
         *     (state&0xFFFFFF1F) == 0x105) {
         *  }
         */
        if ((state & 0xFFFFFF00) == 0x100 && (state & 0xFFFFFF1F) != 0x107 &&
            (state & 0xFFFFFF1F) != 0x108 && (state & 0xFFFFFF1F) != 0x109) {
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

static void close(AVCodecParserContext *s)
{
    H264Context *h   = s->priv_data;
    ParseContext *pc = &h->parse_context;

    av_free(pc->buffer);
    ff_h264_free_context(h);
}

static av_cold int init(AVCodecParserContext *s)
{
    H264Context *h = s->priv_data;
    h->thread_context[0]   = h;
    h->slice_context_count = 1;
    return 0;
}

AVCodecParser ff_h264_parser = {
    .codec_ids      = { AV_CODEC_ID_H264 },
    .priv_data_size = sizeof(H264Context),
    .parser_init    = init,
    .parser_parse   = h264_parse,
    .parser_close   = close,
    .split          = h264_split,
};
