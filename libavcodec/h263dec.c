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

#include "libavutil/cpu.h"
#include "avcodec.h"
#include "error_resilience.h"
#include "flv.h"
#include "h263.h"
#include "h263_parser.h"
#include "internal.h"
#include "mpeg_er.h"
#include "mpeg4video.h"
#include "mpeg4video_parser.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "msmpeg4.h"
#include "qpeldsp.h"
#include "vdpau_compat.h"
#include "thread.h"
#include "wmv2.h"

static enum AVPixelFormat h263_get_format(AVCodecContext *avctx)
{
    if (avctx->codec->id == AV_CODEC_ID_MSS2)
        return AV_PIX_FMT_YUV420P;

    if (CONFIG_GRAY && (avctx->flags & AV_CODEC_FLAG_GRAY)) {
        if (avctx->color_range == AVCOL_RANGE_UNSPECIFIED)
            avctx->color_range = AVCOL_RANGE_MPEG;
        return AV_PIX_FMT_GRAY8;
    }

    return avctx->pix_fmt = ff_get_format(avctx, avctx->codec->pix_fmts);
}

av_cold int ff_h263_decode_init(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;
    int ret;

    s->out_format      = FMT_H263;

    // set defaults
    ff_mpv_decode_defaults(s);
    ff_mpv_decode_init(s, avctx);

    s->quant_precision = 5;
    s->decode_mb       = ff_h263_decode_mb;
    s->low_delay       = 1;
    s->unrestricted_mv = 1;

    /* select sub codec */
    switch (avctx->codec->id) {
    case AV_CODEC_ID_H263:
    case AV_CODEC_ID_H263P:
        s->unrestricted_mv = 0;
        avctx->chroma_sample_location = AVCHROMA_LOC_CENTER;
        break;
    case AV_CODEC_ID_MPEG4:
        break;
    case AV_CODEC_ID_MSMPEG4V1:
        s->h263_pred       = 1;
        s->msmpeg4_version = 1;
        break;
    case AV_CODEC_ID_MSMPEG4V2:
        s->h263_pred       = 1;
        s->msmpeg4_version = 2;
        break;
    case AV_CODEC_ID_MSMPEG4V3:
        s->h263_pred       = 1;
        s->msmpeg4_version = 3;
        break;
    case AV_CODEC_ID_WMV1:
        s->h263_pred       = 1;
        s->msmpeg4_version = 4;
        break;
    case AV_CODEC_ID_WMV2:
        s->h263_pred       = 1;
        s->msmpeg4_version = 5;
        break;
    case AV_CODEC_ID_VC1:
    case AV_CODEC_ID_WMV3:
    case AV_CODEC_ID_VC1IMAGE:
    case AV_CODEC_ID_WMV3IMAGE:
    case AV_CODEC_ID_MSS2:
        s->h263_pred       = 1;
        s->msmpeg4_version = 6;
        avctx->chroma_sample_location = AVCHROMA_LOC_LEFT;
        break;
    case AV_CODEC_ID_H263I:
        break;
    case AV_CODEC_ID_FLV1:
        s->h263_flv = 1;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported codec %d\n",
               avctx->codec->id);
        return AVERROR(ENOSYS);
    }
    s->codec_id    = avctx->codec->id;

    if (avctx->codec_tag == AV_RL32("L263") || avctx->codec_tag == AV_RL32("S263"))
        if (avctx->extradata_size == 56 && avctx->extradata[0] == 1)
            s->ehc_mode = 1;

    /* for H.263, we allocate the images after having read the header */
    if (avctx->codec->id != AV_CODEC_ID_H263 &&
        avctx->codec->id != AV_CODEC_ID_H263P &&
        avctx->codec->id != AV_CODEC_ID_MPEG4) {
        avctx->pix_fmt = h263_get_format(avctx);
        ff_mpv_idct_init(s);
        if ((ret = ff_mpv_common_init(s)) < 0)
            return ret;
    }

    ff_h263dsp_init(&s->h263dsp);
    ff_qpeldsp_init(&s->qdsp);
    ff_h263_decode_init_vlc();

    return 0;
}

