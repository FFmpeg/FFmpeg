/*
 * H.263 decoder
 * Copyright (c) 2001 Fabrice Bellard
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
 * H.263 decoder.
 */

#define UNCHECKED_BITSTREAM_READER 1

#include "config_components.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "error_resilience.h"
#include "flvdec.h"
#include "h263.h"
#include "h263dec.h"
#include "hwaccel_internal.h"
#include "hwconfig.h"
#include "mpeg_er.h"
#include "mpeg4video.h"
#include "mpeg4videodec.h"
#include "mpegvideo.h"
#include "mpegvideodata.h"
#include "mpegvideodec.h"
#include "mpegvideo_unquantize.h"
#include "msmpeg4dec.h"
#include "thread.h"
#include "wmv2dec.h"

static const enum AVPixelFormat h263_hwaccel_pixfmt_list_420[] = {
#if CONFIG_H263_VAAPI_HWACCEL || CONFIG_MPEG4_VAAPI_HWACCEL
    AV_PIX_FMT_VAAPI,
#endif
#if CONFIG_MPEG4_NVDEC_HWACCEL
    AV_PIX_FMT_CUDA,
#endif
#if CONFIG_MPEG4_VDPAU_HWACCEL
    AV_PIX_FMT_VDPAU,
#endif
#if CONFIG_H263_VIDEOTOOLBOX_HWACCEL || CONFIG_MPEG4_VIDEOTOOLBOX_HWACCEL
    AV_PIX_FMT_VIDEOTOOLBOX,
#endif
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NONE
};

static enum AVPixelFormat h263_get_format(AVCodecContext *avctx)
{
    /* MPEG-4 Studio Profile only, not supported by hardware */
    if (avctx->bits_per_raw_sample > 8) {
        av_assert1(((MpegEncContext *)avctx->priv_data)->studio_profile);
        return avctx->pix_fmt;
    }

    if (CONFIG_GRAY && (avctx->flags & AV_CODEC_FLAG_GRAY)) {
        if (avctx->color_range == AVCOL_RANGE_UNSPECIFIED)
            avctx->color_range = AVCOL_RANGE_MPEG;
        return AV_PIX_FMT_GRAY8;
    }

    if (avctx->codec_id == AV_CODEC_ID_H263  ||
        avctx->codec_id == AV_CODEC_ID_H263P ||
        avctx->codec_id == AV_CODEC_ID_MPEG4)
        return avctx->pix_fmt = ff_get_format(avctx, h263_hwaccel_pixfmt_list_420);

    return AV_PIX_FMT_YUV420P;
}

