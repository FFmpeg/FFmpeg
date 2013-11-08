/*
 * MPEG1/2 encoder
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 * MPEG1/2 encoder
 */

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/timecode.h"
#include "avcodec.h"
#include "bytestream.h"
#include "mathops.h"
#include "mpeg12.h"
#include "mpeg12data.h"
#include "mpegvideo.h"


static const uint8_t inv_non_linear_qscale[] = {
    0, 2, 4, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16,
};

static const uint8_t svcd_scan_offset_placeholder[] = {
    0x10, 0x0E, 0x00, 0x80, 0x81, 0x00, 0x80,
    0x81, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static uint8_t mv_penalty[MAX_FCODE + 1][MAX_MV * 2 + 1];
static uint8_t fcode_tab[MAX_MV * 2 + 1];

static uint8_t uni_mpeg1_ac_vlc_len[64 * 64 * 2];
static uint8_t uni_mpeg2_ac_vlc_len[64 * 64 * 2];

/* simple include everything table for dc, first byte is bits
 * number next 3 are code */
static uint32_t mpeg1_lum_dc_uni[512];
static uint32_t mpeg1_chr_dc_uni[512];

static uint8_t mpeg1_index_run[2][64];
static int8_t  mpeg1_max_level[2][64];

static av_cold void init_uni_ac_vlc(RLTable *rl, uint8_t *uni_ac_vlc_len)
{
    int i;

    for (i = 0; i < 128; i++) {
        int level = i - 64;
        int run;
        if (!level)
            continue;
        for (run = 0; run < 64; run++) {
            int len, code;
            int alevel = FFABS(level);

            if (alevel > rl->max_level[0][run])
                code = 111;                         /* rl->n */
            else
                code = rl->index_run[0][run] + alevel - 1;

            if (code < 111) {                       /* rl->n */
                /* length of VLC and sign */
                len = rl->table_vlc[code][1] + 1;
            } else {
                len = rl->table_vlc[111 /* rl->n */][1] + 6;

                if (alevel < 128)
                    len += 8;
                else
                    len += 16;
            }

            uni_ac_vlc_len[UNI_AC_ENC_INDEX(run, i)] = len;
        }
    }
}

static int find_frame_rate_index(MpegEncContext *s)
{
    int i;
    AVRational bestq = (AVRational) {0, 0};
    AVRational ext;
    AVRational target = av_inv_q(s->avctx->time_base);

    for (i = 1; i < 14; i++) {
        if (s->avctx->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL &&
            i >= 9)
            break;

        for (ext.num=1; ext.num <= 4; ext.num++) {
            for (ext.den=1; ext.den <= 32; ext.den++) {
                AVRational q = av_mul_q(ext, ff_mpeg12_frame_rate_tab[i]);

                if (s->codec_id != AV_CODEC_ID_MPEG2VIDEO && (ext.den!=1 || ext.num!=1))
                    continue;
                if (av_gcd(ext.den, ext.num) != 1)
                    continue;

                if (    bestq.num==0
                    || av_nearer_q(target, bestq, q) < 0
                    || ext.num==1 && ext.den==1 && av_nearer_q(target, bestq, q) == 0) {
                    bestq               = q;
                    s->frame_rate_index = i;
                    s->mpeg2_frame_rate_ext.num = ext.num;
                    s->mpeg2_frame_rate_ext.den = ext.den;
                }
            }
        }
    }

    if (av_cmp_q(target, bestq))
        return -1;
    else
        return 0;
}

static av_cold int encode_init(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;

    if (avctx->codec_id == AV_CODEC_ID_MPEG1VIDEO && avctx->height > 2800)
        avctx->thread_count = 1;

    if (ff_MPV_encode_init(avctx) < 0)
        return -1;

    if (find_frame_rate_index(s) < 0) {
        if (s->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
            av_log(avctx, AV_LOG_ERROR, "MPEG1/2 does not support %d/%d fps\n",
                   avctx->time_base.den, avctx->time_base.num);
            return -1;
        } else {
            av_log(avctx, AV_LOG_INFO,
                   "MPEG1/2 does not support %d/%d fps, there may be AV sync issues\n",
                   avctx->time_base.den, avctx->time_base.num);
        }
    }

    if (avctx->profile == FF_PROFILE_UNKNOWN) {
        if (avctx->level != FF_LEVEL_UNKNOWN) {
            av_log(avctx, AV_LOG_ERROR, "Set profile and level\n");
            return -1;
        }
        /* Main or 4:2:2 */
        avctx->profile = s->chroma_format == CHROMA_420 ? 4 : 0;
    }

    if (avctx->level == FF_LEVEL_UNKNOWN) {
        if (avctx->profile == 0) {                  /* 4:2:2 */
            if (avctx->width <= 720 && avctx->height <= 608)
                avctx->level = 5;                   /* Main */
            else
                avctx->level = 2;                   /* High */
        } else {
            if (avctx->profile != 1 && s->chroma_format != CHROMA_420) {
                av_log(avctx, AV_LOG_ERROR,
                       "Only High(1) and 4:2:2(0) profiles support 4:2:2 color sampling\n");
                return -1;
            }
            if (avctx->width <= 720 && avctx->height <= 576)
                avctx->level = 8;                   /* Main */
            else if (avctx->width <= 1440)
                avctx->level = 6;                   /* High 1440 */
            else
                avctx->level = 4;                   /* High */
        }
    }

    if ((avctx->width & 0xFFF) == 0 && (avctx->height & 0xFFF) == 1) {
        av_log(avctx, AV_LOG_ERROR, "Width / Height is invalid for MPEG2\n");
        return AVERROR(EINVAL);
    }

    if (s->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL) {
        if ((avctx->width & 0xFFF) == 0 || (avctx->height & 0xFFF) == 0) {
            av_log(avctx, AV_LOG_ERROR, "Width or Height are not allowed to be multiplies of 4096\n"
                                        "add '-strict %d' if you want to use them anyway.\n", FF_COMPLIANCE_UNOFFICIAL);
            return AVERROR(EINVAL);
        }
    }

    s->drop_frame_timecode = s->drop_frame_timecode || !!(avctx->flags2 & CODEC_FLAG2_DROP_FRAME_TIMECODE);
    if (s->drop_frame_timecode)
        s->tc.flags |= AV_TIMECODE_FLAG_DROPFRAME;
    if (s->drop_frame_timecode && s->frame_rate_index != 4) {
        av_log(avctx, AV_LOG_ERROR,
               "Drop frame time code only allowed with 1001/30000 fps\n");
        return -1;
    }

    if (s->tc_opt_str) {
        AVRational rate = ff_mpeg12_frame_rate_tab[s->frame_rate_index];
        int ret = av_timecode_init_from_string(&s->tc, rate, s->tc_opt_str, s);
        if (ret < 0)
            return ret;
        s->drop_frame_timecode = !!(s->tc.flags & AV_TIMECODE_FLAG_DROPFRAME);
        s->avctx->timecode_frame_start = s->tc.start;
    } else {
        s->avctx->timecode_frame_start = 0; // default is -1
    }
    return 0;
}

static void put_header(MpegEncContext *s, int header)
{
    avpriv_align_put_bits(&s->pb);
    put_bits(&s->pb, 16, header >> 16);
    put_sbits(&s->pb, 16, header);
}

/* put sequence header if needed */
static void mpeg1_encode_sequence_header(MpegEncContext *s)
{
    unsigned int vbv_buffer_size, fps, v;
    int i, constraint_parameter_flag;
    uint64_t time_code;
    float best_aspect_error = 1E10;
    float aspect_ratio      = av_q2d(s->avctx->sample_aspect_ratio);

    if (aspect_ratio == 0.0)
        aspect_ratio = 1.0;             // pixel aspect 1.1 (VGA)

    if (s->current_picture.f.key_frame) {
        AVRational framerate = ff_mpeg12_frame_rate_tab[s->frame_rate_index];

        /* mpeg1 header repeated every gop */
        put_header(s, SEQ_START_CODE);

        put_sbits(&s->pb, 12, s->width  & 0xFFF);
        put_sbits(&s->pb, 12, s->height & 0xFFF);

        for (i = 1; i < 15; i++) {
            float error = aspect_ratio;
            if (s->codec_id == AV_CODEC_ID_MPEG1VIDEO || i <= 1)
                error -= 1.0 / ff_mpeg1_aspect[i];
            else
                error -= av_q2d(ff_mpeg2_aspect[i]) * s->height / s->width;

            error = FFABS(error);

            if (error < best_aspect_error) {
                best_aspect_error    = error;
                s->aspect_ratio_info = i;
            }
        }

        put_bits(&s->pb, 4, s->aspect_ratio_info);
        put_bits(&s->pb, 4, s->frame_rate_index);

        if (s->avctx->rc_max_rate) {
            v = (s->avctx->rc_max_rate + 399) / 400;
            if (v > 0x3ffff && s->codec_id == AV_CODEC_ID_MPEG1VIDEO)
                v = 0x3ffff;
        } else {
            v = 0x3FFFF;
        }

        if (s->avctx->rc_buffer_size)
            vbv_buffer_size = s->avctx->rc_buffer_size;
        else
            /* VBV calculation: Scaled so that a VCD has the proper
             * VBV size of 40 kilobytes */
            vbv_buffer_size = ((20 * s->bit_rate) / (1151929 / 2)) * 8 * 1024;
        vbv_buffer_size = (vbv_buffer_size + 16383) / 16384;

        put_sbits(&s->pb, 18, v);
        put_bits(&s->pb, 1, 1);         // marker
        put_sbits(&s->pb, 10, vbv_buffer_size);

        constraint_parameter_flag =
            s->width  <= 768                                    &&
            s->height <= 576                                    &&
            s->mb_width * s->mb_height                 <= 396   &&
            s->mb_width * s->mb_height * framerate.num <= 396 * 25 * framerate.den &&
            framerate.num <= framerate.den * 30                 &&
            s->avctx->me_range                                  &&
            s->avctx->me_range < 128                            &&
            vbv_buffer_size <= 20                               &&
            v <= 1856000 / 400                                  &&
            s->codec_id == AV_CODEC_ID_MPEG1VIDEO;

        put_bits(&s->pb, 1, constraint_parameter_flag);

        ff_write_quant_matrix(&s->pb, s->avctx->intra_matrix);
        ff_write_quant_matrix(&s->pb, s->avctx->inter_matrix);

        if (s->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            put_header(s, EXT_START_CODE);
            put_bits(&s->pb, 4, 1);                 // seq ext

            put_bits(&s->pb, 1, s->avctx->profile == 0); // escx 1 for 4:2:2 profile

            put_bits(&s->pb, 3, s->avctx->profile); // profile
            put_bits(&s->pb, 4, s->avctx->level);   // level

            put_bits(&s->pb, 1, s->progressive_sequence);
            put_bits(&s->pb, 2, s->chroma_format);
            put_bits(&s->pb, 2, s->width  >> 12);
            put_bits(&s->pb, 2, s->height >> 12);
            put_bits(&s->pb, 12, v >> 18);          // bitrate ext
            put_bits(&s->pb, 1, 1);                 // marker
            put_bits(&s->pb, 8, vbv_buffer_size >> 10); // vbv buffer ext
            put_bits(&s->pb, 1, s->low_delay);
            put_bits(&s->pb, 2, s->mpeg2_frame_rate_ext.num-1); // frame_rate_ext_n
            put_bits(&s->pb, 5, s->mpeg2_frame_rate_ext.den-1); // frame_rate_ext_d
        }

        put_header(s, GOP_START_CODE);
        put_bits(&s->pb, 1, s->drop_frame_timecode);    // drop frame flag
        /* time code: we must convert from the real frame rate to a
         * fake MPEG frame rate in case of low frame rate */
        fps       = (framerate.num + framerate.den / 2) / framerate.den;
        time_code = s->current_picture_ptr->f.coded_picture_number +
                    s->avctx->timecode_frame_start;

        s->gop_picture_number = s->current_picture_ptr->f.coded_picture_number;

        av_assert0(s->drop_frame_timecode == !!(s->tc.flags & AV_TIMECODE_FLAG_DROPFRAME));
        if (s->drop_frame_timecode)
            time_code = av_timecode_adjust_ntsc_framenum2(time_code, fps);

        put_bits(&s->pb, 5, (uint32_t)((time_code / (fps * 3600)) % 24));
        put_bits(&s->pb, 6, (uint32_t)((time_code / (fps *   60)) % 60));
        put_bits(&s->pb, 1, 1);
        put_bits(&s->pb, 6, (uint32_t)((time_code / fps) % 60));
        put_bits(&s->pb, 6, (uint32_t)((time_code % fps)));
        put_bits(&s->pb, 1, !!(s->flags & CODEC_FLAG_CLOSED_GOP));
        put_bits(&s->pb, 1, 0);                     // broken link
    }
}

static inline void encode_mb_skip_run(MpegEncContext *s, int run)
{
    while (run >= 33) {
        put_bits(&s->pb, 11, 0x008);
        run -= 33;
    }
    put_bits(&s->pb, ff_mpeg12_mbAddrIncrTable[run][1],
             ff_mpeg12_mbAddrIncrTable[run][0]);
}

static av_always_inline void put_qscale(MpegEncContext *s)
{
    if (s->q_scale_type) {
        av_assert2(s->qscale >= 1 && s->qscale <= 12);
        put_bits(&s->pb, 5, inv_non_linear_qscale[s->qscale]);
    } else {
        put_bits(&s->pb, 5, s->qscale);
    }
}

void ff_mpeg1_encode_slice_header(MpegEncContext *s)
{
    if (s->codec_id == AV_CODEC_ID_MPEG2VIDEO && s->height > 2800) {
        put_header(s, SLICE_MIN_START_CODE + (s->mb_y & 127));
        /* slice_vertical_position_extension */
        put_bits(&s->pb, 3, s->mb_y >> 7);
    } else {
        put_header(s, SLICE_MIN_START_CODE + s->mb_y);
    }
    put_qscale(s);
    /* slice extra information */
    put_bits(&s->pb, 1, 0);
}

void ff_mpeg1_encode_picture_header(MpegEncContext *s, int picture_number)
{
    mpeg1_encode_sequence_header(s);

    /* mpeg1 picture header */
    put_header(s, PICTURE_START_CODE);
    /* temporal reference */

    // RAL: s->picture_number instead of s->fake_picture_number
    put_bits(&s->pb, 10,
             (s->picture_number - s->gop_picture_number) & 0x3ff);
    put_bits(&s->pb, 3, s->pict_type);

    s->vbv_delay_ptr = s->pb.buf + put_bits_count(&s->pb) / 8;
    put_bits(&s->pb, 16, 0xFFFF);               /* vbv_delay */

    // RAL: Forward f_code also needed for B-frames
    if (s->pict_type == AV_PICTURE_TYPE_P ||
        s->pict_type == AV_PICTURE_TYPE_B) {
        put_bits(&s->pb, 1, 0);                 /* half pel coordinates */
        if (s->codec_id == AV_CODEC_ID_MPEG1VIDEO)
            put_bits(&s->pb, 3, s->f_code);     /* forward_f_code */
        else
            put_bits(&s->pb, 3, 7);             /* forward_f_code */
    }

    // RAL: Backward f_code necessary for B-frames
    if (s->pict_type == AV_PICTURE_TYPE_B) {
        put_bits(&s->pb, 1, 0);                 /* half pel coordinates */
        if (s->codec_id == AV_CODEC_ID_MPEG1VIDEO)
            put_bits(&s->pb, 3, s->b_code);     /* backward_f_code */
        else
            put_bits(&s->pb, 3, 7);             /* backward_f_code */
    }

    put_bits(&s->pb, 1, 0);                     /* extra bit picture */

    s->frame_pred_frame_dct = 1;
    if (s->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        put_header(s, EXT_START_CODE);
        put_bits(&s->pb, 4, 8);                 /* pic ext */
        if (s->pict_type == AV_PICTURE_TYPE_P ||
            s->pict_type == AV_PICTURE_TYPE_B) {
            put_bits(&s->pb, 4, s->f_code);
            put_bits(&s->pb, 4, s->f_code);
        } else {
            put_bits(&s->pb, 8, 255);
        }
        if (s->pict_type == AV_PICTURE_TYPE_B) {
            put_bits(&s->pb, 4, s->b_code);
            put_bits(&s->pb, 4, s->b_code);
        } else {
            put_bits(&s->pb, 8, 255);
        }
        put_bits(&s->pb, 2, s->intra_dc_precision);

        av_assert0(s->picture_structure == PICT_FRAME);
        put_bits(&s->pb, 2, s->picture_structure);
        if (s->progressive_sequence)
            put_bits(&s->pb, 1, 0);             /* no repeat */
        else
            put_bits(&s->pb, 1, s->current_picture_ptr->f.top_field_first);
        /* XXX: optimize the generation of this flag with entropy measures */
        s->frame_pred_frame_dct = s->progressive_sequence;

        put_bits(&s->pb, 1, s->frame_pred_frame_dct);
        put_bits(&s->pb, 1, s->concealment_motion_vectors);
        put_bits(&s->pb, 1, s->q_scale_type);
        put_bits(&s->pb, 1, s->intra_vlc_format);
        put_bits(&s->pb, 1, s->alternate_scan);
        put_bits(&s->pb, 1, s->repeat_first_field);
        s->progressive_frame = s->progressive_sequence;
        /* chroma_420_type */
        put_bits(&s->pb, 1, s->chroma_format ==
                            CHROMA_420 ? s->progressive_frame : 0);
        put_bits(&s->pb, 1, s->progressive_frame);
        put_bits(&s->pb, 1, 0);                 /* composite_display_flag */
    }
    if (s->scan_offset) {
        int i;

        put_header(s, USER_START_CODE);
        for (i = 0; i < sizeof(svcd_scan_offset_placeholder); i++)
            put_bits(&s->pb, 8, svcd_scan_offset_placeholder[i]);
    }

    s->mb_y = 0;
    ff_mpeg1_encode_slice_header(s);
}

static inline void put_mb_modes(MpegEncContext *s, int n, int bits,
                                int has_mv, int field_motion)
{
    put_bits(&s->pb, n, bits);
    if (!s->frame_pred_frame_dct) {
        if (has_mv)
            /* motion_type: frame/field */
            put_bits(&s->pb, 2, 2 - field_motion);
        put_bits(&s->pb, 1, s->interlaced_dct);
    }
}

// RAL: Parameter added: f_or_b_code
static void mpeg1_encode_motion(MpegEncContext *s, int val, int f_or_b_code)
{
    if (val == 0) {
        /* zero vector */
        put_bits(&s->pb,
                 ff_mpeg12_mbMotionVectorTable[0][1],
                 ff_mpeg12_mbMotionVectorTable[0][0]);
    } else {
        int code, sign, bits;
        int bit_size = f_or_b_code - 1;
        int range    = 1 << bit_size;
        /* modulo encoding */
        val = sign_extend(val, 5 + bit_size);

        if (val >= 0) {
            val--;
            code = (val >> bit_size) + 1;
            bits = val & (range - 1);
            sign = 0;
        } else {
            val = -val;
            val--;
            code = (val >> bit_size) + 1;
            bits = val & (range - 1);
            sign = 1;
        }

        av_assert2(code > 0 && code <= 16);

        put_bits(&s->pb,
                 ff_mpeg12_mbMotionVectorTable[code][1],
                 ff_mpeg12_mbMotionVectorTable[code][0]);

        put_bits(&s->pb, 1, sign);
        if (bit_size > 0)
            put_bits(&s->pb, bit_size, bits);
    }
}

static inline void encode_dc(MpegEncContext *s, int diff, int component)
{
    if (((unsigned) (diff + 255)) >= 511) {
        int index;

        if (diff < 0) {
            index = av_log2_16bit(-2 * diff);
            diff--;
        } else {
            index = av_log2_16bit(2 * diff);
        }
        if (component == 0)
            put_bits(&s->pb,
                     ff_mpeg12_vlc_dc_lum_bits[index] + index,
                     (ff_mpeg12_vlc_dc_lum_code[index] << index) +
                     (diff & ((1 << index) - 1)));
        else
            put_bits(&s->pb,
                     ff_mpeg12_vlc_dc_chroma_bits[index] + index,
                     (ff_mpeg12_vlc_dc_chroma_code[index] << index) +
                     (diff & ((1 << index) - 1)));
    } else {
        if (component == 0)
            put_bits(&s->pb,
                     mpeg1_lum_dc_uni[diff + 255] & 0xFF,
                     mpeg1_lum_dc_uni[diff + 255] >> 8);
        else
            put_bits(&s->pb,
                     mpeg1_chr_dc_uni[diff + 255] & 0xFF,
                     mpeg1_chr_dc_uni[diff + 255] >> 8);
    }
}

static void mpeg1_encode_block(MpegEncContext *s, int16_t *block, int n)
{
    int alevel, level, last_non_zero, dc, diff, i, j, run, last_index, sign;
    int code, component;
    const uint16_t (*table_vlc)[2] = ff_rl_mpeg1.table_vlc;

    last_index = s->block_last_index[n];

    /* DC coef */
    if (s->mb_intra) {
        component = (n <= 3 ? 0 : (n & 1) + 1);
        dc        = block[0];                   /* overflow is impossible */
        diff      = dc - s->last_dc[component];
        encode_dc(s, diff, component);
        s->last_dc[component] = dc;
        i = 1;
        if (s->intra_vlc_format)
            table_vlc = ff_rl_mpeg2.table_vlc;
    } else {
        /* encode the first coefficient: needs to be done here because
         * it is handled slightly differently */
        level = block[0];
        if (abs(level) == 1) {
            code = ((uint32_t)level >> 31);     /* the sign bit */
            put_bits(&s->pb, 2, code | 0x02);
            i = 1;
        } else {
            i             = 0;
            last_non_zero = -1;
            goto next_coef;
        }
    }

    /* now quantify & encode AC coefs */
    last_non_zero = i - 1;

    for (; i <= last_index; i++) {
        j     = s->intra_scantable.permutated[i];
        level = block[j];

next_coef:
        /* encode using VLC */
        if (level != 0) {
            run = i - last_non_zero - 1;

            alevel = level;
            MASK_ABS(sign, alevel);
            sign &= 1;

            if (alevel <= mpeg1_max_level[0][run]) {
                code = mpeg1_index_run[0][run] + alevel - 1;
                /* store the VLC & sign at once */
                put_bits(&s->pb, table_vlc[code][1] + 1,
                         (table_vlc[code][0] << 1) + sign);
            } else {
                /* escape seems to be pretty rare <5% so I do not optimize it */
                put_bits(&s->pb, table_vlc[111][1], table_vlc[111][0]);
                /* escape: only clip in this case */
                put_bits(&s->pb, 6, run);
                if (s->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
                    if (alevel < 128) {
                        put_sbits(&s->pb, 8, level);
                    } else {
                        if (level < 0)
                            put_bits(&s->pb, 16, 0x8001 + level + 255);
                        else
                            put_sbits(&s->pb, 16, level);
                    }
                } else {
                    put_sbits(&s->pb, 12, level);
                }
            }
            last_non_zero = i;
        }
    }
    /* end of block */
    put_bits(&s->pb, table_vlc[112][1], table_vlc[112][0]);
}

static av_always_inline void mpeg1_encode_mb_internal(MpegEncContext *s,
                                                      int16_t block[6][64],
                                                      int motion_x, int motion_y,
                                                      int mb_block_count)
{
    int i, cbp;
    const int mb_x     = s->mb_x;
    const int mb_y     = s->mb_y;
    const int first_mb = mb_x == s->resync_mb_x && mb_y == s->resync_mb_y;

    /* compute cbp */
    cbp = 0;
    for (i = 0; i < mb_block_count; i++)
        if (s->block_last_index[i] >= 0)
            cbp |= 1 << (mb_block_count - 1 - i);

    if (cbp == 0 && !first_mb && s->mv_type == MV_TYPE_16X16 &&
        (mb_x != s->mb_width - 1 ||
         (mb_y != s->end_mb_y - 1 && s->codec_id == AV_CODEC_ID_MPEG1VIDEO)) &&
        ((s->pict_type == AV_PICTURE_TYPE_P && (motion_x | motion_y) == 0) ||
         (s->pict_type == AV_PICTURE_TYPE_B && s->mv_dir == s->last_mv_dir &&
          (((s->mv_dir & MV_DIR_FORWARD)
            ? ((s->mv[0][0][0] - s->last_mv[0][0][0]) |
               (s->mv[0][0][1] - s->last_mv[0][0][1])) : 0) |
           ((s->mv_dir & MV_DIR_BACKWARD)
            ? ((s->mv[1][0][0] - s->last_mv[1][0][0]) |
               (s->mv[1][0][1] - s->last_mv[1][0][1])) : 0)) == 0))) {
        s->mb_skip_run++;
        s->qscale -= s->dquant;
        s->skip_count++;
        s->misc_bits++;
        s->last_bits++;
        if (s->pict_type == AV_PICTURE_TYPE_P) {
            s->last_mv[0][0][0] =
            s->last_mv[0][0][1] =
            s->last_mv[0][1][0] =
            s->last_mv[0][1][1] = 0;
        }
    } else {
        if (first_mb) {
            av_assert0(s->mb_skip_run == 0);
            encode_mb_skip_run(s, s->mb_x);
        } else {
            encode_mb_skip_run(s, s->mb_skip_run);
        }

        if (s->pict_type == AV_PICTURE_TYPE_I) {
            if (s->dquant && cbp) {
                /* macroblock_type: macroblock_quant = 1 */
                put_mb_modes(s, 2, 1, 0, 0);
                put_qscale(s);
            } else {
                /* macroblock_type: macroblock_quant = 0 */
                put_mb_modes(s, 1, 1, 0, 0);
                s->qscale -= s->dquant;
            }
            s->misc_bits += get_bits_diff(s);
            s->i_count++;
        } else if (s->mb_intra) {
            if (s->dquant && cbp) {
                put_mb_modes(s, 6, 0x01, 0, 0);
                put_qscale(s);
            } else {
                put_mb_modes(s, 5, 0x03, 0, 0);
                s->qscale -= s->dquant;
            }
            s->misc_bits += get_bits_diff(s);
            s->i_count++;
            memset(s->last_mv, 0, sizeof(s->last_mv));
        } else if (s->pict_type == AV_PICTURE_TYPE_P) {
            if (s->mv_type == MV_TYPE_16X16) {
                if (cbp != 0) {
                    if ((motion_x | motion_y) == 0) {
                        if (s->dquant) {
                            /* macroblock_pattern & quant */
                            put_mb_modes(s, 5, 1, 0, 0);
                            put_qscale(s);
                        } else {
                            /* macroblock_pattern only */
                            put_mb_modes(s, 2, 1, 0, 0);
                        }
                        s->misc_bits += get_bits_diff(s);
                    } else {
                        if (s->dquant) {
                            put_mb_modes(s, 5, 2, 1, 0);    /* motion + cbp */
                            put_qscale(s);
                        } else {
                            put_mb_modes(s, 1, 1, 1, 0);    /* motion + cbp */
                        }
                        s->misc_bits += get_bits_diff(s);
                        // RAL: f_code parameter added
                        mpeg1_encode_motion(s,
                                            motion_x - s->last_mv[0][0][0],
                                            s->f_code);
                        // RAL: f_code parameter added
                        mpeg1_encode_motion(s,
                                            motion_y - s->last_mv[0][0][1],
                                            s->f_code);
                        s->mv_bits += get_bits_diff(s);
                    }
                } else {
                    put_bits(&s->pb, 3, 1);         /* motion only */
                    if (!s->frame_pred_frame_dct)
                        put_bits(&s->pb, 2, 2);     /* motion_type: frame */
                    s->misc_bits += get_bits_diff(s);
                    // RAL: f_code parameter added
                    mpeg1_encode_motion(s,
                                        motion_x - s->last_mv[0][0][0],
                                        s->f_code);
                    // RAL: f_code parameter added
                    mpeg1_encode_motion(s,
                                        motion_y - s->last_mv[0][0][1],
                                        s->f_code);
                    s->qscale  -= s->dquant;
                    s->mv_bits += get_bits_diff(s);
                }
                s->last_mv[0][1][0] = s->last_mv[0][0][0] = motion_x;
                s->last_mv[0][1][1] = s->last_mv[0][0][1] = motion_y;
            } else {
                av_assert2(!s->frame_pred_frame_dct && s->mv_type == MV_TYPE_FIELD);

                if (cbp) {
                    if (s->dquant) {
                        put_mb_modes(s, 5, 2, 1, 1);    /* motion + cbp */
                        put_qscale(s);
                    } else {
                        put_mb_modes(s, 1, 1, 1, 1);    /* motion + cbp */
                    }
                } else {
                    put_bits(&s->pb, 3, 1);             /* motion only */
                    put_bits(&s->pb, 2, 1);             /* motion_type: field */
                    s->qscale -= s->dquant;
                }
                s->misc_bits += get_bits_diff(s);
                for (i = 0; i < 2; i++) {
                    put_bits(&s->pb, 1, s->field_select[0][i]);
                    mpeg1_encode_motion(s,
                                        s->mv[0][i][0] - s->last_mv[0][i][0],
                                        s->f_code);
                    mpeg1_encode_motion(s,
                                        s->mv[0][i][1] - (s->last_mv[0][i][1] >> 1),
                                        s->f_code);
                    s->last_mv[0][i][0] = s->mv[0][i][0];
                    s->last_mv[0][i][1] = 2 * s->mv[0][i][1];
                }
                s->mv_bits += get_bits_diff(s);
            }
            if (cbp) {
                if (s->chroma_y_shift) {
                    put_bits(&s->pb,
                             ff_mpeg12_mbPatTable[cbp][1],
                             ff_mpeg12_mbPatTable[cbp][0]);
                } else {
                    put_bits(&s->pb,
                             ff_mpeg12_mbPatTable[cbp >> 2][1],
                             ff_mpeg12_mbPatTable[cbp >> 2][0]);
                    put_sbits(&s->pb, 2, cbp);
                }
            }
            s->f_count++;
        } else {
            if (s->mv_type == MV_TYPE_16X16) {
                if (cbp) {                      // With coded bloc pattern
                    if (s->dquant) {
                        if (s->mv_dir == MV_DIR_FORWARD)
                            put_mb_modes(s, 6, 3, 1, 0);
                        else
                            put_mb_modes(s, 8 - s->mv_dir, 2, 1, 0);
                        put_qscale(s);
                    } else {
                        put_mb_modes(s, 5 - s->mv_dir, 3, 1, 0);
                    }
                } else {                        // No coded bloc pattern
                    put_bits(&s->pb, 5 - s->mv_dir, 2);
                    if (!s->frame_pred_frame_dct)
                        put_bits(&s->pb, 2, 2); /* motion_type: frame */
                    s->qscale -= s->dquant;
                }
                s->misc_bits += get_bits_diff(s);
                if (s->mv_dir & MV_DIR_FORWARD) {
                    mpeg1_encode_motion(s,
                                        s->mv[0][0][0] - s->last_mv[0][0][0],
                                        s->f_code);
                    mpeg1_encode_motion(s,
                                        s->mv[0][0][1] - s->last_mv[0][0][1],
                                        s->f_code);
                    s->last_mv[0][0][0] =
                    s->last_mv[0][1][0] = s->mv[0][0][0];
                    s->last_mv[0][0][1] =
                    s->last_mv[0][1][1] = s->mv[0][0][1];
                    s->f_count++;
                }
                if (s->mv_dir & MV_DIR_BACKWARD) {
                    mpeg1_encode_motion(s,
                                        s->mv[1][0][0] - s->last_mv[1][0][0],
                                        s->b_code);
                    mpeg1_encode_motion(s,
                                        s->mv[1][0][1] - s->last_mv[1][0][1],
                                        s->b_code);
                    s->last_mv[1][0][0] =
                    s->last_mv[1][1][0] = s->mv[1][0][0];
                    s->last_mv[1][0][1] =
                    s->last_mv[1][1][1] = s->mv[1][0][1];
                    s->b_count++;
                }
            } else {
                av_assert2(s->mv_type == MV_TYPE_FIELD);
                av_assert2(!s->frame_pred_frame_dct);
                if (cbp) {                      // With coded bloc pattern
                    if (s->dquant) {
                        if (s->mv_dir == MV_DIR_FORWARD)
                            put_mb_modes(s, 6, 3, 1, 1);
                        else
                            put_mb_modes(s, 8 - s->mv_dir, 2, 1, 1);
                        put_qscale(s);
                    } else {
                        put_mb_modes(s, 5 - s->mv_dir, 3, 1, 1);
                    }
                } else {                        // No coded bloc pattern
                    put_bits(&s->pb, 5 - s->mv_dir, 2);
                    put_bits(&s->pb, 2, 1);     /* motion_type: field */
                    s->qscale -= s->dquant;
                }
                s->misc_bits += get_bits_diff(s);
                if (s->mv_dir & MV_DIR_FORWARD) {
                    for (i = 0; i < 2; i++) {
                        put_bits(&s->pb, 1, s->field_select[0][i]);
                        mpeg1_encode_motion(s,
                                            s->mv[0][i][0] - s->last_mv[0][i][0],
                                            s->f_code);
                        mpeg1_encode_motion(s,
                                            s->mv[0][i][1] - (s->last_mv[0][i][1] >> 1),
                                            s->f_code);
                        s->last_mv[0][i][0] = s->mv[0][i][0];
                        s->last_mv[0][i][1] = s->mv[0][i][1] * 2;
                    }
                    s->f_count++;
                }
                if (s->mv_dir & MV_DIR_BACKWARD) {
                    for (i = 0; i < 2; i++) {
                        put_bits(&s->pb, 1, s->field_select[1][i]);
                        mpeg1_encode_motion(s,
                                            s->mv[1][i][0] - s->last_mv[1][i][0],
                                            s->b_code);
                        mpeg1_encode_motion(s,
                                            s->mv[1][i][1] - (s->last_mv[1][i][1] >> 1),
                                            s->b_code);
                        s->last_mv[1][i][0] = s->mv[1][i][0];
                        s->last_mv[1][i][1] = s->mv[1][i][1] * 2;
                    }
                    s->b_count++;
                }
            }
            s->mv_bits += get_bits_diff(s);
            if (cbp) {
                if (s->chroma_y_shift) {
                    put_bits(&s->pb,
                             ff_mpeg12_mbPatTable[cbp][1],
                             ff_mpeg12_mbPatTable[cbp][0]);
                } else {
                    put_bits(&s->pb,
                             ff_mpeg12_mbPatTable[cbp >> 2][1],
                             ff_mpeg12_mbPatTable[cbp >> 2][0]);
                    put_sbits(&s->pb, 2, cbp);
                }
            }
        }
        for (i = 0; i < mb_block_count; i++)
            if (cbp & (1 << (mb_block_count - 1 - i)))
                mpeg1_encode_block(s, block[i], i);
        s->mb_skip_run = 0;
        if (s->mb_intra)
            s->i_tex_bits += get_bits_diff(s);
        else
            s->p_tex_bits += get_bits_diff(s);
    }
}

void ff_mpeg1_encode_mb(MpegEncContext *s, int16_t block[6][64],
                        int motion_x, int motion_y)
{
    if (s->chroma_format == CHROMA_420)
        mpeg1_encode_mb_internal(s, block, motion_x, motion_y, 6);
    else
        mpeg1_encode_mb_internal(s, block, motion_x, motion_y, 8);
}

av_cold void ff_mpeg1_encode_init(MpegEncContext *s)
{
    static int done = 0;

    ff_mpeg12_common_init(s);

    if (!done) {
        int f_code;
        int mv;
        int i;

        done = 1;
        ff_init_rl(&ff_rl_mpeg1, ff_mpeg12_static_rl_table_store[0]);
        ff_init_rl(&ff_rl_mpeg2, ff_mpeg12_static_rl_table_store[1]);

        for (i = 0; i < 64; i++) {
            mpeg1_max_level[0][i] = ff_rl_mpeg1.max_level[0][i];
            mpeg1_index_run[0][i] = ff_rl_mpeg1.index_run[0][i];
        }

        init_uni_ac_vlc(&ff_rl_mpeg1, uni_mpeg1_ac_vlc_len);
        if (s->intra_vlc_format)
            init_uni_ac_vlc(&ff_rl_mpeg2, uni_mpeg2_ac_vlc_len);

        /* build unified dc encoding tables */
        for (i = -255; i < 256; i++) {
            int adiff, index;
            int bits, code;
            int diff = i;

            adiff = FFABS(diff);
            if (diff < 0)
                diff--;
            index = av_log2(2 * adiff);

            bits = ff_mpeg12_vlc_dc_lum_bits[index] + index;
            code = (ff_mpeg12_vlc_dc_lum_code[index] << index) +
                   (diff & ((1 << index) - 1));
            mpeg1_lum_dc_uni[i + 255] = bits + (code << 8);

            bits = ff_mpeg12_vlc_dc_chroma_bits[index] + index;
            code = (ff_mpeg12_vlc_dc_chroma_code[index] << index) +
                   (diff & ((1 << index) - 1));
            mpeg1_chr_dc_uni[i + 255] = bits + (code << 8);
        }

        for (f_code = 1; f_code <= MAX_FCODE; f_code++)
            for (mv = -MAX_MV; mv <= MAX_MV; mv++) {
                int len;

                if (mv == 0) {
                    len = ff_mpeg12_mbMotionVectorTable[0][1];
                } else {
                    int val, bit_size, code;

                    bit_size = f_code - 1;

                    val = mv;
                    if (val < 0)
                        val = -val;
                    val--;
                    code = (val >> bit_size) + 1;
                    if (code < 17)
                        len = ff_mpeg12_mbMotionVectorTable[code][1] +
                              1 + bit_size;
                    else
                        len = ff_mpeg12_mbMotionVectorTable[16][1] +
                              2 + bit_size;
                }

                mv_penalty[f_code][mv + MAX_MV] = len;
            }


        for (f_code = MAX_FCODE; f_code > 0; f_code--)
            for (mv = -(8 << f_code); mv < (8 << f_code); mv++)
                fcode_tab[mv + MAX_MV] = f_code;
    }
    s->me.mv_penalty = mv_penalty;
    s->fcode_tab     = fcode_tab;
    if (s->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
        s->min_qcoeff = -255;
        s->max_qcoeff = 255;
    } else {
        s->min_qcoeff = -2047;
        s->max_qcoeff = 2047;
    }
    if (s->intra_vlc_format) {
        s->intra_ac_vlc_length      =
        s->intra_ac_vlc_last_length = uni_mpeg2_ac_vlc_len;
    } else {
        s->intra_ac_vlc_length      =
        s->intra_ac_vlc_last_length = uni_mpeg1_ac_vlc_len;
    }
    s->inter_ac_vlc_length      =
    s->inter_ac_vlc_last_length = uni_mpeg1_ac_vlc_len;
}

#define OFFSET(x) offsetof(MpegEncContext, x)
#define VE AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
#define COMMON_OPTS                                                           \
    { "gop_timecode",        "MPEG GOP Timecode in hh:mm:ss[:;.]ff format",   \
      OFFSET(tc_opt_str), AV_OPT_TYPE_STRING, {.str=NULL}, CHAR_MIN, CHAR_MAX, VE },\
    { "intra_vlc",           "Use MPEG-2 intra VLC table.",                   \
      OFFSET(intra_vlc_format),    AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, VE }, \
    { "drop_frame_timecode", "Timecode is in drop frame format.",             \
      OFFSET(drop_frame_timecode), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, VE }, \
    { "scan_offset",         "Reserve space for SVCD scan offset user data.", \
      OFFSET(scan_offset),         AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, VE },

static const AVOption mpeg1_options[] = {
    COMMON_OPTS
    FF_MPV_COMMON_OPTS
    { NULL },
};

static const AVOption mpeg2_options[] = {
    COMMON_OPTS
    { "non_linear_quant", "Use nonlinear quantizer.",    OFFSET(q_scale_type),   AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, VE },
    { "alternate_scan",   "Enable alternate scantable.", OFFSET(alternate_scan), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, VE },
    FF_MPV_COMMON_OPTS
    { NULL },
};

#define mpeg12_class(x)                                 \
static const AVClass mpeg ## x ## _class = {            \
    .class_name = "mpeg" # x "video encoder",           \
    .item_name  = av_default_item_name,                 \
    .option     = mpeg ## x ## _options,                \
    .version    = LIBAVUTIL_VERSION_INT,                \
};

