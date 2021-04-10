/*
 * RV10/RV20 decoder
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer
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
 * RV10/RV20 decoder
 */

#include <inttypes.h>

#include "libavutil/imgutils.h"
#include "libavutil/thread.h"

#include "avcodec.h"
#include "error_resilience.h"
#include "h263.h"
#include "h263data.h"
#include "internal.h"
#include "mpeg_er.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "mpeg4video.h"
#include "mpegvideodata.h"
#include "rv10.h"

#define RV_GET_MAJOR_VER(x)  ((x) >> 28)
#define RV_GET_MINOR_VER(x) (((x) >> 20) & 0xFF)
#define RV_GET_MICRO_VER(x) (((x) >> 12) & 0xFF)

#define MAX_VLC_ENTRIES 1023 // Note: Does not include the skip entries.
#define DC_VLC_BITS        9

typedef struct RVDecContext {
    MpegEncContext m;
    int sub_id;
    int orig_width, orig_height;
} RVDecContext;

/* (run, length) encoded value for the symbols table. The actual symbols
 * are run..run - length (mod 256).
 * The last two entries in the following table apply to luma only.
 * The skip values are not included in this list. */
static const uint8_t rv_sym_run_len[][2] = {
    {   0,   0 }, {   1,   0 }, { 255,   0 }, {   3,   1 }, { 254,  1 },
    {   7,   3 }, { 252,   3 }, {  15,   7 }, { 248,   7 }, {  31, 15 },
    { 240,  15 }, {  63,  31 }, { 224,  31 }, { 127,  63 }, { 192, 63 },
    { 255, 127 }, { 128, 127 }, { 127, 255 }, { 128, 255 },
};

/* entry[i] of the following tables gives
 * the number of VLC codes of length i + 2. */
static const uint16_t rv_lum_len_count[15] = {
    1,  0,  2,  4,  8, 16, 32,  0, 64,  0, 128,  0, 256,  0, 512,
};

static const uint16_t rv_chrom_len_count[15] = {
    1,  2,  4,  0,  8,  0, 16,  0, 32,  0,  64,  0, 128,  0, 256,
};

static VLC rv_dc_lum, rv_dc_chrom;

int ff_rv_decode_dc(MpegEncContext *s, int n)
{
    int code;

    if (n < 4) {
        code = get_vlc2(&s->gb, rv_dc_lum.table, DC_VLC_BITS, 2);
    } else {
        code = get_vlc2(&s->gb, rv_dc_chrom.table, DC_VLC_BITS, 2);
        if (code < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "chroma dc error\n");
            return -1;
        }
    }
    return code;
}

/* read RV 1.0 compatible frame header */
static int rv10_decode_picture_header(MpegEncContext *s)
{
    int mb_count, pb_frame, marker, mb_xy;

    marker = get_bits1(&s->gb);

    if (get_bits1(&s->gb))
        s->pict_type = AV_PICTURE_TYPE_P;
    else
        s->pict_type = AV_PICTURE_TYPE_I;

    if (!marker)
        av_log(s->avctx, AV_LOG_ERROR, "marker missing\n");

    pb_frame = get_bits1(&s->gb);

    ff_dlog(s->avctx, "pict_type=%d pb_frame=%d\n", s->pict_type, pb_frame);

    if (pb_frame) {
        avpriv_request_sample(s->avctx, "PB-frame");
        return AVERROR_PATCHWELCOME;
    }

    s->qscale = get_bits(&s->gb, 5);
    if (s->qscale == 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid qscale value: 0\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->pict_type == AV_PICTURE_TYPE_I) {
        if (s->rv10_version == 3) {
            /* specific MPEG like DC coding not used */
            s->last_dc[0] = get_bits(&s->gb, 8);
            s->last_dc[1] = get_bits(&s->gb, 8);
            s->last_dc[2] = get_bits(&s->gb, 8);
            ff_dlog(s->avctx, "DC:%d %d %d\n", s->last_dc[0],
                    s->last_dc[1], s->last_dc[2]);
        }
    }
    /* if multiple packets per frame are sent, the position at which
     * to display the macroblocks is coded here */

    mb_xy = s->mb_x + s->mb_y * s->mb_width;
    if (show_bits(&s->gb, 12) == 0 || (mb_xy && mb_xy < s->mb_num)) {
        s->mb_x  = get_bits(&s->gb, 6); /* mb_x */
        s->mb_y  = get_bits(&s->gb, 6); /* mb_y */
        mb_count = get_bits(&s->gb, 12);
    } else {
        s->mb_x  = 0;
        s->mb_y  = 0;
        mb_count = s->mb_width * s->mb_height;
    }
    skip_bits(&s->gb, 3);   /* ignored */
    s->f_code          = 1;
    s->unrestricted_mv = 1;

    return mb_count;
}