av_cold int ff_h263_decode_init(AVCodecContext *avctx)
{
    H263DecContext *const h = avctx->priv_data;
    MPVContext *const s = &h->c;
    MPVUnquantDSPContext unquant_dsp_ctx;
    int ret;

    s->out_format      = FMT_H263;

    // set defaults
    ret = ff_mpv_decode_init(s, avctx);
    if (ret < 0)
        return ret;

    h->decode_mb       = ff_h263_decode_mb;
    s->low_delay       = 1;

    s->y_dc_scale_table =
    s->c_dc_scale_table = ff_mpeg1_dc_scale_table;

    ff_mpv_unquantize_init(&unquant_dsp_ctx,
                           avctx->flags & AV_CODEC_FLAG_BITEXACT, 0);
    // dct_unquantize defaults for H.263;
    // they might change on a per-frame basis for MPEG-4;
    // dct_unquantize_inter will be unset for MSMPEG4 codecs later.
    s->dct_unquantize_intra = unquant_dsp_ctx.dct_unquantize_h263_intra;
    s->dct_unquantize_inter = unquant_dsp_ctx.dct_unquantize_h263_inter;

    /* select sub codec */
    switch (avctx->codec->id) {
    case AV_CODEC_ID_H263:
    case AV_CODEC_ID_H263P:
        avctx->chroma_sample_location = AVCHROMA_LOC_CENTER;
        h->decode_header = ff_h263_decode_picture_header;
        break;
    case AV_CODEC_ID_MPEG4:
        break;
    case AV_CODEC_ID_MSMPEG4V1:
        s->h263_pred       = 1;
        s->msmpeg4_version = MSMP4_V1;
        break;
    case AV_CODEC_ID_MSMPEG4V2:
        s->h263_pred       = 1;
        s->msmpeg4_version = MSMP4_V2;
        break;
    case AV_CODEC_ID_MSMPEG4V3:
        s->h263_pred       = 1;
        s->msmpeg4_version = MSMP4_V3;
        break;
    case AV_CODEC_ID_WMV1:
        s->h263_pred       = 1;
        s->msmpeg4_version = MSMP4_WMV1;
        break;
    case AV_CODEC_ID_WMV2:
        s->h263_pred       = 1;
        s->msmpeg4_version = MSMP4_WMV2;
        break;
    case AV_CODEC_ID_RV10:
    case AV_CODEC_ID_RV20:
        break;
#if CONFIG_H263I_DECODER
    case AV_CODEC_ID_H263I:
        h->decode_header = ff_intel_h263_decode_picture_header;
        break;
#endif
#if CONFIG_FLV_DECODER
    case AV_CODEC_ID_FLV1:
        h->decode_header = ff_flv_decode_picture_header;
        break;
#endif
    default:
        av_unreachable("Switch contains a case for every codec using ff_h263_decode_init()");
    }

    if (avctx->codec_tag == AV_RL32("L263") || avctx->codec_tag == AV_RL32("S263"))
        if (avctx->extradata_size == 56 && avctx->extradata[0] == 1)
            h->ehc_mode = 1;

    /* for H.263, we allocate the images after having read the header */
    if (avctx->codec->id != AV_CODEC_ID_H263 &&
        avctx->codec->id != AV_CODEC_ID_H263P &&
        avctx->codec->id != AV_CODEC_ID_MPEG4) {
        avctx->pix_fmt = h263_get_format(avctx);
        if ((ret = ff_mpv_common_init(s)) < 0)
            return ret;
    }

    ff_h263dsp_init(&s->h263dsp);
    ff_h263_decode_init_vlc();

    return 0;
}

static void report_decode_progress(H263DecContext *const h)
{
    if (h->c.pict_type != AV_PICTURE_TYPE_B && !h->partitioned_frame && !h->c.er.error_occurred)
        ff_thread_progress_report(&h->c.cur_pic.ptr->progress, h->c.mb_y);
}