av_cold int ff_h263_decode_end(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;

    ff_mpv_common_end(s);
    return 0;
}

/**
 * Return the number of bytes consumed for building the current frame.
 */
static int get_consumed_bytes(MpegEncContext *s, int buf_size)
{
    int pos = (get_bits_count(&s->gb) + 7) >> 3;

    if (s->divx_packed || s->avctx->hwaccel) {
        /* We would have to scan through the whole buf to handle the weird
         * reordering ... */
        return buf_size;
    } else if (s->avctx->flags & AV_CODEC_FLAG_TRUNCATED) {
        pos -= s->parse_context.last_index;
        // padding is not really read so this might be -1
        if (pos < 0)
            pos = 0;
        return pos;
    } else {
        // avoid infinite loops (maybe not needed...)
        if (pos == 0)
            pos = 1;
        // oops ;)
        if (pos + 10 > buf_size)
            pos = buf_size;

        return pos;
    }
}

static int decode_slice(MpegEncContext *s)
{
    const int part_mask = s->partitioned_frame
                          ? (ER_AC_END | ER_AC_ERROR) : 0x7F;
    const int mb_size   = 16 >> s->avctx->lowres;
    int ret;

    s->last_resync_gb   = s->gb;
    s->first_slice_line = 1;
    s->resync_mb_x      = s->mb_x;
    s->resync_mb_y      = s->mb_y;

    ff_set_qscale(s, s->qscale);

    if (s->avctx->hwaccel) {
        const uint8_t *start = s->gb.buffer + get_bits_count(&s->gb) / 8;
        ret = s->avctx->hwaccel->decode_slice(s->avctx, start, s->gb.buffer_end - start);
        // ensure we exit decode loop
        s->mb_y = s->mb_height;
        return ret;
    }

    if (s->partitioned_frame) {
        const int qscale = s->qscale;

        if (CONFIG_MPEG4_DECODER && s->codec_id == AV_CODEC_ID_MPEG4)
            if ((ret = ff_mpeg4_decode_partitions(s->avctx->priv_data)) < 0)
                return ret;

        /* restore variables which were modified */
        s->first_slice_line = 1;
        s->mb_x             = s->resync_mb_x;
        s->mb_y             = s->resync_mb_y;
        ff_set_qscale(s, qscale);
    }

    for (; s->mb_y < s->mb_height; s->mb_y++) {
        /* per-row end of slice checks */
        if (s->msmpeg4_version) {
            if (s->resync_mb_y + s->slice_height == s->mb_y) {
                ff_er_add_slice(&s->er, s->resync_mb_x, s->resync_mb_y,
                                s->mb_x - 1, s->mb_y, ER_MB_END);

                return 0;
            }
        }

        if (s->msmpeg4_version == 1) {
            s->last_dc[0] =
            s->last_dc[1] =
            s->last_dc[2] = 128;
        }

        ff_init_block_index(s);
        for (; s->mb_x < s->mb_width; s->mb_x++) {
            int ret;

            ff_update_block_index(s);

            if (s->resync_mb_x == s->mb_x && s->resync_mb_y + 1 == s->mb_y)
                s->first_slice_line = 0;

            /* DCT & quantize */

            s->mv_dir  = MV_DIR_FORWARD;
            s->mv_type = MV_TYPE_16X16;
            ff_dlog(s, "%d %06X\n",
                    get_bits_count(&s->gb), show_bits(&s->gb, 24));

            ff_tlog(NULL, "Decoding MB at %dx%d\n", s->mb_x, s->mb_y);
            ret = s->decode_mb(s, s->block);

            if (s->pict_type != AV_PICTURE_TYPE_B)
                ff_h263_update_motion_val(s);

            if (ret < 0) {
                const int xy = s->mb_x + s->mb_y * s->mb_stride;
                if (ret == SLICE_END) {
                    ff_mpv_reconstruct_mb(s, s->block);
                    if (s->loop_filter)
                        ff_h263_loop_filter(s);

                    ff_er_add_slice(&s->er, s->resync_mb_x, s->resync_mb_y,
                                    s->mb_x, s->mb_y, ER_MB_END & part_mask);

                    s->padding_bug_score--;

                    if (++s->mb_x >= s->mb_width) {
                        s->mb_x = 0;
                        ff_mpeg_draw_horiz_band(s, s->mb_y * mb_size, mb_size);
                        ff_mpv_report_decode_progress(s);
                        s->mb_y++;
                    }
                    return 0;
                } else if (ret == SLICE_NOEND) {
                    av_log(s->avctx, AV_LOG_ERROR,
                           "Slice mismatch at MB: %d\n", xy);
                    ff_er_add_slice(&s->er, s->resync_mb_x, s->resync_mb_y,
                                    s->mb_x + 1, s->mb_y,
                                    ER_MB_END & part_mask);
                    return AVERROR_INVALIDDATA;
                }
                av_log(s->avctx, AV_LOG_ERROR, "Error at MB: %d\n", xy);
                ff_er_add_slice(&s->er, s->resync_mb_x, s->resync_mb_y,
                                s->mb_x, s->mb_y, ER_MB_ERROR & part_mask);

                if (s->avctx->err_recognition & AV_EF_IGNORE_ERR)
                    continue;
                return AVERROR_INVALIDDATA;
            }

            ff_mpv_reconstruct_mb(s, s->block);
            if (s->loop_filter)
                ff_h263_loop_filter(s);
        }

        ff_mpeg_draw_horiz_band(s, s->mb_y * mb_size, mb_size);
        ff_mpv_report_decode_progress(s);

        s->mb_x = 0;
    }

    av_assert1(s->mb_x == 0 && s->mb_y == s->mb_height);

    if (s->codec_id == AV_CODEC_ID_MPEG4         &&
        (s->workaround_bugs & FF_BUG_AUTODETECT) &&
        get_bits_left(&s->gb) >= 48              &&
        show_bits(&s->gb, 24) == 0x4010          &&
        !s->data_partitioning)
        s->padding_bug_score += 32;

    /* try to detect the padding bug */
    if (s->codec_id == AV_CODEC_ID_MPEG4         &&
        (s->workaround_bugs & FF_BUG_AUTODETECT) &&
        get_bits_left(&s->gb) >= 0               &&
        get_bits_left(&s->gb) < 137              &&
        !s->data_partitioning) {
        const int bits_count = get_bits_count(&s->gb);
        const int bits_left  = s->gb.size_in_bits - bits_count;

        if (bits_left == 0) {
            s->padding_bug_score += 16;
        } else if (bits_left != 1) {
            int v = show_bits(&s->gb, 8);
            v |= 0x7F >> (7 - (bits_count & 7));

            if (v == 0x7F && bits_left <= 8)
                s->padding_bug_score--;
            else if (v == 0x7F && ((get_bits_count(&s->gb) + 8) & 8) &&
                     bits_left <= 16)
                s->padding_bug_score += 4;
            else
                s->padding_bug_score++;
        }
    }

    if (s->codec_id == AV_CODEC_ID_H263          &&
        (s->workaround_bugs & FF_BUG_AUTODETECT) &&
        get_bits_left(&s->gb) >= 8               &&
        get_bits_left(&s->gb) < 300              &&
        s->pict_type == AV_PICTURE_TYPE_I        &&
        show_bits(&s->gb, 8) == 0                &&
        !s->data_partitioning) {

        s->padding_bug_score += 32;
    }

    if (s->codec_id == AV_CODEC_ID_H263          &&
        (s->workaround_bugs & FF_BUG_AUTODETECT) &&
        get_bits_left(&s->gb) >= 64              &&
        AV_RB64(s->gb.buffer_end - 8) == 0xCDCDCDCDFC7F0000) {

        s->padding_bug_score += 32;
    }

    if (s->workaround_bugs & FF_BUG_AUTODETECT) {
        if (
            (s->padding_bug_score > -2 && !s->data_partitioning))
            s->workaround_bugs |= FF_BUG_NO_PADDING;
        else
            s->workaround_bugs &= ~FF_BUG_NO_PADDING;
    }

    // handle formats which don't have unique end markers
    if (s->msmpeg4_version || (s->workaround_bugs & FF_BUG_NO_PADDING)) { // FIXME perhaps solve this more cleanly
        int left      = get_bits_left(&s->gb);
        int max_extra = 7;

        /* no markers in M$ crap */
        if (s->msmpeg4_version && s->pict_type == AV_PICTURE_TYPE_I)
            max_extra += 17;

        /* buggy padding but the frame should still end approximately at
         * the bitstream end */
        if ((s->workaround_bugs & FF_BUG_NO_PADDING) &&
            (s->avctx->err_recognition & (AV_EF_BUFFER|AV_EF_AGGRESSIVE)))
            max_extra += 48;
        else if ((s->workaround_bugs & FF_BUG_NO_PADDING))
            max_extra += 256 * 256 * 256 * 64;

        if (left > max_extra)
            av_log(s->avctx, AV_LOG_ERROR,
                   "discarding %d junk bits at end, next would be %X\n",
                   left, show_bits(&s->gb, 24));
        else if (left < 0)
            av_log(s->avctx, AV_LOG_ERROR, "overreading %d bits\n", -left);
        else
            ff_er_add_slice(&s->er, s->resync_mb_x, s->resync_mb_y,
                            s->mb_x - 1, s->mb_y, ER_MB_END);

        return 0;
    }

    av_log(s->avctx, AV_LOG_ERROR,
           "slice end not reached but screenspace end (%d left %06X, score= %d)\n",
           get_bits_left(&s->gb), show_bits(&s->gb, 24), s->padding_bug_score);

    ff_er_add_slice(&s->er, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y,
                    ER_MB_END & part_mask);

    return AVERROR_INVALIDDATA;
}