mpeg12_class(1)
mpeg12_class(2)

AVCodec ff_mpeg1video_encoder = {
    .name                 = "mpeg1video",
    .long_name            = NULL_IF_CONFIG_SMALL("MPEG-1 video"),
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_MPEG1VIDEO,
    .priv_data_size       = sizeof(MpegEncContext),
    .init                 = encode_init,
    .encode2              = ff_MPV_encode_picture,
    .close                = ff_MPV_encode_end,
    .supported_framerates = ff_mpeg12_frame_rate_tab + 1,
    .pix_fmts             = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUV420P,
                                                           AV_PIX_FMT_NONE },
    .capabilities         = CODEC_CAP_DELAY | CODEC_CAP_SLICE_THREADS,
    .priv_class           = &mpeg1_class,
};

AVCodec ff_mpeg2video_encoder = {
    .name                 = "mpeg2video",
    .long_name            = NULL_IF_CONFIG_SMALL("MPEG-2 video"),
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_MPEG2VIDEO,
    .priv_data_size       = sizeof(MpegEncContext),
    .init                 = encode_init,
    .encode2              = ff_MPV_encode_picture,
    .close                = ff_MPV_encode_end,
    .supported_framerates = ff_mpeg2_frame_rate_tab,
    .pix_fmts             = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUV420P,
                                                           AV_PIX_FMT_YUV422P,
                                                           AV_PIX_FMT_NONE },
    .capabilities         = CODEC_CAP_DELAY | CODEC_CAP_SLICE_THREADS,
    .priv_class           = &mpeg2_class,
};