static int decode_slice(H263DecContext *const h)
{
    const int part_mask = h->partitioned_frame
                          ? (ER_AC_END | ER_AC_ERROR) : 0x7F;
    const int mb_size   = 16 >> h->c.avctx->lowres;
    int ret;

    h->last_resync_gb     = h->gb;
    h->c.first_slice_line = 1;
    h->c.resync_mb_x      = h->c.mb_x;
    h->c.resync_mb_y      = h->c.mb_y;

    ff_set_qscale(&h->c, h->c.qscale);

    if (h->c.studio_profile) {
        if ((ret = ff_mpeg4_decode_studio_slice_header(h)) < 0)
            return ret;
    }

    if (h->c.avctx->hwaccel) {
        const uint8_t *start = h->gb.buffer + get_bits_count(&h->gb) / 8;
        ret = FF_HW_CALL(h->c.avctx, decode_slice, start,
                         get_bits_bytesize(&h->gb, 0) - get_bits_count(&h->gb) / 8);
        // ensure we exit decode loop
        h->c.mb_y = h->c.mb_height;
        return ret;
    }

    if (h->partitioned_frame) {
        const int qscale = h->c.qscale;

        if (CONFIG_MPEG4_DECODER && h->c.codec_id == AV_CODEC_ID_MPEG4)
            if ((ret = ff_mpeg4_decode_partitions(h)) < 0)
                return ret;

        /* restore variables which were modified */
        h->c.first_slice_line = 1;
        h->c.mb_x             = h->c.resync_mb_x;
        h->c.mb_y             = h->c.resync_mb_y;
        ff_set_qscale(&h->c, qscale);
    }

    for (; h->c.mb_y < h->c.mb_height; h->c.mb_y++) {
        /* per-row end of slice checks */
        if (h->c.msmpeg4_version != MSMP4_UNUSED) {
            if (h->c.resync_mb_y + h->slice_height == h->c.mb_y) {
                ff_er_add_slice(&h->c.er, h->c.resync_mb_x, h->c.resync_mb_y,
                                h->c.mb_x - 1, h->c.mb_y, ER_MB_END);

                return 0;
            }
        }

        if (h->c.msmpeg4_version == MSMP4_V1) {
            h->c.last_dc[0] =
            h->c.last_dc[1] =
            h->c.last_dc[2] = 128;
        }

        ff_init_block_index(&h->c);
        for (; h->c.mb_x < h->c.mb_width; h->c.mb_x++) {
            int ret;

            ff_update_block_index(&h->c, h->c.avctx->bits_per_raw_sample,
                                  h->c.avctx->lowres, h->c.chroma_x_shift);

            if (h->c.resync_mb_x == h->c.mb_x && h->c.resync_mb_y + 1 == h->c.mb_y)
                h->c.first_slice_line = 0;

            /* DCT & quantize */

            h->c.mv_dir  = MV_DIR_FORWARD;
            h->c.mv_type = MV_TYPE_16X16;
            ff_dlog(h->c.avctx, "%d %06X\n",
                    get_bits_count(&h->gb), show_bits(&h->gb, 24));

            ff_tlog(NULL, "Decoding MB at %dx%d\n", h->c.mb_x, h->c.mb_y);
            ret = h->decode_mb(h);

            if (h->c.h263_pred || h->c.h263_aic) {
                int mb_xy = h->c.mb_y * h->c.mb_stride + h->c.mb_x;
                if (!h->c.mb_intra) {
                    ff_h263_clean_intra_table_entries(&h->c, mb_xy);
                } else
                    h->c.mbintra_table[mb_xy] = 1;
            }

            if (h->c.pict_type != AV_PICTURE_TYPE_B)
                ff_h263_update_motion_val(&h->c);

            if (ret < 0) {
                const int xy = h->c.mb_x + h->c.mb_y * h->c.mb_stride;
                if (ret == SLICE_END) {
                    ff_mpv_reconstruct_mb(&h->c, h->block);
                    if (h->loop_filter)
                        ff_h263_loop_filter(&h->c);

                    ff_er_add_slice(&h->c.er, h->c.resync_mb_x, h->c.resync_mb_y,
                                    h->c.mb_x, h->c.mb_y, ER_MB_END & part_mask);

                    h->padding_bug_score--;

                    if (++h->c.mb_x >= h->c.mb_width) {
                        h->c.mb_x = 0;
                        report_decode_progress(h);
                        ff_mpeg_draw_horiz_band(&h->c, h->c.mb_y * mb_size, mb_size);
                        h->c.mb_y++;
                    }
                    return 0;
                } else if (ret == SLICE_NOEND) {
                    av_log(h->c.avctx, AV_LOG_ERROR,
                           "Slice mismatch at MB: %d\n", xy);
                    ff_er_add_slice(&h->c.er, h->c.resync_mb_x, h->c.resync_mb_y,
                                    h->c.mb_x + 1, h->c.mb_y,
                                    ER_MB_END & part_mask);
                    return AVERROR_INVALIDDATA;
                }
                av_log(h->c.avctx, AV_LOG_ERROR, "Error at MB: %d\n", xy);
                ff_er_add_slice(&h->c.er, h->c.resync_mb_x, h->c.resync_mb_y,
                                h->c.mb_x, h->c.mb_y, ER_MB_ERROR & part_mask);

                if ((h->c.avctx->err_recognition & AV_EF_IGNORE_ERR) && get_bits_left(&h->gb) > 0)
                    continue;
                return AVERROR_INVALIDDATA;
            }

            ff_mpv_reconstruct_mb(&h->c, h->block);
            if (h->loop_filter)
                ff_h263_loop_filter(&h->c);
        }

        report_decode_progress(h);
        ff_mpeg_draw_horiz_band(&h->c, h->c.mb_y * mb_size, mb_size);

        h->c.mb_x = 0;
    }

    av_assert1(h->c.mb_x == 0 && h->c.mb_y == h->c.mb_height);

    // Detect incorrect padding with wrong stuffing codes used by NEC N-02B
    if (h->c.codec_id == AV_CODEC_ID_MPEG4         &&
        (h->c.workaround_bugs & FF_BUG_AUTODETECT) &&
        get_bits_left(&h->gb) >= 48                &&
        show_bits(&h->gb, 24) == 0x4010            &&
        !h->data_partitioning)
        h->padding_bug_score += 32;

    /* try to detect the padding bug */
    if (h->c.codec_id == AV_CODEC_ID_MPEG4         &&
        (h->c.workaround_bugs & FF_BUG_AUTODETECT) &&
        get_bits_left(&h->gb) >= 0                 &&
        get_bits_left(&h->gb) < 137                &&
        !h->data_partitioning) {
        const int bits_count = get_bits_count(&h->gb);
        const int bits_left  = h->gb.size_in_bits - bits_count;

        if (bits_left == 0) {
            h->padding_bug_score += 16;
        } else if (bits_left != 1) {
            int v = show_bits(&h->gb, 8);
            v |= 0x7F >> (7 - (bits_count & 7));

            if (v == 0x7F && bits_left <= 8)
                h->padding_bug_score--;
            else if (v == 0x7F && ((get_bits_count(&h->gb) + 8) & 8) &&
                     bits_left <= 16)
                h->padding_bug_score += 4;
            else
                h->padding_bug_score++;
        }
    }

    if (h->c.codec_id == AV_CODEC_ID_H263          &&
        (h->c.workaround_bugs & FF_BUG_AUTODETECT) &&
        get_bits_left(&h->gb) >= 8                 &&
        get_bits_left(&h->gb) < 300                &&
        h->c.pict_type == AV_PICTURE_TYPE_I        &&
        show_bits(&h->gb, 8) == 0                  &&
        !h->data_partitioning) {

        h->padding_bug_score += 32;
    }

    if (h->c.codec_id == AV_CODEC_ID_H263          &&
        (h->c.workaround_bugs & FF_BUG_AUTODETECT) &&
        get_bits_left(&h->gb) >= 64                &&
        AV_RB64(h->gb.buffer + (get_bits_bytesize(&h->gb, 0) - 8)) == 0xCDCDCDCDFC7F0000) {

        h->padding_bug_score += 32;
    }

    if (h->c.workaround_bugs & FF_BUG_AUTODETECT) {
        if (
            (h->padding_bug_score > -2 && !h->data_partitioning))
            h->c.workaround_bugs |= FF_BUG_NO_PADDING;
        else
            h->c.workaround_bugs &= ~FF_BUG_NO_PADDING;
    }

    // handle formats which don't have unique end markers
    if (h->c.msmpeg4_version != MSMP4_UNUSED || (h->c.workaround_bugs & FF_BUG_NO_PADDING)) { // FIXME perhaps solve this more cleanly
        int left      = get_bits_left(&h->gb);
        int max_extra = 7;

        /* no markers in M$ crap */
        if (h->c.msmpeg4_version != MSMP4_UNUSED && h->c.pict_type == AV_PICTURE_TYPE_I)
            max_extra += 17;

        /* buggy padding but the frame should still end approximately at
         * the bitstream end */
        if ((h->c.workaround_bugs & FF_BUG_NO_PADDING) &&
            (h->c.avctx->err_recognition & (AV_EF_BUFFER|AV_EF_AGGRESSIVE)))
            max_extra += 48;
        else if ((h->c.workaround_bugs & FF_BUG_NO_PADDING))
            max_extra += 256 * 256 * 256 * 64;

        if (left > max_extra)
            av_log(h->c.avctx, AV_LOG_ERROR,
                   "discarding %d junk bits at end, next would be %X\n",
                   left, show_bits(&h->gb, 24));
        else if (left < 0)
            av_log(h->c.avctx, AV_LOG_ERROR, "overreading %d bits\n", -left);
        else
            ff_er_add_slice(&h->c.er, h->c.resync_mb_x, h->c.resync_mb_y,
                            h->c.mb_x - 1, h->c.mb_y, ER_MB_END);

        return 0;
    }

    av_log(h->c.avctx, AV_LOG_ERROR,
           "slice end not reached but screenspace end (%d left %06X, score= %d)\n",
           get_bits_left(&h->gb), show_bits(&h->gb, 24), h->padding_bug_score);

    ff_er_add_slice(&h->c.er, h->c.resync_mb_x, h->c.resync_mb_y, h->c.mb_x, h->c.mb_y,
                    ER_MB_END & part_mask);

    return AVERROR_INVALIDDATA;
}