static int rv20_decode_picture_header(RVDecContext *rv, int whole_size)
{
    MpegEncContext *s = &rv->m;
    int seq, mb_pos, i, ret;
    int rpr_max;

    i = get_bits(&s->gb, 2);
    switch (i) {
    case 0:
        s->pict_type = AV_PICTURE_TYPE_I;
        break;
    case 1:
        s->pict_type = AV_PICTURE_TYPE_I;
        break;                                  // hmm ...
    case 2:
        s->pict_type = AV_PICTURE_TYPE_P;
        break;
    case 3:
        s->pict_type = AV_PICTURE_TYPE_B;
        break;
    default:
        av_log(s->avctx, AV_LOG_ERROR, "unknown frame type\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->low_delay && s->pict_type == AV_PICTURE_TYPE_B) {
        av_log(s->avctx, AV_LOG_ERROR, "low delay B\n");
        return -1;
    }
    if (!s->last_picture_ptr && s->pict_type == AV_PICTURE_TYPE_B) {
        av_log(s->avctx, AV_LOG_ERROR, "early B-frame\n");
        return AVERROR_INVALIDDATA;
    }

    if (get_bits1(&s->gb)) {
        av_log(s->avctx, AV_LOG_ERROR, "reserved bit set\n");
        return AVERROR_INVALIDDATA;
    }

    s->qscale = get_bits(&s->gb, 5);
    if (s->qscale == 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid qscale value: 0\n");
        return AVERROR_INVALIDDATA;
    }

    if (RV_GET_MINOR_VER(rv->sub_id) >= 2)
        s->loop_filter = get_bits1(&s->gb) && !s->avctx->lowres;

    if (RV_GET_MINOR_VER(rv->sub_id) <= 1)
        seq = get_bits(&s->gb, 8) << 7;
    else
        seq = get_bits(&s->gb, 13) << 2;

    rpr_max = s->avctx->extradata[1] & 7;
    if (rpr_max) {
        int f, new_w, new_h;
        int rpr_bits = av_log2(rpr_max) + 1;

        f = get_bits(&s->gb, rpr_bits);

        if (f) {
            if (s->avctx->extradata_size < 8 + 2 * f) {
                av_log(s->avctx, AV_LOG_ERROR, "Extradata too small.\n");
                return AVERROR_INVALIDDATA;
            }

            new_w = 4 * ((uint8_t *) s->avctx->extradata)[6 + 2 * f];
            new_h = 4 * ((uint8_t *) s->avctx->extradata)[7 + 2 * f];
        } else {
            new_w = rv->orig_width;
            new_h = rv->orig_height;
        }
        if (new_w != s->width || new_h != s->height || !s->context_initialized) {
            AVRational old_aspect = s->avctx->sample_aspect_ratio;
            av_log(s->avctx, AV_LOG_DEBUG,
                   "attempting to change resolution to %dx%d\n", new_w, new_h);
            if (av_image_check_size(new_w, new_h, 0, s->avctx) < 0)
                return AVERROR_INVALIDDATA;

            if (whole_size < (new_w + 15)/16 * ((new_h + 15)/16) / 8)
                return AVERROR_INVALIDDATA;

            ff_mpv_common_end(s);

            // attempt to keep aspect during typical resolution switches
            if (!old_aspect.num)
                old_aspect = (AVRational){1, 1};
            if (2 * (int64_t)new_w * s->height == (int64_t)new_h * s->width)
                s->avctx->sample_aspect_ratio = av_mul_q(old_aspect, (AVRational){2, 1});
            if ((int64_t)new_w * s->height == 2 * (int64_t)new_h * s->width)
                s->avctx->sample_aspect_ratio = av_mul_q(old_aspect, (AVRational){1, 2});

            ret = ff_set_dimensions(s->avctx, new_w, new_h);
            if (ret < 0)
                return ret;

            s->width  = new_w;
            s->height = new_h;
            if ((ret = ff_mpv_common_init(s)) < 0)
                return ret;
        }

        if (s->avctx->debug & FF_DEBUG_PICT_INFO) {
            av_log(s->avctx, AV_LOG_DEBUG, "F %d/%d/%d\n", f, rpr_bits, rpr_max);
        }
    }
    if (av_image_check_size(s->width, s->height, 0, s->avctx) < 0)
        return AVERROR_INVALIDDATA;

    mb_pos = ff_h263_decode_mba(s);

    seq |= s->time & ~0x7FFF;
    if (seq - s->time >  0x4000)
        seq -= 0x8000;
    if (seq - s->time < -0x4000)
        seq += 0x8000;

    if (seq != s->time) {
        if (s->pict_type != AV_PICTURE_TYPE_B) {
            s->time            = seq;
            s->pp_time         = s->time - s->last_non_b_time;
            s->last_non_b_time = s->time;
        } else {
            s->time    = seq;
            s->pb_time = s->pp_time - (s->last_non_b_time - s->time);
        }
    }
    if (s->pict_type == AV_PICTURE_TYPE_B) {
        if (s->pp_time <=s->pb_time || s->pp_time <= s->pp_time - s->pb_time || s->pp_time<=0) {
            av_log(s->avctx, AV_LOG_DEBUG,
                   "messed up order, possible from seeking? skipping current B-frame\n");
#define ERROR_SKIP_FRAME -123
            return ERROR_SKIP_FRAME;
        }
        ff_mpeg4_init_direct_mv(s);
    }

    s->no_rounding = get_bits1(&s->gb);

    if (RV_GET_MINOR_VER(rv->sub_id) <= 1 && s->pict_type == AV_PICTURE_TYPE_B)
        // binary decoder reads 3+2 bits here but they don't seem to be used
        skip_bits(&s->gb, 5);

    s->f_code          = 1;
    s->unrestricted_mv = 1;
    s->h263_aic        = s->pict_type == AV_PICTURE_TYPE_I;
    s->modified_quant  = 1;
    if (!s->avctx->lowres)
        s->loop_filter = 1;

    if (s->avctx->debug & FF_DEBUG_PICT_INFO) {
        av_log(s->avctx, AV_LOG_INFO,
               "num:%5d x:%2d y:%2d type:%d qscale:%2d rnd:%d\n",
               seq, s->mb_x, s->mb_y, s->pict_type, s->qscale,
               s->no_rounding);
    }

    av_assert0(s->pict_type != AV_PICTURE_TYPE_B || !s->low_delay);

    return s->mb_width * s->mb_height - mb_pos;
}

static av_cold void rv10_build_vlc(VLC *vlc, const uint16_t len_count[15],
                                   const uint8_t sym_rl[][2], int sym_rl_elems)
{
    uint16_t syms[MAX_VLC_ENTRIES];
    uint8_t  lens[MAX_VLC_ENTRIES];
    unsigned nb_syms = 0, nb_lens = 0;

    for (unsigned i = 0; i < sym_rl_elems; i++) {
        unsigned cur_sym = sym_rl[i][0];
        for (unsigned tmp = nb_syms + sym_rl[i][1]; nb_syms <= tmp; nb_syms++)
            syms[nb_syms] = 0xFF & cur_sym--;
    }

    for (unsigned i = 0; i < 15; i++)
        for (unsigned tmp = nb_lens + len_count[i]; nb_lens < tmp; nb_lens++)
            lens[nb_lens] = i + 2;
    av_assert1(nb_lens == nb_syms);
    ff_init_vlc_from_lengths(vlc, DC_VLC_BITS, nb_lens, lens, 1,
                             syms, 2, 2, 0, INIT_VLC_STATIC_OVERLONG, NULL);
}

static av_cold void rv10_init_static(void)
{
    static VLC_TYPE table[1472 + 992][2];

    rv_dc_lum.table             = table;
    rv_dc_lum.table_allocated   = 1472;
    rv10_build_vlc(&rv_dc_lum, rv_lum_len_count,
                   rv_sym_run_len, FF_ARRAY_ELEMS(rv_sym_run_len));
    for (int i = 0; i < 1 << (DC_VLC_BITS - 7 /* Length of skip prefix */); i++) {
        /* All codes beginning with 0x7F have the same length and value.
         * Modifying the table directly saves us the useless subtables. */
        rv_dc_lum.table[(0x7F << (DC_VLC_BITS - 7)) + i][0] = 255;
        rv_dc_lum.table[(0x7F << (DC_VLC_BITS - 7)) + i][1] = 18;
    }
    rv_dc_chrom.table           = &table[1472];
    rv_dc_chrom.table_allocated = 992;
    rv10_build_vlc(&rv_dc_chrom, rv_chrom_len_count,
                   rv_sym_run_len, FF_ARRAY_ELEMS(rv_sym_run_len) - 2);
    for (int i = 0; i < 1 << (DC_VLC_BITS - 9 /* Length of skip prefix */); i++) {
        /* Same as above. */
        rv_dc_chrom.table[(0x1FE << (DC_VLC_BITS - 9)) + i][0] = 255;
        rv_dc_chrom.table[(0x1FE << (DC_VLC_BITS - 9)) + i][1] = 18;
    }
    ff_h263_decode_init_vlc();
}

static av_cold int rv10_decode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    RVDecContext *rv = avctx->priv_data;
    MpegEncContext *s = &rv->m;
    int major_ver, minor_ver, micro_ver, ret;

    if (avctx->extradata_size < 8) {
        av_log(avctx, AV_LOG_ERROR, "Extradata is too small.\n");
        return AVERROR_INVALIDDATA;
    }
    if ((ret = av_image_check_size(avctx->coded_width,
                                   avctx->coded_height, 0, avctx)) < 0)
        return ret;

    ff_mpv_decode_init(s, avctx);

    s->out_format  = FMT_H263;

    rv->orig_width  =
    s->width        = avctx->coded_width;
    rv->orig_height =
    s->height       = avctx->coded_height;

    s->h263_long_vectors = ((uint8_t *) avctx->extradata)[3] & 1;
    rv->sub_id           = AV_RB32((uint8_t *) avctx->extradata + 4);

    major_ver = RV_GET_MAJOR_VER(rv->sub_id);
    minor_ver = RV_GET_MINOR_VER(rv->sub_id);
    micro_ver = RV_GET_MICRO_VER(rv->sub_id);

    s->low_delay = 1;
    switch (major_ver) {
    case 1:
        s->rv10_version = micro_ver ? 3 : 1;
        s->obmc         = micro_ver == 2;
        break;
    case 2:
        if (minor_ver >= 2) {
            s->low_delay           = 0;
            s->avctx->has_b_frames = 1;
        }
        break;
    default:
        av_log(s->avctx, AV_LOG_ERROR, "unknown header %X\n", rv->sub_id);
        avpriv_request_sample(avctx, "RV1/2 version");
        return AVERROR_PATCHWELCOME;
    }

    if (avctx->debug & FF_DEBUG_PICT_INFO) {
        av_log(avctx, AV_LOG_DEBUG, "ver:%X ver0:%"PRIX32"\n", rv->sub_id,
               ((uint32_t *) avctx->extradata)[0]);
    }

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    ff_mpv_idct_init(s);
    if ((ret = ff_mpv_common_init(s)) < 0)
        return ret;

    ff_h263dsp_init(&s->h263dsp);

    /* init static VLCs */
    ff_thread_once(&init_static_once, rv10_init_static);

    return 0;
}

