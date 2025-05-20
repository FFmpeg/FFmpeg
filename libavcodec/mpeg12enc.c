/*
 * MPEG-1/2 encoder
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
 * MPEG-1/2 encoder
 */

#include <assert.h>
#include <stdint.h>

#include "config.h"
#include "config_components.h"
#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/thread.h"
#include "libavutil/timecode.h"
#include "libavutil/stereo3d.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "mathops.h"
#include "mpeg12.h"
#include "mpeg12data.h"
#include "mpeg12enc.h"
#include "mpeg12vlc.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "mpegvideoenc.h"
#include "profiles.h"
#include "put_bits.h"
#include "rl.h"

#if CONFIG_MPEG1VIDEO_ENCODER || CONFIG_MPEG2VIDEO_ENCODER
static const uint8_t svcd_scan_offset_placeholder[] = {
    0x10, 0x0E, 0x00, 0x80, 0x81, 0x00, 0x80,
    0x81, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static uint8_t mv_penalty[MAX_FCODE + 1][MAX_DMV * 2 + 1];
static uint8_t fcode_tab[MAX_MV * 2 + 1];

static uint8_t uni_mpeg1_ac_vlc_len[64 * 64 * 2];
static uint8_t uni_mpeg2_ac_vlc_len[64 * 64 * 2];

static uint8_t mpeg12_max_level[MAX_LEVEL + 1];
static uint8_t mpeg12_index_run[MAX_RUN   + 1];

/* simple include everything table for dc, first byte is bits
 * number next 3 are code */
static uint32_t mpeg1_lum_dc_uni[512];
static uint32_t mpeg1_chr_dc_uni[512];

typedef struct MPEG12EncContext {
    MPVMainEncContext mpeg;
    AVRational frame_rate_ext;
    unsigned frame_rate_index;

    int gop_picture_number;  ///< index of the first picture of a GOP based on fake_pic_num

    int64_t timecode_frame_start; ///< GOP timecode frame start number, in non drop frame format
    AVTimecode tc;           ///< timecode context
    char *tc_opt_str;        ///< timecode option string
    int drop_frame_timecode; ///< timecode is in drop frame format.
    int scan_offset;         ///< reserve space for SVCD scan offset user data.

    int a53_cc;
    int seq_disp_ext;
    int video_format;
#define VIDEO_FORMAT_COMPONENT   0
#define VIDEO_FORMAT_PAL         1
#define VIDEO_FORMAT_NTSC        2
#define VIDEO_FORMAT_SECAM       3
#define VIDEO_FORMAT_MAC         4
#define VIDEO_FORMAT_UNSPECIFIED 5
} MPEG12EncContext;

#define A53_MAX_CC_COUNT 0x1f
#endif /* CONFIG_MPEG1VIDEO_ENCODER || CONFIG_MPEG2VIDEO_ENCODER */

av_cold void ff_mpeg1_init_uni_ac_vlc(const int8_t max_level[],
                                      const uint8_t index_run[],
                                      const uint16_t table_vlc[][2],
                                      uint8_t uni_ac_vlc_len[])
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

            if (alevel > max_level[run])
                code = 111;                         /* rl->n */
            else
                code = index_run[run] + alevel - 1;

            if (code < 111) {                       /* rl->n */
                /* length of VLC and sign */
                len = table_vlc[code][1] + 1;
            } else {
                len = table_vlc[MPEG12_RL_NB_ELEMS][1] + 6;

                if (alevel < 128)
                    len += 8;
                else
                    len += 16;
            }

            uni_ac_vlc_len[UNI_AC_ENC_INDEX(run, i)] = len;
        }
    }
}

#if CONFIG_MPEG1VIDEO_ENCODER || CONFIG_MPEG2VIDEO_ENCODER
static void put_header(MPVEncContext *const s, uint32_t header)
{
    align_put_bits(&s->pb);
    put_bits32(&s->pb, header);
}

