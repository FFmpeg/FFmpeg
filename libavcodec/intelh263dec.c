/*
 * H.263i decoder
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

#include "codec_internal.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "mpegvideodec.h"
#include "h263data.h"
#include "h263dec.h"
#include "mpegvideodata.h"

/* don't understand why they choose a different header ! */
int ff_intel_h263_decode_picture_header(MpegEncContext *s)
{
    int format;

    if (get_bits_left(&s->gb) == 64) { /* special dummy frames */
        return FRAME_SKIPPED;
    }

    /* picture header */
    if (get_bits(&s->gb, 22) != 0x20) {
        av_log(s->avctx, AV_LOG_ERROR, "Bad picture start code\n");
        return -1;
    }
    s->picture_number = get_bits(&s->gb, 8); /* picture timestamp */

    if (check_marker(s->avctx, &s->gb, "after picture_number") != 1) {
        return -1;      /* marker */
    }
    if (get_bits1(&s->gb) != 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Bad H.263 id\n");
        return -1;      /* H.263 id */
    }
    skip_bits1(&s->gb);         /* split screen off */
    skip_bits1(&s->gb);         /* camera  off */
    skip_bits1(&s->gb);         /* freeze picture release off */

    format = get_bits(&s->gb, 3);
    if (format == 0 || format == 6) {
        av_log(s->avctx, AV_LOG_ERROR, "Intel H.263 free format not supported\n");
        return -1;
    }
    s->h263_plus = 0;

    s->pict_type = AV_PICTURE_TYPE_I + get_bits1(&s->gb);

    s->h263_long_vectors = get_bits1(&s->gb);

    if (get_bits1(&s->gb) != 0) {
        av_log(s->avctx, AV_LOG_ERROR, "SAC not supported\n");
        return -1;      /* SAC: off */
    }
    s->obmc= get_bits1(&s->gb);
    s->pb_frame = get_bits1(&s->gb);

    if (format < 6) {
        s->width = ff_h263_format[format][0];
        s->height = ff_h263_format[format][1];
        s->avctx->sample_aspect_ratio.num = 12;
        s->avctx->sample_aspect_ratio.den = 11;
    } else {
        format = get_bits(&s->gb, 3);
        if(format == 0 || format == 7){
            av_log(s->avctx, AV_LOG_ERROR, "Wrong Intel H.263 format\n");
            return -1;
        }
        if(get_bits(&s->gb, 2))
            av_log(s->avctx, AV_LOG_ERROR, "Bad value for reserved field\n");
        s->loop_filter = get_bits1(&s->gb) * !s->avctx->lowres;
        if(get_bits1(&s->gb))
            av_log(s->avctx, AV_LOG_ERROR, "Bad value for reserved field\n");
        if(get_bits1(&s->gb))
            s->pb_frame = 2;
        if(get_bits(&s->gb, 5))
            av_log(s->avctx, AV_LOG_ERROR, "Bad value for reserved field\n");
        if(get_bits(&s->gb, 5) != 1)
            av_log(s->avctx, AV_LOG_ERROR, "Invalid marker\n");
    }
    if(format == 6){
        int ar = get_bits(&s->gb, 4);
        skip_bits(&s->gb, 9); // display width
        check_marker(s->avctx, &s->gb, "in dimensions");
        skip_bits(&s->gb, 9); // display height
        if(ar == 15){
            s->avctx->sample_aspect_ratio.num = get_bits(&s->gb, 8); // aspect ratio - width
            s->avctx->sample_aspect_ratio.den = get_bits(&s->gb, 8); // aspect ratio - height
        } else {
            s->avctx->sample_aspect_ratio = ff_h263_pixel_aspect[ar];
        }
        if (s->avctx->sample_aspect_ratio.num == 0)
            av_log(s->avctx, AV_LOG_ERROR, "Invalid aspect ratio.\n");
    }

    s->chroma_qscale= s->qscale = get_bits(&s->gb, 5);
    skip_bits1(&s->gb); /* Continuous Presence Multipoint mode: off */

    if(s->pb_frame){
        skip_bits(&s->gb, 3); //temporal reference for B-frame
        skip_bits(&s->gb, 2); //dbquant
    }

    /* PEI */
    if (skip_1stop_8data_bits(&s->gb) < 0)
        return AVERROR_INVALIDDATA;
    s->f_code = 1;

    ff_h263_show_pict_info(s);

    return 0;
}

const FFCodec ff_h263i_decoder = {
    .p.name         = "h263i",
    CODEC_LONG_NAME("Intel H.263"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_H263I,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_h263_decode_init,
    .close          = ff_h263_decode_end,
    FF_CODEC_DECODE_CB(ff_h263_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DRAW_HORIZ_BAND | AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    .p.pix_fmts     = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE
    },
};