int ff_h263_decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                         AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    MpegEncContext *s  = avctx->priv_data;
    int ret;
    int slice_ret = 0;
    AVFrame *pict = data;

    /* no supplementary picture */
    if (buf_size == 0) {
        /* special case for last picture */
        if (s->low_delay == 0 && s->next_picture_ptr) {
            if ((ret = av_frame_ref(pict, s->next_picture_ptr->f)) < 0)
                return ret;
            s->next_picture_ptr = NULL;

            *got_frame = 1;
        }

        return 0;
    }

    if (s->avctx->flags & AV_CODEC_FLAG_TRUNCATED) {
        int next;

        if (CONFIG_MPEG4_DECODER && s->codec_id == AV_CODEC_ID_MPEG4) {
            next = ff_mpeg4_find_frame_end(&s->parse_context, buf, buf_size);
        } else if (CONFIG_H263_DECODER && s->codec_id == AV_CODEC_ID_H263) {
            next = ff_h263_find_frame_end(&s->parse_context, buf, buf_size);
        } else if (CONFIG_H263P_DECODER && s->codec_id == AV_CODEC_ID_H263P) {
            next = ff_h263_find_frame_end(&s->parse_context, buf, buf_size);
        } else {
            av_log(s->avctx, AV_LOG_ERROR,
                   "this codec does not support truncated bitstreams\n");
            return AVERROR(ENOSYS);
        }

        if (ff_combine_frame(&s->parse_context, next, (const uint8_t **)&buf,
                             &buf_size) < 0)
            return buf_size;
    }