int ff_h263_decode_frame(AVCodecContext *avctx, AVFrame *pict,
                         int *got_frame, AVPacket *avpkt)
{
    H263DecContext *const h = avctx->priv_data;
    MPVContext *const s = &h->c;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    int ret;
    int slice_ret = 0;
    int bak_width, bak_height;

    /* no supplementary picture */
    if (buf_size == 0) {
        /* special case for last picture */
        if ((!h->c.low_delay || h->skipped_last_frame) && h->c.next_pic.ptr) {
            if ((ret = av_frame_ref(pict, h->c.next_pic.ptr->f)) < 0)
                return ret;
            if (h->skipped_last_frame) {
                /* If the stream ended with an NVOP, we output the last frame
                 * in display order, but with the props from the last input
                 * packet so that the stream's end time is correct. */
                ret = ff_decode_frame_props(avctx, pict);
                if (ret < 0)
                    return ret;
            }

            ff_mpv_unref_picture(&h->c.next_pic);

            *got_frame = 1;
        }

        return 0;
    }

    // h->gb might be overridden in ff_mpeg4_decode_picture_header() below.
    ret = init_get_bits8(&h->gb, buf, buf_size);
    if (ret < 0)
        return ret;

    bak_width  = h->c.width;
    bak_height = h->c.height;

    /* let's go :-) */
    ret = h->decode_header(h);
    if (ret < 0 || ret == FRAME_SKIPPED) {
        if (   h->c.width  != bak_width
            || h->c.height != bak_height) {
                av_log(h->c.avctx, AV_LOG_WARNING, "Reverting picture dimensions change due to header decoding failure\n");
                h->c.width = bak_width;
                h->c.height= bak_height;

        }
    }
    if (ret == FRAME_SKIPPED)
        return buf_size;

    /* skip if the header was thrashed */
    if (ret < 0) {
        av_log(h->c.avctx, AV_LOG_ERROR, "header damaged\n");
        return ret;
    }

    if (!h->c.context_initialized) {
        avctx->pix_fmt = h263_get_format(avctx);
        if ((ret = ff_mpv_common_init(s)) < 0)
            return ret;
    }

    avctx->has_b_frames = !h->c.low_delay;

    if (CONFIG_MPEG4_DECODER && avctx->codec_id == AV_CODEC_ID_MPEG4) {
        if (h->c.pict_type != AV_PICTURE_TYPE_B && h->c.mb_num/2 > get_bits_left(&h->gb))
            return AVERROR_INVALIDDATA;
        ff_mpeg4_workaround_bugs(avctx);
        if (h->c.studio_profile != (h->c.idsp.idct == NULL))
            ff_mpv_idct_init(s);
    }

    /* After H.263 & MPEG-4 header decode we have the height, width,
     * and other parameters. So then we could init the picture. */
    if (h->c.width  != avctx->coded_width  ||
        h->c.height != avctx->coded_height ||
        h->c.context_reinit) {
        /* H.263 could change picture size any time */
        h->c.context_reinit = 0;

        ret = ff_set_dimensions(avctx, h->c.width, h->c.height);
        if (ret < 0)
            return ret;

        ff_set_sar(avctx, avctx->sample_aspect_ratio);

        if ((ret = ff_mpv_common_frame_size_change(s)))
            return ret;

        if (avctx->pix_fmt != h263_get_format(avctx)) {
            av_log(avctx, AV_LOG_ERROR, "format change not supported\n");
            avctx->pix_fmt = AV_PIX_FMT_NONE;
            return AVERROR_UNKNOWN;
        }
    }

    /* skip B-frames if we don't have reference frames */
    if (!h->c.last_pic.ptr &&
        (h->c.pict_type == AV_PICTURE_TYPE_B || h->c.droppable))
        return buf_size;
    if ((avctx->skip_frame >= AVDISCARD_NONREF &&
         h->c.pict_type == AV_PICTURE_TYPE_B)    ||
        (avctx->skip_frame >= AVDISCARD_NONKEY &&
         h->c.pict_type != AV_PICTURE_TYPE_I)    ||
        avctx->skip_frame >= AVDISCARD_ALL)
        return buf_size;

    if ((ret = ff_mpv_frame_start(s, avctx)) < 0)
        return ret;

    if (!h->divx_packed)
        ff_thread_finish_setup(avctx);

    if (avctx->hwaccel) {
        ret = FF_HW_CALL(avctx, start_frame, NULL,
                         h->gb.buffer, get_bits_bytesize(&h->gb, 0));
        if (ret < 0 )
            return ret;
    }

    ff_mpv_er_frame_start_ext(s, h->partitioned_frame,
                              s->pp_time, s->pb_time);

    /* the second part of the wmv2 header contains the MB skip bits which
     * are stored in current_picture->mb_type which is not available before
     * ff_mpv_frame_start() */
#if CONFIG_WMV2_DECODER
    if (h->c.msmpeg4_version == MSMP4_WMV2) {
        ret = ff_wmv2_decode_secondary_picture_header(h);
        if (ret < 0)
            return ret;
        if (ret == 1)
            goto frame_end;
    }
#endif

    /* decode each macroblock */
    h->c.mb_x = 0;
    h->c.mb_y = 0;

    slice_ret = decode_slice(h);
    while (h->c.mb_y < h->c.mb_height) {
        if (h->c.msmpeg4_version != MSMP4_UNUSED) {
            if (h->slice_height == 0 || h->c.mb_x != 0 || slice_ret < 0 ||
                (h->c.mb_y % h->slice_height) != 0 || get_bits_left(&h->gb) < 0)
                break;
        } else {
            int prev_x = h->c.mb_x, prev_y = h->c.mb_y;
            if (ff_h263_resync(h) < 0)
                break;
            if (prev_y * h->c.mb_width + prev_x < h->c.mb_y * h->c.mb_width + h->c.mb_x)
                h->c.er.error_occurred = 1;
        }

        if (h->c.msmpeg4_version < MSMP4_WMV1 && h->c.h263_pred)
            ff_mpeg4_clean_buffers(s);

        if (decode_slice(h) < 0)
            slice_ret = AVERROR_INVALIDDATA;
    }

    if (h->c.msmpeg4_version != MSMP4_UNUSED && h->c.msmpeg4_version < MSMP4_WMV1 &&
        h->c.pict_type == AV_PICTURE_TYPE_I)
        if (!CONFIG_MSMPEG4DEC ||
            ff_msmpeg4_decode_ext_header(h, buf_size) < 0)
            h->c.er.error_status_table[h->c.mb_num - 1] = ER_MB_ERROR;

frame_end:
    if (!h->c.studio_profile)
        ff_er_frame_end(&h->c.er, NULL);

    if (avctx->hwaccel) {
        ret = FF_HW_SIMPLE_CALL(avctx, end_frame);
        if (ret < 0)
            return ret;
    }

    ff_mpv_frame_end(s);

    if (CONFIG_MPEG4_DECODER && avctx->codec_id == AV_CODEC_ID_MPEG4)
        ff_mpeg4_frame_end(avctx, avpkt);

    av_assert1(h->c.pict_type == h->c.cur_pic.ptr->f->pict_type);
    if (h->c.pict_type == AV_PICTURE_TYPE_B || h->c.low_delay) {
        if ((ret = av_frame_ref(pict, h->c.cur_pic.ptr->f)) < 0)
            return ret;
        ff_print_debug_info(s, h->c.cur_pic.ptr, pict);
        ff_mpv_export_qp_table(s, pict, h->c.cur_pic.ptr, FF_MPV_QSCALE_TYPE_MPEG1);
    } else if (h->c.last_pic.ptr) {
        if ((ret = av_frame_ref(pict, h->c.last_pic.ptr->f)) < 0)
            return ret;
        ff_print_debug_info(s, h->c.last_pic.ptr, pict);
        ff_mpv_export_qp_table(s, pict, h->c.last_pic.ptr, FF_MPV_QSCALE_TYPE_MPEG1);
    }

    if (h->c.last_pic.ptr || h->c.low_delay) {
        if (   pict->format == AV_PIX_FMT_YUV420P
            && (h->c.codec_tag == AV_RL32("GEOV") || h->c.codec_tag == AV_RL32("GEOX"))) {
            for (int p = 0; p < 3; p++) {
                int h = AV_CEIL_RSHIFT(pict->height, !!p);

                pict->data[p]     += (h - 1) * pict->linesize[p];
                pict->linesize[p] *= -1;
            }
        }
        *got_frame = 1;
    }

    if (slice_ret < 0 && (avctx->err_recognition & AV_EF_EXPLODE))
        return slice_ret;
    else
        return buf_size;
}