/* put sequence header if needed */
static void mpeg1_encode_sequence_header(MPEG12EncContext *mpeg12)
{
    MPVEncContext *const s = &mpeg12->mpeg.s;
    unsigned int vbv_buffer_size, fps, v;
    int constraint_parameter_flag;
    AVRational framerate = ff_mpeg12_frame_rate_tab[mpeg12->frame_rate_index];
    uint64_t time_code;
    int64_t best_aspect_error = INT64_MAX;
    AVRational aspect_ratio = s->c.avctx->sample_aspect_ratio;
    int aspect_ratio_info;

    put_bits_assume_flushed(&s->pb);

    if (!(s->c.cur_pic.ptr->f->flags & AV_FRAME_FLAG_KEY))
        return;

    if (aspect_ratio.num == 0 || aspect_ratio.den == 0)
        aspect_ratio = (AVRational){1,1};             // pixel aspect 1.1 (VGA)

    /* MPEG-1 header repeated every GOP */
    put_header(s, SEQ_START_CODE);

    put_sbits(&s->pb, 12, s->c.width  & 0xFFF);
    put_sbits(&s->pb, 12, s->c.height & 0xFFF);

    for (int i = 1; i < 15; i++) {
        int64_t error = aspect_ratio.num * (1LL<<32) / aspect_ratio.den;
        if (s->c.codec_id == AV_CODEC_ID_MPEG1VIDEO || i <= 1)
            error -= (1LL<<32) / ff_mpeg1_aspect[i];
        else
            error -= (1LL<<32)*ff_mpeg2_aspect[i].num * s->c.height / s->c.width / ff_mpeg2_aspect[i].den;

        error = FFABS(error);

        if (error - 2 <= best_aspect_error) {
            best_aspect_error = error;
            aspect_ratio_info = i;
        }
    }

    put_bits(&s->pb, 4, aspect_ratio_info);
    put_bits(&s->pb, 4, mpeg12->frame_rate_index);

    if (s->c.avctx->rc_max_rate) {
        v = (s->c.avctx->rc_max_rate + 399) / 400;
        if (v > 0x3ffff && s->c.codec_id == AV_CODEC_ID_MPEG1VIDEO)
            v = 0x3ffff;
    } else {
        v = 0x3FFFF;
    }

    if (s->c.avctx->rc_buffer_size)
        vbv_buffer_size = s->c.avctx->rc_buffer_size;
    else
        /* VBV calculation: Scaled so that a VCD has the proper
         * VBV size of 40 kilobytes */
        vbv_buffer_size = av_rescale_rnd(mpeg12->mpeg.bit_rate, 20, 1151929 / 2, AV_ROUND_ZERO) * 8 * 1024;
    vbv_buffer_size = (vbv_buffer_size + 16383) / 16384;

    put_sbits(&s->pb, 18, v);
    put_bits(&s->pb, 1, 1);         // marker
    put_sbits(&s->pb, 10, vbv_buffer_size);

    constraint_parameter_flag =
        s->c.width  <= 768                                  &&
        s->c.height <= 576                                  &&
        s->c.mb_width * s->c.mb_height               <= 396 &&
        s->c.mb_width * s->c.mb_height * framerate.num <= 396 * 25 * framerate.den &&
        framerate.num <= framerate.den * 30                 &&
        s->c.avctx->me_range                                &&
        s->c.avctx->me_range < 128                          &&
        vbv_buffer_size <= 20                               &&
        v <= 1856000 / 400                                  &&
        s->c.codec_id == AV_CODEC_ID_MPEG1VIDEO;

    put_bits(&s->pb, 1, constraint_parameter_flag);

    ff_write_quant_matrix(&s->pb, s->c.avctx->intra_matrix);
    ff_write_quant_matrix(&s->pb, s->c.avctx->inter_matrix);

    if (s->c.codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        const AVFrameSideData *side_data;
        int width = s->c.width;
        int height = s->c.height;
        int use_seq_disp_ext;

        put_header(s, EXT_START_CODE);
        put_bits(&s->pb, 4, 1);                 // seq ext

        put_bits(&s->pb, 1, s->c.avctx->profile == AV_PROFILE_MPEG2_422); // escx 1 for 4:2:2 profile

        put_bits(&s->pb, 3, s->c.avctx->profile); // profile
        put_bits(&s->pb, 4, s->c.avctx->level);   // level

        put_bits(&s->pb, 1, s->c.progressive_sequence);
        put_bits(&s->pb, 2, s->c.chroma_format);
        put_bits(&s->pb, 2, s->c.width  >> 12);
        put_bits(&s->pb, 2, s->c.height >> 12);
        put_bits(&s->pb, 12, v >> 18);          // bitrate ext
        put_bits(&s->pb, 1, 1);                 // marker
        put_bits(&s->pb, 8, vbv_buffer_size >> 10); // vbv buffer ext
        put_bits(&s->pb, 1, s->c.low_delay);
        put_bits(&s->pb, 2, mpeg12->frame_rate_ext.num-1); // frame_rate_ext_n
        put_bits(&s->pb, 5, mpeg12->frame_rate_ext.den-1); // frame_rate_ext_d

        side_data = av_frame_get_side_data(s->c.cur_pic.ptr->f, AV_FRAME_DATA_PANSCAN);
        if (side_data) {
            const AVPanScan *pan_scan = (AVPanScan *)side_data->data;
            if (pan_scan->width && pan_scan->height) {
                width  = pan_scan->width  >> 4;
                height = pan_scan->height >> 4;
            }
        }

        use_seq_disp_ext = (width != s->c.width ||
                            height != s->c.height ||
                            s->c.avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
                            s->c.avctx->color_trc != AVCOL_TRC_UNSPECIFIED ||
                            s->c.avctx->colorspace != AVCOL_SPC_UNSPECIFIED ||
                            mpeg12->video_format != VIDEO_FORMAT_UNSPECIFIED);

        if (mpeg12->seq_disp_ext == 1 ||
            (mpeg12->seq_disp_ext == -1 && use_seq_disp_ext)) {
            put_header(s, EXT_START_CODE);
            put_bits(&s->pb, 4, 2);                         // sequence display extension
            put_bits(&s->pb, 3, mpeg12->video_format);      // video_format
            put_bits(&s->pb, 1, 1);                         // colour_description
            put_bits(&s->pb, 8, s->c.avctx->color_primaries); // colour_primaries
            put_bits(&s->pb, 8, s->c.avctx->color_trc);       // transfer_characteristics
            put_bits(&s->pb, 8, s->c.avctx->colorspace);      // matrix_coefficients
            put_bits(&s->pb, 14, width);                    // display_horizontal_size
            put_bits(&s->pb, 1, 1);                         // marker_bit
            put_bits(&s->pb, 14, height);                   // display_vertical_size
            put_bits(&s->pb, 3, 0);                         // remaining 3 bits are zero padding
        }
    }

    put_header(s, GOP_START_CODE);
    put_bits(&s->pb, 1, mpeg12->drop_frame_timecode);    // drop frame flag
    /* time code: we must convert from the real frame rate to a
     * fake MPEG frame rate in case of low frame rate */
    fps       = (framerate.num + framerate.den / 2) / framerate.den;
    time_code = s->c.cur_pic.ptr->coded_picture_number +
                mpeg12->timecode_frame_start;

    mpeg12->gop_picture_number = s->c.cur_pic.ptr->coded_picture_number;

    av_assert0(mpeg12->drop_frame_timecode == !!(mpeg12->tc.flags & AV_TIMECODE_FLAG_DROPFRAME));
    if (mpeg12->drop_frame_timecode)
        time_code = av_timecode_adjust_ntsc_framenum2(time_code, fps);

    put_bits(&s->pb, 5, (uint32_t)((time_code / (fps * 3600)) % 24));
    put_bits(&s->pb, 6, (uint32_t)((time_code / (fps *   60)) % 60));
    put_bits(&s->pb, 1, 1);
    put_bits(&s->pb, 6, (uint32_t)((time_code / fps) % 60));
    put_bits(&s->pb, 6, (uint32_t)((time_code % fps)));
    put_bits(&s->pb, 1, !!(s->c.avctx->flags & AV_CODEC_FLAG_CLOSED_GOP) ||
                        mpeg12->mpeg.intra_only || !mpeg12->gop_picture_number);
    put_bits(&s->pb, 1, 0);                     // broken link
}

static inline void encode_mb_skip_run(MPVEncContext *const s, int run)
{
    while (run >= 33) {
        put_bits(&s->pb, 11, 0x008);
        run -= 33;
    }
    put_bits(&s->pb, ff_mpeg12_mbAddrIncrTable[run][1],
             ff_mpeg12_mbAddrIncrTable[run][0]);
}

static av_always_inline void put_qscale(MPVEncContext *const s)
{
    put_bits(&s->pb, 5, s->c.qscale);
}