retry:
    if (s->divx_packed && s->bitstream_buffer_size) {
        int i;
        for(i=0; i < buf_size-3; i++) {
            if (buf[i]==0 && buf[i+1]==0 && buf[i+2]==1) {
                if (buf[i+3]==0xB0) {
                    av_log(s->avctx, AV_LOG_WARNING, "Discarding excessive bitstream in packed xvid\n");
                    s->bitstream_buffer_size = 0;
                }
                break;
            }
        }
    }

    if (s->bitstream_buffer_size && (s->divx_packed || buf_size <= MAX_NVOP_SIZE)) // divx 5.01+/xvid frame reorder
        ret = init_get_bits8(&s->gb, s->bitstream_buffer,
                             s->bitstream_buffer_size);
    else
        ret = init_get_bits8(&s->gb, buf, buf_size);

    s->bitstream_buffer_size = 0;
    if (ret < 0)
        return ret;

    if (!s->context_initialized)
        // we need the idct permutation for reading a custom matrix
        ff_mpv_idct_init(s);

    /* let's go :-) */
    if (CONFIG_WMV2_DECODER && s->msmpeg4_version == 5) {
        ret = ff_wmv2_decode_picture_header(s);
    } else if (CONFIG_MSMPEG4_DECODER && s->msmpeg4_version) {
        ret = ff_msmpeg4_decode_picture_header(s);
    } else if (CONFIG_MPEG4_DECODER && avctx->codec_id == AV_CODEC_ID_MPEG4) {
        if (s->avctx->extradata_size && s->picture_number == 0) {
            GetBitContext gb;

            if (init_get_bits8(&gb, s->avctx->extradata, s->avctx->extradata_size) >= 0 )
                ff_mpeg4_decode_picture_header(avctx->priv_data, &gb);
        }
        ret = ff_mpeg4_decode_picture_header(avctx->priv_data, &s->gb);
    } else if (CONFIG_H263I_DECODER && s->codec_id == AV_CODEC_ID_H263I) {
        ret = ff_intel_h263_decode_picture_header(s);
    } else if (CONFIG_FLV_DECODER && s->h263_flv) {
        ret = ff_flv_decode_picture_header(s);
    } else {
        ret = ff_h263_decode_picture_header(s);
    }

    if (ret < 0 || ret == FRAME_SKIPPED) {
        if (   s->width  != avctx->coded_width
            || s->height != avctx->coded_height) {
                av_log(s->avctx, AV_LOG_WARNING, "Reverting picture dimensions change due to header decoding failure\n");
                s->width = avctx->coded_width;
                s->height= avctx->coded_height;
        }
    }
    if (ret == FRAME_SKIPPED)
        return get_consumed_bytes(s, buf_size);

    /* skip if the header was thrashed */
    if (ret < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "header damaged\n");
        return ret;
    }

    if (!s->context_initialized) {
        avctx->pix_fmt = h263_get_format(avctx);
        if ((ret = ff_mpv_common_init(s)) < 0)
            return ret;
    }

    if (!s->current_picture_ptr || s->current_picture_ptr->f->data[0]) {
        int i = ff_find_unused_picture(s->avctx, s->picture, 0);
        if (i < 0)
            return i;
        s->current_picture_ptr = &s->picture[i];
    }

    avctx->has_b_frames = !s->low_delay;

    if (CONFIG_MPEG4_DECODER && avctx->codec_id == AV_CODEC_ID_MPEG4) {
        if (ff_mpeg4_workaround_bugs(avctx) == 1)
            goto retry;
    }

    /* After H.263 & MPEG-4 header decode we have the height, width,
     * and other parameters. So then we could init the picture.
     * FIXME: By the way H.263 decoder is evolving it should have
     * an H263EncContext */
    if (s->width  != avctx->coded_width  ||
        s->height != avctx->coded_height ||
        s->context_reinit) {
        /* H.263 could change picture size any time */
        s->context_reinit = 0;

        ret = ff_set_dimensions(avctx, s->width, s->height);
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

    if (s->codec_id == AV_CODEC_ID_H263  ||
        s->codec_id == AV_CODEC_ID_H263P ||
        s->codec_id == AV_CODEC_ID_H263I)
        s->gob_index = H263_GOB_HEIGHT(s->height);

    // for skipping the frame
    s->current_picture.f->pict_type = s->pict_type;
    s->current_picture.f->key_frame = s->pict_type == AV_PICTURE_TYPE_I;

    /* skip B-frames if we don't have reference frames */
    if (!s->last_picture_ptr &&
        (s->pict_type == AV_PICTURE_TYPE_B || s->droppable))
        return get_consumed_bytes(s, buf_size);
    if ((avctx->skip_frame >= AVDISCARD_NONREF &&
         s->pict_type == AV_PICTURE_TYPE_B)    ||
        (avctx->skip_frame >= AVDISCARD_NONKEY &&
         s->pict_type != AV_PICTURE_TYPE_I)    ||
        avctx->skip_frame >= AVDISCARD_ALL)
        return get_consumed_bytes(s, buf_size);

    if (s->next_p_frame_damaged) {
        if (s->pict_type == AV_PICTURE_TYPE_B)
            return get_consumed_bytes(s, buf_size);
        else
            s->next_p_frame_damaged = 0;
    }

    if ((!s->no_rounding) || s->pict_type == AV_PICTURE_TYPE_B) {
        s->me.qpel_put = s->qdsp.put_qpel_pixels_tab;
        s->me.qpel_avg = s->qdsp.avg_qpel_pixels_tab;
    } else {
        s->me.qpel_put = s->qdsp.put_no_rnd_qpel_pixels_tab;
        s->me.qpel_avg = s->qdsp.avg_qpel_pixels_tab;
    }

    if ((ret = ff_mpv_frame_start(s, avctx)) < 0)
        return ret;

    if (!s->divx_packed)
        ff_thread_finish_setup(avctx);

#if FF_API_CAP_VDPAU
    if (CONFIG_MPEG4_VDPAU_DECODER && (s->avctx->codec->capabilities & AV_CODEC_CAP_HWACCEL_VDPAU)) {
        ff_vdpau_mpeg4_decode_picture(avctx->priv_data, s->gb.buffer, s->gb.buffer_end - s->gb.buffer);
        goto frame_end;
    }
#endif

    if (avctx->hwaccel) {
        ret = avctx->hwaccel->start_frame(avctx, s->gb.buffer,
                                          s->gb.buffer_end - s->gb.buffer);
        if (ret < 0 )
            return ret;
    }

    ff_mpeg_er_frame_start(s);

    /* the second part of the wmv2 header contains the MB skip bits which
     * are stored in current_picture->mb_type which is not available before
     * ff_mpv_frame_start() */
    if (CONFIG_WMV2_DECODER && s->msmpeg4_version == 5) {
        ret = ff_wmv2_decode_secondary_picture_header(s);
        if (ret < 0)
            return ret;
        if (ret == 1)
            goto frame_end;
    }

    /* decode each macroblock */
    s->mb_x = 0;
    s->mb_y = 0;

    slice_ret = decode_slice(s);
    while (s->mb_y < s->mb_height) {
        if (s->msmpeg4_version) {
            if (s->slice_height == 0 || s->mb_x != 0 ||
                (s->mb_y % s->slice_height) != 0 || get_bits_left(&s->gb) < 0)
                break;
        } else {
            int prev_x = s->mb_x, prev_y = s->mb_y;
            if (ff_h263_resync(s) < 0)
                break;
            if (prev_y * s->mb_width + prev_x < s->mb_y * s->mb_width + s->mb_x)
                s->er.error_occurred = 1;
        }

        if (s->msmpeg4_version < 4 && s->h263_pred)
            ff_mpeg4_clean_buffers(s);

        if (decode_slice(s) < 0)
            slice_ret = AVERROR_INVALIDDATA;
    }

    if (s->msmpeg4_version && s->msmpeg4_version < 4 &&
        s->pict_type == AV_PICTURE_TYPE_I)
        if (!CONFIG_MSMPEG4_DECODER ||
            ff_msmpeg4_decode_ext_header(s, buf_size) < 0)
            s->er.error_status_table[s->mb_num - 1] = ER_MB_ERROR;

    av_assert1(s->bitstream_buffer_size == 0);
frame_end:
    ff_er_frame_end(&s->er);

    if (avctx->hwaccel) {
        ret = avctx->hwaccel->end_frame(avctx);
        if (ret < 0)
            return ret;
    }

    ff_mpv_frame_end(s);

    if (CONFIG_MPEG4_DECODER && avctx->codec_id == AV_CODEC_ID_MPEG4)
        ff_mpeg4_frame_end(avctx, buf, buf_size);

    if (!s->divx_packed && avctx->hwaccel)
        ff_thread_finish_setup(avctx);

    av_assert1(s->current_picture.f->pict_type == s->current_picture_ptr->f->pict_type);
    av_assert1(s->current_picture.f->pict_type == s->pict_type);
    if (s->pict_type == AV_PICTURE_TYPE_B || s->low_delay) {
        if ((ret = av_frame_ref(pict, s->current_picture_ptr->f)) < 0)
            return ret;
        ff_print_debug_info(s, s->current_picture_ptr, pict);
        ff_mpv_export_qp_table(s, pict, s->current_picture_ptr, FF_QSCALE_TYPE_MPEG1);
    } else if (s->last_picture_ptr) {
        if ((ret = av_frame_ref(pict, s->last_picture_ptr->f)) < 0)
            return ret;
        ff_print_debug_info(s, s->last_picture_ptr, pict);
        ff_mpv_export_qp_table(s, pict, s->last_picture_ptr, FF_QSCALE_TYPE_MPEG1);
    }

    if (s->last_picture_ptr || s->low_delay) {
        if (   pict->format == AV_PIX_FMT_YUV420P
            && (s->codec_tag == AV_RL32("GEOV") || s->codec_tag == AV_RL32("GEOX"))) {
            int x, y, p;
            av_frame_make_writable(pict);
            for (p=0; p<3; p++) {
                int w = AV_CEIL_RSHIFT(pict-> width, !!p);
                int h = AV_CEIL_RSHIFT(pict->height, !!p);
                int linesize = pict->linesize[p];
                for (y=0; y<(h>>1); y++)
                    for (x=0; x<w; x++)
                        FFSWAP(int,
                               pict->data[p][x + y*linesize],
                               pict->data[p][x + (h-1-y)*linesize]);
            }
        }
        *got_frame = 1;
    }

    if (slice_ret < 0 && (avctx->err_recognition & AV_EF_EXPLODE))
        return slice_ret;
    else
        return get_consumed_bytes(s, buf_size);
}