static const AVCodecHWConfigInternal *const h263_hw_config_list[] = {
#if CONFIG_H263_VAAPI_HWACCEL
    HWACCEL_VAAPI(h263),
#endif
#if CONFIG_MPEG4_NVDEC_HWACCEL
    HWACCEL_NVDEC(mpeg4),
#endif
#if CONFIG_MPEG4_VDPAU_HWACCEL
    HWACCEL_VDPAU(mpeg4),
#endif
#if CONFIG_H263_VIDEOTOOLBOX_HWACCEL
    HWACCEL_VIDEOTOOLBOX(h263),
#endif
    NULL
};

const FFCodec ff_h263_decoder = {
    .p.name         = "h263",
    CODEC_LONG_NAME("H.263 / H.263-1996, H.263+ / H.263-1998 / H.263 version 2"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_H263,
    .priv_data_size = sizeof(H263DecContext),
    .init           = ff_h263_decode_init,
    FF_CODEC_DECODE_CB(ff_h263_decode_frame),
    .close          = ff_mpv_decode_close,
    .p.capabilities = AV_CODEC_CAP_DRAW_HORIZ_BAND | AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_DELAY,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP |
                      FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    .flush          = ff_mpeg_flush,
    .p.max_lowres   = 3,
    .hw_configs     = h263_hw_config_list,
};

const FFCodec ff_h263p_decoder = {
    .p.name         = "h263p",
    CODEC_LONG_NAME("H.263 / H.263-1996, H.263+ / H.263-1998 / H.263 version 2"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_H263P,
    .priv_data_size = sizeof(H263DecContext),
    .init           = ff_h263_decode_init,
    FF_CODEC_DECODE_CB(ff_h263_decode_frame),
    .close          = ff_mpv_decode_close,
    .p.capabilities = AV_CODEC_CAP_DRAW_HORIZ_BAND | AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_DELAY,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP |
                      FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    .flush          = ff_mpeg_flush,
    .p.max_lowres   = 3,
    .hw_configs     = h263_hw_config_list,
};