void ff_mpeg1_encode_slice_header(MPVEncContext *const s)
{
    if (s->c.codec_id == AV_CODEC_ID_MPEG2VIDEO && s->c.height > 2800) {
        put_header(s, SLICE_MIN_START_CODE + (s->c.mb_y & 127));
        /* slice_vertical_position_extension */
        put_bits(&s->pb, 3, s->c.mb_y >> 7);
    } else {
        av_assert1(s->c.mb_y <= SLICE_MAX_START_CODE - SLICE_MIN_START_CODE);
        put_header(s, SLICE_MIN_START_CODE + s->c.mb_y);
    }
    put_qscale(s);
    /* slice extra information */
    put_bits(&s->pb, 1, 0);
}

static int mpeg1_encode_picture_header(MPVMainEncContext *const m)
{
    MPEG12EncContext *const mpeg12 = (MPEG12EncContext*)m;
    MPVEncContext *const s = &m->s;
    const AVFrameSideData *side_data;

    put_bits_assume_flushed(&s->pb);

    mpeg1_encode_sequence_header(mpeg12);

    /* MPEG-1 picture header */
    put_header(s, PICTURE_START_CODE);
    /* temporal reference */

    // RAL: s->c.picture_number instead of s->fake_picture_number
    put_bits(&s->pb, 10,
             (s->c.picture_number - mpeg12->gop_picture_number) & 0x3ff);
    put_bits(&s->pb, 3, s->c.pict_type);

    m->vbv_delay_pos = put_bytes_count(&s->pb, 0);
    put_bits(&s->pb, 16, 0xFFFF);               /* vbv_delay */

    // RAL: Forward f_code also needed for B-frames
    if (s->c.pict_type == AV_PICTURE_TYPE_P ||
        s->c.pict_type == AV_PICTURE_TYPE_B) {
        put_bits(&s->pb, 1, 0);                 /* half pel coordinates */
        if (s->c.codec_id == AV_CODEC_ID_MPEG1VIDEO)
            put_bits(&s->pb, 3, s->f_code);     /* forward_f_code */
        else
            put_bits(&s->pb, 3, 7);             /* forward_f_code */
    }

    // RAL: Backward f_code necessary for B-frames
    if (s->c.pict_type == AV_PICTURE_TYPE_B) {
        put_bits(&s->pb, 1, 0);                 /* half pel coordinates */
        if (s->c.codec_id == AV_CODEC_ID_MPEG1VIDEO)
            put_bits(&s->pb, 3, s->b_code);     /* backward_f_code */
        else
            put_bits(&s->pb, 3, 7);             /* backward_f_code */
    }

    put_bits(&s->pb, 1, 0);                     /* extra bit picture */

    s->c.frame_pred_frame_dct = 1;
    if (s->c.codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        put_header(s, EXT_START_CODE);
        put_bits(&s->pb, 4, 8);                 /* pic ext */
        if (s->c.pict_type == AV_PICTURE_TYPE_P ||
            s->c.pict_type == AV_PICTURE_TYPE_B) {
            put_bits(&s->pb, 4, s->f_code);
            put_bits(&s->pb, 4, s->f_code);
        } else {
            put_bits(&s->pb, 8, 255);
        }
        if (s->c.pict_type == AV_PICTURE_TYPE_B) {
            put_bits(&s->pb, 4, s->b_code);
            put_bits(&s->pb, 4, s->b_code);
        } else {
            put_bits(&s->pb, 8, 255);
        }
        put_bits(&s->pb, 2, s->c.intra_dc_precision);

        av_assert0(s->c.picture_structure == PICT_FRAME);
        put_bits(&s->pb, 2, s->c.picture_structure);
        if (s->c.progressive_sequence)
            put_bits(&s->pb, 1, 0);             /* no repeat */
        else
            put_bits(&s->pb, 1, !!(s->c.cur_pic.ptr->f->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST));
        /* XXX: optimize the generation of this flag with entropy measures */
        s->c.frame_pred_frame_dct = s->c.progressive_sequence;

        put_bits(&s->pb, 1, s->c.frame_pred_frame_dct);
        put_bits(&s->pb, 1, s->c.concealment_motion_vectors);
        put_bits(&s->pb, 1, s->c.q_scale_type);
        put_bits(&s->pb, 1, s->c.intra_vlc_format);
        put_bits(&s->pb, 1, s->c.alternate_scan);
        put_bits(&s->pb, 1, s->c.repeat_first_field);
        s->c.progressive_frame = s->c.progressive_sequence;
        /* chroma_420_type */
        put_bits(&s->pb, 1, s->c.chroma_format ==
                            CHROMA_420 ? s->c.progressive_frame : 0);
        put_bits(&s->pb, 1, s->c.progressive_frame);
        put_bits(&s->pb, 1, 0);                 /* composite_display_flag */
    }
    if (mpeg12->scan_offset) {
        int i;

        put_header(s, USER_START_CODE);
        for (i = 0; i < sizeof(svcd_scan_offset_placeholder); i++)
            put_bits(&s->pb, 8, svcd_scan_offset_placeholder[i]);
    }
    side_data = av_frame_get_side_data(s->c.cur_pic.ptr->f,
                                       AV_FRAME_DATA_STEREO3D);
    if (side_data) {
        const AVStereo3D *stereo = (AVStereo3D *)side_data->data;
        uint8_t fpa_type;

        switch (stereo->type) {
        case AV_STEREO3D_SIDEBYSIDE:
            fpa_type = 0x03;
            break;
        case AV_STEREO3D_TOPBOTTOM:
            fpa_type = 0x04;
            break;
        case AV_STEREO3D_2D:
            fpa_type = 0x08;
            break;
        case AV_STEREO3D_SIDEBYSIDE_QUINCUNX:
            fpa_type = 0x23;
            break;
        default:
            fpa_type = 0;
            break;
        }

        if (fpa_type != 0) {
            put_header(s, USER_START_CODE);
            // S3D_video_format_signaling_identifier
            put_bits32(&s->pb, MKBETAG('J','P','3','D'));
            put_bits(&s->pb, 8, 0x03);  // S3D_video_format_length

            put_bits(&s->pb, 1, 1);     // reserved_bit
            put_bits(&s->pb, 7, fpa_type); // S3D_video_format_type
            put_bits(&s->pb, 16, 0x04FF);  // reserved_data
        }
    }

    if (CONFIG_MPEG2VIDEO_ENCODER && mpeg12->a53_cc) {
        side_data = av_frame_get_side_data(s->c.cur_pic.ptr->f,
            AV_FRAME_DATA_A53_CC);
        if (side_data) {
            if (side_data->size <= A53_MAX_CC_COUNT * 3 && side_data->size % 3 == 0) {
                put_header (s, USER_START_CODE);

                put_bits32(&s->pb, MKBETAG('G','A','9','4')); // user_identifier
                put_bits(&s->pb, 8, 3);                     // user_data_type_code
                put_bits(&s->pb, 8,
                    (side_data->size / 3 & A53_MAX_CC_COUNT) | 0x40); // flags, cc_count
                put_bits(&s->pb, 8, 0xff);                  // em_data

                for (int i = 0; i < side_data->size; i++)
                    put_bits(&s->pb, 8, side_data->data[i]);

                put_bits(&s->pb, 8, 0xff);                  // marker_bits
            } else {
                av_log(s->c.avctx, AV_LOG_WARNING,
                    "Closed Caption size (%"SIZE_SPECIFIER") can not exceed "
                    "93 bytes and must be a multiple of 3\n", side_data->size);
            }
        }
    }

    s->c.mb_y = 0;
    ff_mpeg1_encode_slice_header(s);

    return 0;
}