const enum AVPixelFormat ff_h263_hwaccel_pixfmt_list_420[] = {
#if CONFIG_H263_VAAPI_HWACCEL || CONFIG_MPEG4_VAAPI_HWACCEL
    AV_PIX_FMT_VAAPI,
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

AVCodec ff_h263_decoder = {
    .name           = "h263",
    .long_name      = NULL_IF_CONFIG_SMALL("H.263 / H.263-1996, H.263+ / H.263-1998 / H.263 version 2"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H263,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_h263_decode_init,
    .close          = ff_h263_decode_end,
    .decode         = ff_h263_decode_frame,
    .capabilities   = AV_CODEC_CAP_DRAW_HORIZ_BAND | AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_TRUNCATED | AV_CODEC_CAP_DELAY,
    .caps_internal  = FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    .flush          = ff_mpeg_flush,
    .max_lowres     = 3,
    .pix_fmts       = ff_h263_hwaccel_pixfmt_list_420,
};

AVCodec ff_h263p_decoder = {
    .name           = "h263p",
    .long_name      = NULL_IF_CONFIG_SMALL("H.263 / H.263-1996, H.263+ / H.263-1998 / H.263 version 2"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H263P,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_h263_decode_init,
    .close          = ff_h263_decode_end,
    .decode         = ff_h263_decode_frame,
    .capabilities   = AV_CODEC_CAP_DRAW_HORIZ_BAND | AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_TRUNCATED | AV_CODEC_CAP_DELAY,
    .caps_internal  = FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    .flush          = ff_mpeg_flush,
    .max_lowres     = 3,
    .pix_fmts       = ff_h263_hwaccel_pixfmt_list_420,
};