static av_cold int rv10_decode_end(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;

    ff_mpv_common_end(s);
    return 0;
}

static int rv10_decode_packet(AVCodecContext *avctx, const uint8_t *buf,
                              int buf_size, int buf_size2, int whole_size)
{
    RVDecContext *rv = avctx->priv_data;
    MpegEncContext *s = &rv->m;
    int mb_count, mb_pos, left, start_mb_x, active_bits_size, ret;

    active_bits_size = buf_size * 8;
    init_get_bits(&s->gb, buf, FFMAX(buf_size, buf_size2) * 8);
    if (s->codec_id == AV_CODEC_ID_RV10)
        mb_count = rv10_decode_picture_header(s);
    else
        mb_count = rv20_decode_picture_header(rv, whole_size);
    if (mb_count < 0) {
        if (mb_count != ERROR_SKIP_FRAME)
            av_log(s->avctx, AV_LOG_ERROR, "HEADER ERROR\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->mb_x >= s->mb_width ||
        s->mb_y >= s->mb_height) {
        av_log(s->avctx, AV_LOG_ERROR, "POS ERROR %d %d\n", s->mb_x, s->mb_y);
        return AVERROR_INVALIDDATA;
    }
    mb_pos = s->mb_y * s->mb_width + s->mb_x;
    left   = s->mb_width * s->mb_height - mb_pos;
    if (mb_count > left) {
        av_log(s->avctx, AV_LOG_ERROR, "COUNT ERROR\n");
        return AVERROR_INVALIDDATA;
    }

    if (whole_size < s->mb_width * s->mb_height / 8)
        return AVERROR_INVALIDDATA;

    if ((s->mb_x == 0 && s->mb_y == 0) || !s->current_picture_ptr) {
        // FIXME write parser so we always have complete frames?
        if (s->current_picture_ptr) {
            ff_er_frame_end(&s->er);
            ff_mpv_frame_end(s);
            s->mb_x = s->mb_y = s->resync_mb_x = s->resync_mb_y = 0;
        }
        if ((ret = ff_mpv_frame_start(s, avctx)) < 0)
            return ret;
        ff_mpeg_er_frame_start(s);
    } else {
        if (s->current_picture_ptr->f->pict_type != s->pict_type) {
            av_log(s->avctx, AV_LOG_ERROR, "Slice type mismatch\n");
            return AVERROR_INVALIDDATA;
        }
    }


    ff_dlog(avctx, "qscale=%d\n", s->qscale);

    /* default quantization values */
    if (s->codec_id == AV_CODEC_ID_RV10) {
        if (s->mb_y == 0)
            s->first_slice_line = 1;
    } else {
        s->first_slice_line = 1;
        s->resync_mb_x      = s->mb_x;
    }
    start_mb_x     = s->mb_x;
    s->resync_mb_y = s->mb_y;
    if (s->h263_aic) {
        s->y_dc_scale_table =
        s->c_dc_scale_table = ff_aic_dc_scale_table;
    } else {
        s->y_dc_scale_table =
        s->c_dc_scale_table = ff_mpeg1_dc_scale_table;
    }

    if (s->modified_quant)
        s->chroma_qscale_table = ff_h263_chroma_qscale_table;

    ff_set_qscale(s, s->qscale);

    s->rv10_first_dc_coded[0] = 0;
    s->rv10_first_dc_coded[1] = 0;
    s->rv10_first_dc_coded[2] = 0;
    s->block_wrap[0] =
    s->block_wrap[1] =
    s->block_wrap[2] =
    s->block_wrap[3] = s->b8_stride;
    s->block_wrap[4] =
    s->block_wrap[5] = s->mb_stride;
    ff_init_block_index(s);

    /* decode each macroblock */
    for (s->mb_num_left = mb_count; s->mb_num_left > 0; s->mb_num_left--) {
        int ret;
        ff_update_block_index(s);
        ff_tlog(avctx, "**mb x=%d y=%d\n", s->mb_x, s->mb_y);

        s->mv_dir  = MV_DIR_FORWARD;
        s->mv_type = MV_TYPE_16X16;
        ret = ff_h263_decode_mb(s, s->block);

        // Repeat the slice end check from ff_h263_decode_mb with our active
        // bitstream size
        if (ret != SLICE_ERROR && active_bits_size >= get_bits_count(&s->gb)) {
            int v = show_bits(&s->gb, 16);

            if (get_bits_count(&s->gb) + 16 > active_bits_size)
                v >>= get_bits_count(&s->gb) + 16 - active_bits_size;

            if (!v)
                ret = SLICE_END;
        }
        if (ret != SLICE_ERROR && active_bits_size < get_bits_count(&s->gb) &&
            8 * buf_size2 >= get_bits_count(&s->gb)) {
            active_bits_size = buf_size2 * 8;
            av_log(avctx, AV_LOG_DEBUG, "update size from %d to %d\n",
                   8 * buf_size, active_bits_size);
            ret = SLICE_OK;
        }

        if (ret == SLICE_ERROR || active_bits_size < get_bits_count(&s->gb)) {
            av_log(s->avctx, AV_LOG_ERROR, "ERROR at MB %d %d\n", s->mb_x,
                   s->mb_y);
            return AVERROR_INVALIDDATA;
        }
        if (s->pict_type != AV_PICTURE_TYPE_B)
            ff_h263_update_motion_val(s);
        ff_mpv_reconstruct_mb(s, s->block);
        if (s->loop_filter)
            ff_h263_loop_filter(s);

        if (++s->mb_x == s->mb_width) {
            s->mb_x = 0;
            s->mb_y++;
            ff_init_block_index(s);
        }
        if (s->mb_x == s->resync_mb_x)
            s->first_slice_line = 0;
        if (ret == SLICE_END)
            break;
    }

    ff_er_add_slice(&s->er, start_mb_x, s->resync_mb_y, s->mb_x - 1, s->mb_y,
                    ER_MB_END);

    return active_bits_size;
}

static int get_slice_offset(AVCodecContext *avctx, const uint8_t *buf, int n)
{
    if (avctx->slice_count)
        return avctx->slice_offset[n];
    else
        return AV_RL32(buf + n * 8);
}

static int rv10_decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                             AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    MpegEncContext *s = avctx->priv_data;
    AVFrame *pict = data;
    int i, ret;
    int slice_count;
    const uint8_t *slices_hdr = NULL;

    ff_dlog(avctx, "*****frame %d size=%d\n", avctx->frame_number, buf_size);

    /* no supplementary picture */
    if (buf_size == 0) {
        return 0;
    }

    if (!avctx->slice_count) {
        slice_count = (*buf++) + 1;
        buf_size--;

        if (!slice_count || buf_size <= 8 * slice_count) {
            av_log(avctx, AV_LOG_ERROR, "Invalid slice count: %d.\n",
                   slice_count);
            return AVERROR_INVALIDDATA;
        }

        slices_hdr = buf + 4;
        buf       += 8 * slice_count;
        buf_size  -= 8 * slice_count;
    } else
        slice_count = avctx->slice_count;

    for (i = 0; i < slice_count; i++) {
        unsigned offset = get_slice_offset(avctx, slices_hdr, i);
        int size, size2;

        if (offset >= buf_size)
            return AVERROR_INVALIDDATA;

        if (i + 1 == slice_count)
            size = buf_size - offset;
        else
            size = get_slice_offset(avctx, slices_hdr, i + 1) - offset;

        if (i + 2 >= slice_count)
            size2 = buf_size - offset;
        else
            size2 = get_slice_offset(avctx, slices_hdr, i + 2) - offset;

        if (size <= 0 || size2 <= 0 ||
            offset + FFMAX(size, size2) > buf_size)
            return AVERROR_INVALIDDATA;

        if ((ret = rv10_decode_packet(avctx, buf + offset, size, size2, buf_size)) < 0)
            return ret;

        if (ret > 8 * size)
            i++;
    }

    if (s->current_picture_ptr && s->mb_y >= s->mb_height) {
        ff_er_frame_end(&s->er);
        ff_mpv_frame_end(s);

        if (s->pict_type == AV_PICTURE_TYPE_B || s->low_delay) {
            if ((ret = av_frame_ref(pict, s->current_picture_ptr->f)) < 0)
                return ret;
            ff_print_debug_info(s, s->current_picture_ptr, pict);
            ff_mpv_export_qp_table(s, pict, s->current_picture_ptr, FF_QSCALE_TYPE_MPEG1);
        } else if (s->last_picture_ptr) {
            if ((ret = av_frame_ref(pict, s->last_picture_ptr->f)) < 0)
                return ret;
            ff_print_debug_info(s, s->last_picture_ptr, pict);
            ff_mpv_export_qp_table(s, pict,s->last_picture_ptr, FF_QSCALE_TYPE_MPEG1);
        }

        if (s->last_picture_ptr || s->low_delay) {
            *got_frame = 1;
        }

        // so we can detect if frame_end was not called (find some nicer solution...)
        s->current_picture_ptr = NULL;
    }

    return avpkt->size;
}

AVCodec ff_rv10_decoder = {
    .name           = "rv10",
    .long_name      = NULL_IF_CONFIG_SMALL("RealVideo 1.0"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_RV10,
    .priv_data_size = sizeof(RVDecContext),
    .init           = rv10_decode_init,
    .close          = rv10_decode_end,
    .decode         = rv10_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .max_lowres     = 3,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE
    },
};

AVCodec ff_rv20_decoder = {
    .name           = "rv20",
    .long_name      = NULL_IF_CONFIG_SMALL("RealVideo 2.0"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_RV20,
    .priv_data_size = sizeof(RVDecContext),
    .init           = rv10_decode_init,
    .close          = rv10_decode_end,
    .decode         = rv10_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY,
    .flush          = ff_mpeg_flush,
    .max_lowres     = 3,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE
    },
};