static inline void put_mb_modes(MPVEncContext *const s, int n, int bits,
                                int has_mv, int field_motion)
{
    put_bits(&s->pb, n, bits);
    if (!s->c.frame_pred_frame_dct) {
        if (has_mv)
            /* motion_type: frame/field */
            put_bits(&s->pb, 2, 2 - field_motion);
        put_bits(&s->pb, 1, s->c.interlaced_dct);
    }
}

// RAL: Parameter added: f_or_b_code
static void mpeg1_encode_motion(MPVEncContext *const s, int val, int f_or_b_code)
{
    if (val == 0) {
        /* zero vector, corresponds to ff_mpeg12_mbMotionVectorTable[0] */
        put_bits(&s->pb, 1, 0x01);
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

static inline void encode_dc(MPVEncContext *const s, int diff, int component)
{
    unsigned int diff_u = diff + 255;
    if (diff_u >= 511) {
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
                     av_zero_extend(diff, index));
        else
            put_bits(&s->pb,
                     ff_mpeg12_vlc_dc_chroma_bits[index] + index,
                     (ff_mpeg12_vlc_dc_chroma_code[index] << index) +
                     av_zero_extend(diff, index));
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

static void mpeg1_encode_block(MPVEncContext *const s, const int16_t block[], int n)
{
    int alevel, level, last_non_zero, dc, diff, i, j, run, last_index, sign;
    int code, component;
    const uint16_t (*table_vlc)[2] = ff_mpeg1_vlc_table;

    last_index = s->c.block_last_index[n];

    /* DC coef */
    if (s->c.mb_intra) {
        component = (n <= 3 ? 0 : (n & 1) + 1);
        dc        = block[0];                   /* overflow is impossible */
        diff      = dc - s->c.last_dc[component];
        encode_dc(s, diff, component);
        s->c.last_dc[component] = dc;
        i = 1;
        if (s->c.intra_vlc_format)
            table_vlc = ff_mpeg2_vlc_table;
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
        j     = s->c.intra_scantable.permutated[i];
        level = block[j];

next_coef:
        /* encode using VLC */
        if (level != 0) {
            run = i - last_non_zero - 1;

            alevel = level;
            MASK_ABS(sign, alevel);
            sign &= 1;

            if (alevel <= mpeg12_max_level[run]) {
                code = mpeg12_index_run[run] + alevel - 1;
                /* store the VLC & sign at once */
                put_bits(&s->pb, table_vlc[code][1] + 1,
                         (table_vlc[code][0] << 1) + sign);
            } else {
                /* Escape seems to be pretty rare <5% so I do not optimize it.
                 * The following encodes run together with the common escape
                 * value of both tables 000001b. */
                put_bits(&s->pb, 6 + 6, 0x01 << 6 | run);
                /* escape: only clip in this case */
                if (s->c.codec_id == AV_CODEC_ID_MPEG1VIDEO) {
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

static av_always_inline void mpeg1_encode_mb_internal(MPVEncContext *const s,
                                                      const int16_t block[8][64],
                                                      int motion_x, int motion_y,
                                                      int mb_block_count,
                                                      int chroma_y_shift)
{
/* MPEG-1 is always 420. */
#define IS_MPEG1(s) (chroma_y_shift == 1 && (s)->c.codec_id == AV_CODEC_ID_MPEG1VIDEO)
    int i, cbp;
    const int mb_x     = s->c.mb_x;
    const int mb_y     = s->c.mb_y;
    const int first_mb = mb_x == s->c.resync_mb_x && mb_y == s->c.resync_mb_y;

    /* compute cbp */
    cbp = 0;
    for (i = 0; i < mb_block_count; i++)
        if (s->c.block_last_index[i] >= 0)
            cbp |= 1 << (mb_block_count - 1 - i);

    if (cbp == 0 && !first_mb && s->c.mv_type == MV_TYPE_16X16 &&
        (mb_x != s->c.mb_width - 1 ||
         (mb_y != s->c.end_mb_y - 1 && IS_MPEG1(s))) &&
        ((s->c.pict_type == AV_PICTURE_TYPE_P && (motion_x | motion_y) == 0) ||
         (s->c.pict_type == AV_PICTURE_TYPE_B && s->c.mv_dir == s->last_mv_dir &&
          (((s->c.mv_dir & MV_DIR_FORWARD)
            ? ((s->c.mv[0][0][0] - s->c.last_mv[0][0][0]) |
               (s->c.mv[0][0][1] - s->c.last_mv[0][0][1])) : 0) |
           ((s->c.mv_dir & MV_DIR_BACKWARD)
            ? ((s->c.mv[1][0][0] - s->c.last_mv[1][0][0]) |
               (s->c.mv[1][0][1] - s->c.last_mv[1][0][1])) : 0)) == 0))) {
        s->c.mb_skip_run++;
        s->c.qscale -= s->dquant;
        s->misc_bits++;
        s->last_bits++;
        if (s->c.pict_type == AV_PICTURE_TYPE_P) {
            s->c.last_mv[0][0][0] =
            s->c.last_mv[0][0][1] =
            s->c.last_mv[0][1][0] =
            s->c.last_mv[0][1][1] = 0;
        }
    } else {
        if (first_mb) {
            av_assert0(s->c.mb_skip_run == 0);
            encode_mb_skip_run(s, s->c.mb_x);
        } else {
            encode_mb_skip_run(s, s->c.mb_skip_run);
        }

        if (s->c.pict_type == AV_PICTURE_TYPE_I) {
            if (s->dquant && cbp) {
                /* macroblock_type: macroblock_quant = 1 */
                put_mb_modes(s, 2, 1, 0, 0);
                put_qscale(s);
            } else {
                /* macroblock_type: macroblock_quant = 0 */
                put_mb_modes(s, 1, 1, 0, 0);
                s->c.qscale -= s->dquant;
            }
            s->misc_bits += get_bits_diff(s);
            s->i_count++;
        } else if (s->c.mb_intra) {
            if (s->dquant && cbp) {
                put_mb_modes(s, 6, 0x01, 0, 0);
                put_qscale(s);
            } else {
                put_mb_modes(s, 5, 0x03, 0, 0);
                s->c.qscale -= s->dquant;
            }
            s->misc_bits += get_bits_diff(s);
            s->i_count++;
            memset(s->c.last_mv, 0, sizeof(s->c.last_mv));
        } else if (s->c.pict_type == AV_PICTURE_TYPE_P) {
            if (s->c.mv_type == MV_TYPE_16X16) {
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
                                            motion_x - s->c.last_mv[0][0][0],
                                            s->f_code);
                        // RAL: f_code parameter added
                        mpeg1_encode_motion(s,
                                            motion_y - s->c.last_mv[0][0][1],
                                            s->f_code);
                        s->mv_bits += get_bits_diff(s);
                    }
                } else {
                    put_bits(&s->pb, 3, 1);         /* motion only */
                    if (!s->c.frame_pred_frame_dct)
                        put_bits(&s->pb, 2, 2);     /* motion_type: frame */
                    s->misc_bits += get_bits_diff(s);
                    // RAL: f_code parameter added
                    mpeg1_encode_motion(s,
                                        motion_x - s->c.last_mv[0][0][0],
                                        s->f_code);
                    // RAL: f_code parameter added
                    mpeg1_encode_motion(s,
                                        motion_y - s->c.last_mv[0][0][1],
                                        s->f_code);
                    s->c.qscale  -= s->dquant;
                    s->mv_bits += get_bits_diff(s);
                }
                s->c.last_mv[0][1][0] = s->c.last_mv[0][0][0] = motion_x;
                s->c.last_mv[0][1][1] = s->c.last_mv[0][0][1] = motion_y;
            } else {
                av_assert2(!s->c.frame_pred_frame_dct && s->c.mv_type == MV_TYPE_FIELD);

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
                    s->c.qscale -= s->dquant;
                }
                s->misc_bits += get_bits_diff(s);
                for (i = 0; i < 2; i++) {
                    put_bits(&s->pb, 1, s->c.field_select[0][i]);
                    mpeg1_encode_motion(s,
                                        s->c.mv[0][i][0] - s->c.last_mv[0][i][0],
                                        s->f_code);
                    mpeg1_encode_motion(s,
                                        s->c.mv[0][i][1] - (s->c.last_mv[0][i][1] >> 1),
                                        s->f_code);
                    s->c.last_mv[0][i][0] = s->c.mv[0][i][0];
                    s->c.last_mv[0][i][1] = 2 * s->c.mv[0][i][1];
                }
                s->mv_bits += get_bits_diff(s);
            }
            if (cbp) {
                if (chroma_y_shift) {
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
        } else {
            if (s->c.mv_type == MV_TYPE_16X16) {
                if (cbp) {                      // With coded bloc pattern
                    if (s->dquant) {
                        if (s->c.mv_dir == MV_DIR_FORWARD)
                            put_mb_modes(s, 6, 3, 1, 0);
                        else
                            put_mb_modes(s, 8 - s->c.mv_dir, 2, 1, 0);
                        put_qscale(s);
                    } else {
                        put_mb_modes(s, 5 - s->c.mv_dir, 3, 1, 0);
                    }
                } else {                        // No coded bloc pattern
                    put_bits(&s->pb, 5 - s->c.mv_dir, 2);
                    if (!s->c.frame_pred_frame_dct)
                        put_bits(&s->pb, 2, 2); /* motion_type: frame */
                    s->c.qscale -= s->dquant;
                }
                s->misc_bits += get_bits_diff(s);
                if (s->c.mv_dir & MV_DIR_FORWARD) {
                    mpeg1_encode_motion(s,
                                        s->c.mv[0][0][0] - s->c.last_mv[0][0][0],
                                        s->f_code);
                    mpeg1_encode_motion(s,
                                        s->c.mv[0][0][1] - s->c.last_mv[0][0][1],
                                        s->f_code);
                    s->c.last_mv[0][0][0] =
                    s->c.last_mv[0][1][0] = s->c.mv[0][0][0];
                    s->c.last_mv[0][0][1] =
                    s->c.last_mv[0][1][1] = s->c.mv[0][0][1];
                }
                if (s->c.mv_dir & MV_DIR_BACKWARD) {
                    mpeg1_encode_motion(s,
                                        s->c.mv[1][0][0] - s->c.last_mv[1][0][0],
                                        s->b_code);
                    mpeg1_encode_motion(s,
                                        s->c.mv[1][0][1] - s->c.last_mv[1][0][1],
                                        s->b_code);
                    s->c.last_mv[1][0][0] =
                    s->c.last_mv[1][1][0] = s->c.mv[1][0][0];
                    s->c.last_mv[1][0][1] =
                    s->c.last_mv[1][1][1] = s->c.mv[1][0][1];
                }
            } else {
                av_assert2(s->c.mv_type == MV_TYPE_FIELD);
                av_assert2(!s->c.frame_pred_frame_dct);
                if (cbp) {                      // With coded bloc pattern
                    if (s->dquant) {
                        if (s->c.mv_dir == MV_DIR_FORWARD)
                            put_mb_modes(s, 6, 3, 1, 1);
                        else
                            put_mb_modes(s, 8 - s->c.mv_dir, 2, 1, 1);
                        put_qscale(s);
                    } else {
                        put_mb_modes(s, 5 - s->c.mv_dir, 3, 1, 1);
                    }
                } else {                        // No coded bloc pattern
                    put_bits(&s->pb, 5 - s->c.mv_dir, 2);
                    put_bits(&s->pb, 2, 1);     /* motion_type: field */
                    s->c.qscale -= s->dquant;
                }
                s->misc_bits += get_bits_diff(s);
                if (s->c.mv_dir & MV_DIR_FORWARD) {
                    for (i = 0; i < 2; i++) {
                        put_bits(&s->pb, 1, s->c.field_select[0][i]);
                        mpeg1_encode_motion(s,
                                            s->c.mv[0][i][0] - s->c.last_mv[0][i][0],
                                            s->f_code);
                        mpeg1_encode_motion(s,
                                            s->c.mv[0][i][1] - (s->c.last_mv[0][i][1] >> 1),
                                            s->f_code);
                        s->c.last_mv[0][i][0] = s->c.mv[0][i][0];
                        s->c.last_mv[0][i][1] = s->c.mv[0][i][1] * 2;
                    }
                }
                if (s->c.mv_dir & MV_DIR_BACKWARD) {
                    for (i = 0; i < 2; i++) {
                        put_bits(&s->pb, 1, s->c.field_select[1][i]);
                        mpeg1_encode_motion(s,
                                            s->c.mv[1][i][0] - s->c.last_mv[1][i][0],
                                            s->b_code);
                        mpeg1_encode_motion(s,
                                            s->c.mv[1][i][1] - (s->c.last_mv[1][i][1] >> 1),
                                            s->b_code);
                        s->c.last_mv[1][i][0] = s->c.mv[1][i][0];
                        s->c.last_mv[1][i][1] = s->c.mv[1][i][1] * 2;
                    }
                }
            }
            s->mv_bits += get_bits_diff(s);
            if (cbp) {
                if (chroma_y_shift) {
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
        s->c.mb_skip_run = 0;
        if (s->c.mb_intra)
            s->i_tex_bits += get_bits_diff(s);
        else
            s->p_tex_bits += get_bits_diff(s);
    }
}

static void mpeg12_encode_mb(MPVEncContext *const s, int16_t block[][64],
                             int motion_x, int motion_y)
{
    if (!s->c.mb_intra)
        s->c.last_dc[0] = s->c.last_dc[1] = s->c.last_dc[2] = 128 << s->c.intra_dc_precision;
    if (s->c.chroma_format == CHROMA_420)
        mpeg1_encode_mb_internal(s, block, motion_x, motion_y, 6, 1);
    else
        mpeg1_encode_mb_internal(s, block, motion_x, motion_y, 8, 0);
}

static av_cold void mpeg12_encode_init_static(void)
{
    ff_rl_init_level_run(mpeg12_max_level, mpeg12_index_run,
                         ff_mpeg12_run, ff_mpeg12_level, MPEG12_RL_NB_ELEMS);

    ff_mpeg1_init_uni_ac_vlc(mpeg12_max_level, mpeg12_index_run,
                             ff_mpeg1_vlc_table, uni_mpeg1_ac_vlc_len);
    ff_mpeg1_init_uni_ac_vlc(mpeg12_max_level, mpeg12_index_run,
                             ff_mpeg2_vlc_table, uni_mpeg2_ac_vlc_len);

    /* build unified dc encoding tables */
    for (int i = -255; i < 256; i++) {
        int adiff, index;
        int bits, code;
        int diff = i;

        adiff = FFABS(diff);
        if (diff < 0)
            diff--;
        index = av_log2(2 * adiff);

        bits = ff_mpeg12_vlc_dc_lum_bits[index] + index;
        code = (ff_mpeg12_vlc_dc_lum_code[index] << index) +
               av_zero_extend(diff, index);
        mpeg1_lum_dc_uni[i + 255] = bits + (code << 8);

        bits = ff_mpeg12_vlc_dc_chroma_bits[index] + index;
        code = (ff_mpeg12_vlc_dc_chroma_code[index] << index) +
               av_zero_extend(diff, index);
        mpeg1_chr_dc_uni[i + 255] = bits + (code << 8);
    }

    for (int f_code = 1; f_code <= MAX_FCODE; f_code++)
        for (int mv = -MAX_DMV; mv <= MAX_DMV; mv++) {
            int len;

            if (mv == 0) {
                len = 1; /* ff_mpeg12_mbMotionVectorTable[0][1] */
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
                    len = 10 /* ff_mpeg12_mbMotionVectorTable[16][1] */ +
                          2 + bit_size;
            }

            mv_penalty[f_code][mv + MAX_DMV] = len;
        }


    for (int f_code = MAX_FCODE; f_code > 0; f_code--)
        for (int mv = -(8 << f_code); mv < (8 << f_code); mv++)
            fcode_tab[mv + MAX_MV] = f_code;
}

static av_cold int find_frame_rate_index(AVCodecContext *avctx, MPEG12EncContext *mpeg12)
{
    AVRational bestq = (AVRational) {0, 0};
    AVRational ext;
    AVRational target = av_inv_q(avctx->time_base);

    for (int i = 1; i < 14; i++) {
        if (avctx->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL &&
            i >= 9)
            break;

        for (ext.num = 1; ext.num <= 4; ext.num++) {
            for (ext.den = 1; ext.den <= 32; ext.den++) {
                AVRational q = av_mul_q(ext, ff_mpeg12_frame_rate_tab[i]);

                if (avctx->codec_id != AV_CODEC_ID_MPEG2VIDEO && (ext.den!=1 || ext.num!=1))
                    continue;
                if (av_gcd(ext.den, ext.num) != 1)
                    continue;

                if (    bestq.num==0
                    || av_nearer_q(target, bestq, q) < 0
                    || ext.num==1 && ext.den==1 && av_nearer_q(target, bestq, q) == 0) {
                    bestq               = q;
                    mpeg12->frame_rate_index   = i;
                    mpeg12->frame_rate_ext.num = ext.num;
                    mpeg12->frame_rate_ext.den = ext.den;
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
    static AVOnce init_static_once = AV_ONCE_INIT;
    MPEG12EncContext *const mpeg12 = avctx->priv_data;
    MPVMainEncContext *const m = &mpeg12->mpeg;
    MPVEncContext     *const s = &m->s;
    int ret;
    int max_size = avctx->codec_id == AV_CODEC_ID_MPEG2VIDEO ? 16383 : 4095;

    if (avctx->width > max_size || avctx->height > max_size) {
        av_log(avctx, AV_LOG_ERROR, "%s does not support resolutions above %dx%d\n",
               CONFIG_SMALL ? avctx->codec->name : avctx->codec->long_name,
               max_size, max_size);
        return AVERROR(EINVAL);
    }
    if ((avctx->width & 0xFFF) == 0 && (avctx->height & 0xFFF) == 1) {
        av_log(avctx, AV_LOG_ERROR, "Width / Height is invalid for MPEG2\n");
        return AVERROR(EINVAL);
    }

    if (avctx->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL) {
        if ((avctx->width & 0xFFF) == 0 || (avctx->height & 0xFFF) == 0) {
            av_log(avctx, AV_LOG_ERROR, "Width or Height are not allowed to be multiples of 4096\n"
                                        "add '-strict %d' if you want to use them anyway.\n", FF_COMPLIANCE_UNOFFICIAL);
            return AVERROR(EINVAL);
        }
    }

    if (s->c.q_scale_type == 1) {
        if (avctx->qmax > 28) {
            av_log(avctx, AV_LOG_ERROR,
                   "non linear quant only supports qmax <= 28 currently\n");
            return AVERROR_PATCHWELCOME;
        }
    }

    if (avctx->profile == AV_PROFILE_UNKNOWN) {
        if (avctx->level != AV_LEVEL_UNKNOWN) {
            av_log(avctx, AV_LOG_ERROR, "Set profile and level\n");
            return AVERROR(EINVAL);
        }
        /* Main or 4:2:2 */
        avctx->profile = avctx->pix_fmt == AV_PIX_FMT_YUV420P ? AV_PROFILE_MPEG2_MAIN
                                                              : AV_PROFILE_MPEG2_422;
    }
    if (avctx->level == AV_LEVEL_UNKNOWN) {
        if (avctx->profile == AV_PROFILE_MPEG2_422) {   /* 4:2:2 */
            if (avctx->width <= 720 && avctx->height <= 608)
                avctx->level = 5;                   /* Main */
            else
                avctx->level = 2;                   /* High */
        } else {
            if (avctx->profile != AV_PROFILE_MPEG2_HIGH &&
                avctx->pix_fmt != AV_PIX_FMT_YUV420P) {
                av_log(avctx, AV_LOG_ERROR,
                       "Only High(1) and 4:2:2(0) profiles support 4:2:2 color sampling\n");
                return AVERROR(EINVAL);
            }
            if (avctx->width <= 720 && avctx->height <= 576)
                avctx->level = 8;                   /* Main */
            else if (avctx->width <= 1440)
                avctx->level = 6;                   /* High 1440 */
            else
                avctx->level = 4;                   /* High */
        }
    }

    m->encode_picture_header = mpeg1_encode_picture_header;
    s->encode_mb             = mpeg12_encode_mb;

    s->me.mv_penalty = mv_penalty;
    m->fcode_tab     = fcode_tab + MAX_MV;
    if (avctx->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
        s->min_qcoeff = -255;
        s->max_qcoeff = 255;
    } else {
        s->min_qcoeff = -2047;
        s->max_qcoeff = 2047;
        s->mpeg_quant = 1;
    }
    if (s->c.intra_vlc_format) {
        s->intra_ac_vlc_length      =
        s->intra_ac_vlc_last_length = uni_mpeg2_ac_vlc_len;
    } else {
        s->intra_ac_vlc_length      =
        s->intra_ac_vlc_last_length = uni_mpeg1_ac_vlc_len;
    }
    s->inter_ac_vlc_length      =
    s->inter_ac_vlc_last_length = uni_mpeg1_ac_vlc_len;

    ret = ff_mpv_encode_init(avctx);
    if (ret < 0)
        return ret;

    if (avctx->codec_id == AV_CODEC_ID_MPEG1VIDEO &&
        s->c.thread_context[s->c.slice_context_count - 1]->start_mb_y >
            SLICE_MAX_START_CODE - SLICE_MIN_START_CODE) {
        // MPEG-1 slices must not start at a MB row number that would make
        // their start code > SLICE_MAX_START_CODE. So make the last slice
        // bigger if needed and evenly distribute the first 174 rows.
        static_assert(MAX_THREADS <= 1 + SLICE_MAX_START_CODE - SLICE_MIN_START_CODE,
                      "With more than 175 slice contexts, we have to handle "
                      "the case in which there is no work to do for some "
                      "slice contexts.");
        const int mb_height = SLICE_MAX_START_CODE - SLICE_MIN_START_CODE;
        const int nb_slices = s->c.slice_context_count - 1;

        s->c.thread_context[nb_slices]->start_mb_y = mb_height;

        av_assert1(nb_slices >= 1);
        for (int i = 0; i < nb_slices; i++) {
            s->c.thread_context[i]->start_mb_y =
                (mb_height * (i    ) + nb_slices / 2) / nb_slices;
            s->c.thread_context[i]->end_mb_y   =
                (mb_height * (i + 1) + nb_slices / 2) / nb_slices;
        }
    }

    if (find_frame_rate_index(avctx, mpeg12) < 0) {
        if (avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
            av_log(avctx, AV_LOG_ERROR, "MPEG-1/2 does not support %d/%d fps\n",
                   avctx->time_base.den, avctx->time_base.num);
            return AVERROR(EINVAL);
        } else {
            av_log(avctx, AV_LOG_INFO,
                   "MPEG-1/2 does not support %d/%d fps, there may be AV sync issues\n",
                   avctx->time_base.den, avctx->time_base.num);
        }
    }

    if (avctx->rc_max_rate &&
        avctx->rc_min_rate == avctx->rc_max_rate &&
        90000LL * (avctx->rc_buffer_size - 1) >
            avctx->rc_max_rate * 0xFFFFLL) {
        av_log(avctx, AV_LOG_INFO,
               "Warning vbv_delay will be set to 0xFFFF (=VBR) as the "
               "specified vbv buffer is too large for the given bitrate!\n");
    }

    if (mpeg12->drop_frame_timecode)
        mpeg12->tc.flags |= AV_TIMECODE_FLAG_DROPFRAME;
    if (mpeg12->drop_frame_timecode && mpeg12->frame_rate_index != 4) {
        av_log(avctx, AV_LOG_ERROR,
               "Drop frame time code only allowed with 1001/30000 fps\n");
        return AVERROR(EINVAL);
    }

    if (mpeg12->tc_opt_str) {
        AVRational rate = ff_mpeg12_frame_rate_tab[mpeg12->frame_rate_index];
        int ret = av_timecode_init_from_string(&mpeg12->tc, rate, mpeg12->tc_opt_str, avctx);
        if (ret < 0)
            return ret;
        mpeg12->drop_frame_timecode  = !!(mpeg12->tc.flags & AV_TIMECODE_FLAG_DROPFRAME);
        mpeg12->timecode_frame_start = mpeg12->tc.start;
    } else {
        mpeg12->timecode_frame_start = 0; // default is -1
    }

    ff_thread_once(&init_static_once, mpeg12_encode_init_static);

    return 0;
}

#define OFFSET(x) offsetof(MPEG12EncContext, x)
#define VE AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
#define COMMON_OPTS                                                           \
    { "gop_timecode",        "MPEG GOP Timecode in hh:mm:ss[:;.]ff format. Overrides timecode_frame_start.",   \
      OFFSET(tc_opt_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, VE },\
    { "drop_frame_timecode", "Timecode is in drop frame format.",             \
      OFFSET(drop_frame_timecode), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE }, \
    { "scan_offset",         "Reserve space for SVCD scan offset user data.", \
      OFFSET(scan_offset),         AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE }, \
    { "timecode_frame_start", "GOP timecode frame start number, in non-drop-frame format", \
      OFFSET(timecode_frame_start), AV_OPT_TYPE_INT64, {.i64 = -1 }, -1, INT64_MAX, VE}, \
    FF_MPV_COMMON_BFRAME_OPTS

static const AVOption mpeg1_options[] = {
    COMMON_OPTS
    FF_MPV_COMMON_OPTS
    FF_MPV_COMMON_MOTION_EST_OPTS
    { NULL },
};

static const AVOption mpeg2_options[] = {
    COMMON_OPTS
    { "intra_vlc",        "Use MPEG-2 intra VLC table.",
      FF_MPV_OFFSET(c.intra_vlc_format),    AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "non_linear_quant", "Use nonlinear quantizer.",    FF_MPV_OFFSET(c.q_scale_type),   AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "alternate_scan",   "Enable alternate scantable.", FF_MPV_OFFSET(c.alternate_scan), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "a53cc", "Use A53 Closed Captions (if available)", OFFSET(a53_cc),         AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, VE },
    { "seq_disp_ext",     "Write sequence_display_extension blocks.", OFFSET(seq_disp_ext), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 1, VE, .unit = "seq_disp_ext" },
    {     "auto",   NULL, 0, AV_OPT_TYPE_CONST,  {.i64 = -1},  0, 0, VE, .unit = "seq_disp_ext" },
    {     "never",  NULL, 0, AV_OPT_TYPE_CONST,  {.i64 = 0 },  0, 0, VE, .unit = "seq_disp_ext" },
    {     "always", NULL, 0, AV_OPT_TYPE_CONST,  {.i64 = 1 },  0, 0, VE, .unit = "seq_disp_ext" },
    { "video_format",     "Video_format in the sequence_display_extension indicating the source of the video.", OFFSET(video_format), AV_OPT_TYPE_INT, { .i64 = VIDEO_FORMAT_UNSPECIFIED }, 0, 7, VE, .unit = "video_format" },
    {     "component",    NULL, 0, AV_OPT_TYPE_CONST,  {.i64 = VIDEO_FORMAT_COMPONENT  },  0, 0, VE, .unit = "video_format" },
    {     "pal",          NULL, 0, AV_OPT_TYPE_CONST,  {.i64 = VIDEO_FORMAT_PAL        },  0, 0, VE, .unit = "video_format" },
    {     "ntsc",         NULL, 0, AV_OPT_TYPE_CONST,  {.i64 = VIDEO_FORMAT_NTSC       },  0, 0, VE, .unit = "video_format" },
    {     "secam",        NULL, 0, AV_OPT_TYPE_CONST,  {.i64 = VIDEO_FORMAT_SECAM      },  0, 0, VE, .unit = "video_format" },
    {     "mac",          NULL, 0, AV_OPT_TYPE_CONST,  {.i64 = VIDEO_FORMAT_MAC        },  0, 0, VE, .unit = "video_format" },
    {     "unspecified",  NULL, 0, AV_OPT_TYPE_CONST,  {.i64 = VIDEO_FORMAT_UNSPECIFIED},  0, 0, VE, .unit = "video_format" },
#define LEVEL(name, value) name, NULL, 0, AV_OPT_TYPE_CONST, { .i64 = value }, 0, 0, VE, .unit = "avctx.level"
    { LEVEL("high",     4) },
    { LEVEL("high1440", 6) },
    { LEVEL("main",     8) },
    { LEVEL("low",     10) },
#undef LEVEL
    FF_MPV_COMMON_OPTS
    FF_MPV_COMMON_MOTION_EST_OPTS
    FF_MPEG2_PROFILE_OPTS
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

const FFCodec ff_mpeg1video_encoder = {
    .p.name               = "mpeg1video",
    CODEC_LONG_NAME("MPEG-1 video"),
    .p.type               = AVMEDIA_TYPE_VIDEO,
    .p.id                 = AV_CODEC_ID_MPEG1VIDEO,
    .priv_data_size       = sizeof(MPEG12EncContext),
    .init                 = encode_init,
    FF_CODEC_ENCODE_CB(ff_mpv_encode_picture),
    .close                = ff_mpv_encode_end,
    CODEC_FRAMERATES_ARRAY(ff_mpeg12_frame_rate_tab + 1),
    CODEC_PIXFMTS(AV_PIX_FMT_YUV420P),
    .color_ranges         = AVCOL_RANGE_MPEG,
    .p.capabilities       = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                            AV_CODEC_CAP_SLICE_THREADS |
                            AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal        = FF_CODEC_CAP_INIT_CLEANUP,
    .p.priv_class         = &mpeg1_class,
};

const FFCodec ff_mpeg2video_encoder = {
    .p.name               = "mpeg2video",
    CODEC_LONG_NAME("MPEG-2 video"),
    .p.type               = AVMEDIA_TYPE_VIDEO,
    .p.id                 = AV_CODEC_ID_MPEG2VIDEO,
    .priv_data_size       = sizeof(MPEG12EncContext),
    .init                 = encode_init,
    FF_CODEC_ENCODE_CB(ff_mpv_encode_picture),
    .close                = ff_mpv_encode_end,
    CODEC_FRAMERATES_ARRAY(ff_mpeg2_frame_rate_tab),
    CODEC_PIXFMTS(AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P),
    .color_ranges         = AVCOL_RANGE_MPEG,
    .p.capabilities       = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                            AV_CODEC_CAP_SLICE_THREADS |
                            AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal        = FF_CODEC_CAP_INIT_CLEANUP,
    .p.priv_class         = &mpeg2_class,
};
#endif /* CONFIG_MPEG1VIDEO_ENCODER || CONFIG_MPEG2VIDEO_ENCODER */
