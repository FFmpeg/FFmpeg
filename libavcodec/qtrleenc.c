/*
 * Quicktime Animation (RLE) Video Encoder
 * Copyright (C) 2007 Clemens Fruhwirth
 * Copyright (C) 2007 Alexis Ballier
 *
 * This file is based on flashsvenc.c.
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

#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "encode.h"

/** Maximum RLE code for bulk copy */
#define MAX_RLE_BULK   127
/** Maximum RLE code for repeat */
#define MAX_RLE_REPEAT 128
/** Maximum RLE code for skip */
#define MAX_RLE_SKIP   254

typedef struct QtrleEncContext {
    AVCodecContext *avctx;
    int pixel_size;
    AVFrame *previous_frame;
    unsigned int max_buf_size;
    int logical_width;
    /**
     * This array will contain at ith position the value of the best RLE code
     * if the line started at pixel i
     * There can be 3 values :
     * skip (0)     : skip as much as possible pixels because they are equal to the
     *                previous frame ones
     * repeat (<-1) : repeat that pixel -rle_code times, still as much as
     *                possible
     * copy (>0)    : copy the raw next rle_code pixels */
    signed char *rlecode_table;
    /**
     * This array will contain the length of the best rle encoding of the line
     * starting at ith pixel */
    int *length_table;
    /**
     * Will contain at ith position the number of consecutive pixels equal to the previous
     * frame starting from pixel i */
    uint8_t* skip_table;

    /** Encoded frame is a key frame */
    int key_frame;
} QtrleEncContext;

static av_cold int qtrle_encode_end(AVCodecContext *avctx)
{
    QtrleEncContext *s = avctx->priv_data;

    av_frame_free(&s->previous_frame);
    av_free(s->rlecode_table);
    av_free(s->length_table);
    av_free(s->skip_table);
    return 0;
}

static av_cold int qtrle_encode_init(AVCodecContext *avctx)
{
    QtrleEncContext *s = avctx->priv_data;

    if (av_image_check_size(avctx->width, avctx->height, 0, avctx) < 0) {
        return AVERROR(EINVAL);
    }
    s->avctx=avctx;
    s->logical_width=avctx->width;

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_GRAY8:
        if (avctx->width % 4) {
            av_log(avctx, AV_LOG_ERROR, "Width not being a multiple of 4 is not supported\n");
            return AVERROR(EINVAL);
        }
        s->logical_width = avctx->width / 4;
        s->pixel_size = 4;
        break;
    case AV_PIX_FMT_RGB555BE:
        s->pixel_size = 2;
        break;
    case AV_PIX_FMT_RGB24:
        s->pixel_size = 3;
        break;
    case AV_PIX_FMT_ARGB:
        s->pixel_size = 4;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported colorspace.\n");
        break;
    }
    avctx->bits_per_coded_sample = avctx->pix_fmt == AV_PIX_FMT_GRAY8 ? 40 : s->pixel_size*8;

    s->rlecode_table = av_mallocz(s->logical_width);
    s->skip_table    = av_mallocz(s->logical_width);
    s->length_table  = av_calloc(s->logical_width + 1, sizeof(*s->length_table));
    if (!s->skip_table || !s->length_table || !s->rlecode_table) {
        av_log(avctx, AV_LOG_ERROR, "Error allocating memory.\n");
        return AVERROR(ENOMEM);
    }
    s->previous_frame = av_frame_alloc();
    if (!s->previous_frame) {
        av_log(avctx, AV_LOG_ERROR, "Error allocating picture\n");
        return AVERROR(ENOMEM);
    }

    s->max_buf_size = s->logical_width*s->avctx->height*s->pixel_size*2 /* image base material */
                      + 15                                            /* header + footer */
                      + s->avctx->height*2                            /* skip code+rle end */
                      + s->logical_width/MAX_RLE_BULK + 1             /* rle codes */;

    return 0;
}

/**
 * Compute the best RLE sequence for a line
 */
static void qtrle_encode_line(QtrleEncContext *s, const AVFrame *p, int line, uint8_t **buf)
{
    int width=s->logical_width;
    int i;
    signed char rlecode;

    /* This will be the number of pixels equal to the previous frame one's
     * starting from the ith pixel */
    unsigned int skipcount;
    /* This will be the number of consecutive equal pixels in the current
     * frame, starting from the ith one also */
    unsigned int av_uninit(repeatcount);

    /* The cost of the three different possibilities */
    int total_skip_cost;
    int total_repeat_cost;

    int base_bulk_cost;
    int lowest_bulk_cost;
    int lowest_bulk_cost_index;
    int sec_lowest_bulk_cost;
    int sec_lowest_bulk_cost_index;

    const uint8_t *this_line = p->data[0] + line * p->linesize[0] + width * s->pixel_size;
    /* There might be no earlier frame if the current frame is a keyframe.
     * So just use a pointer to the current frame to avoid a check
     * to avoid NULL - s->pixel_size (which is undefined behaviour). */
    const uint8_t *prev_line = s->key_frame ? this_line
                                            : s->previous_frame->data[0]
                                              + line * s->previous_frame->linesize[0]
                                              + width * s->pixel_size;

    s->length_table[width] = 0;
    skipcount = 0;

    /* Initial values */
    lowest_bulk_cost = INT_MAX / 2;
    lowest_bulk_cost_index = width;
    sec_lowest_bulk_cost = INT_MAX / 2;
    sec_lowest_bulk_cost_index = width;

    base_bulk_cost = 1 + s->pixel_size;

    for (i = width - 1; i >= 0; i--) {

        int prev_bulk_cost;

        this_line -= s->pixel_size;
        prev_line -= s->pixel_size;

        /* If our lowest bulk cost index is too far away, replace it
         * with the next lowest bulk cost */
        if (FFMIN(width, i + MAX_RLE_BULK) < lowest_bulk_cost_index) {
            lowest_bulk_cost = sec_lowest_bulk_cost;
            lowest_bulk_cost_index = sec_lowest_bulk_cost_index;

            sec_lowest_bulk_cost = INT_MAX / 2;
            sec_lowest_bulk_cost_index = width;
        }

        /* Deal with the first pixel's bulk cost */
        if (!i) {
            base_bulk_cost++;
            lowest_bulk_cost++;
            sec_lowest_bulk_cost++;
        }

        /* Look at the bulk cost of the previous loop and see if it is
         * a new lower bulk cost */
        prev_bulk_cost = s->length_table[i + 1] + base_bulk_cost;
        if (prev_bulk_cost <= sec_lowest_bulk_cost) {
            /* If it's lower than the 2nd lowest, then it may be lower
             * than the lowest */
            if (prev_bulk_cost <= lowest_bulk_cost) {

                /* If we have found a new lowest bulk cost,
                 * then the 2nd lowest bulk cost is now farther than the
                 * lowest bulk cost, and will never be used */
                sec_lowest_bulk_cost = INT_MAX / 2;

                lowest_bulk_cost = prev_bulk_cost;
                lowest_bulk_cost_index = i + 1;
            } else {
                /* Then it must be the 2nd lowest bulk cost */
                sec_lowest_bulk_cost = prev_bulk_cost;
                sec_lowest_bulk_cost_index = i + 1;
            }
        }

        if (!s->key_frame && !memcmp(this_line, prev_line, s->pixel_size))
            skipcount = FFMIN(skipcount + 1, MAX_RLE_SKIP);
        else
            skipcount = 0;

        total_skip_cost  = s->length_table[i + skipcount] + 2;
        s->skip_table[i] = skipcount;


        if (i < width - 1 && !memcmp(this_line, this_line + s->pixel_size, s->pixel_size))
            repeatcount = FFMIN(repeatcount + 1, MAX_RLE_REPEAT);
        else
            repeatcount = 1;

        total_repeat_cost = s->length_table[i + repeatcount] + 1 + s->pixel_size;

        /* skip code is free for the first pixel, it costs one byte for repeat and bulk copy
         * so let's make it aware */
        if (i == 0) {
            total_skip_cost--;
            total_repeat_cost++;
        }

        if (repeatcount > 1 && (skipcount == 0 || total_repeat_cost < total_skip_cost)) {
            /* repeat is the best */
            s->length_table[i]  = total_repeat_cost;
            s->rlecode_table[i] = -repeatcount;
        }
        else if (skipcount > 0) {
            /* skip is the best choice here */
            s->length_table[i]  = total_skip_cost;
            s->rlecode_table[i] = 0;
        }
        else {
            /* We cannot do neither skip nor repeat
             * thus we use the best bulk copy  */

            s->length_table[i]  = lowest_bulk_cost;
            s->rlecode_table[i] = lowest_bulk_cost_index - i;

        }

        /* These bulk costs increase every iteration */
        lowest_bulk_cost += s->pixel_size;
        sec_lowest_bulk_cost += s->pixel_size;
    }

    /* Good! Now we have the best sequence for this line, let's output it. */

    /* We do a special case for the first pixel so that we avoid testing it in
     * the whole loop */

    i=0;
    this_line = p->               data[0] + line*p->linesize[0];

    if (s->rlecode_table[0] == 0) {
        bytestream_put_byte(buf, s->skip_table[0] + 1);
        i += s->skip_table[0];
    }
    else bytestream_put_byte(buf, 1);


    while (i < width) {
        rlecode = s->rlecode_table[i];
        bytestream_put_byte(buf, rlecode);
        if (rlecode == 0) {
            /* Write a skip sequence */
            bytestream_put_byte(buf, s->skip_table[i] + 1);
            i += s->skip_table[i];
        }
        else if (rlecode > 0) {
            /* bulk copy */
            if (s->avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
                int j;
                // QT grayscale colorspace has 0=white and 255=black, we will
                // ignore the palette that is included in the AVFrame because
                // AV_PIX_FMT_GRAY8 has defined color mapping
                for (j = 0; j < rlecode*s->pixel_size; ++j)
                    bytestream_put_byte(buf, *(this_line + i*s->pixel_size + j) ^ 0xff);
            } else {
                bytestream_put_buffer(buf, this_line + i*s->pixel_size, rlecode*s->pixel_size);
            }
            i += rlecode;
        }
        else {
            /* repeat the bits */
            if (s->avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
                int j;
                // QT grayscale colorspace has 0=white and 255=black, ...
                for (j = 0; j < s->pixel_size; ++j)
                    bytestream_put_byte(buf, *(this_line + i*s->pixel_size + j) ^ 0xff);
            } else {
                bytestream_put_buffer(buf, this_line + i*s->pixel_size, s->pixel_size);
            }
            i -= rlecode;
        }
    }
    bytestream_put_byte(buf, -1); // end RLE line
}

/** Encode frame including header */
static int encode_frame(QtrleEncContext *s, const AVFrame *p, uint8_t *buf)
{
    int i;
    int start_line = 0;
    int end_line = s->avctx->height;
    uint8_t *orig_buf = buf;

    if (!s->key_frame) {
        unsigned line_size = s->logical_width * s->pixel_size;
        for (start_line = 0; start_line < s->avctx->height; start_line++)
            if (memcmp(p->data[0] + start_line*p->linesize[0],
                       s->previous_frame->data[0] + start_line * s->previous_frame->linesize[0],
                       line_size))
                break;

        for (end_line=s->avctx->height; end_line > start_line; end_line--)
            if (memcmp(p->data[0] + (end_line - 1)*p->linesize[0],
                       s->previous_frame->data[0] + (end_line - 1) * s->previous_frame->linesize[0],
                       line_size))
                break;
    }

    bytestream_put_be32(&buf, 0);                         // CHUNK SIZE, patched later

    if ((start_line == 0 && end_line == s->avctx->height) || start_line == s->avctx->height)
        bytestream_put_be16(&buf, 0);                     // header
    else {
        bytestream_put_be16(&buf, 8);                     // header
        bytestream_put_be16(&buf, start_line);            // starting line
        bytestream_put_be16(&buf, 0);                     // unknown
        bytestream_put_be16(&buf, end_line - start_line); // lines to update
        bytestream_put_be16(&buf, 0);                     // unknown
    }
    for (i = start_line; i < end_line; i++)
        qtrle_encode_line(s, p, i, &buf);

    bytestream_put_byte(&buf, 0);                         // zero skip code = frame finished
    AV_WB32(orig_buf, buf - orig_buf);                    // patch the chunk size
    return buf - orig_buf;
}

static int qtrle_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                              const AVFrame *pict, int *got_packet)
{
    QtrleEncContext * const s = avctx->priv_data;
    int ret;

    if ((ret = ff_alloc_packet(avctx, pkt, s->max_buf_size)) < 0)
        return ret;

    if (avctx->gop_size == 0 || !s->previous_frame->data[0] ||
        (s->avctx->frame_num % avctx->gop_size) == 0) {
        /* I-Frame */
        s->key_frame = 1;
    } else {
        /* P-Frame */
        s->key_frame = 0;
    }

    pkt->size = encode_frame(s, pict, pkt->data);

    /* save the current frame */
    ret = av_frame_replace(s->previous_frame, pict);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "cannot add reference\n");
        return ret;
    }

    if (s->key_frame)
        pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

const FFCodec ff_qtrle_encoder = {
    .p.name         = "qtrle",
    CODEC_LONG_NAME("QuickTime Animation (RLE) video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_QTRLE,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .priv_data_size = sizeof(QtrleEncContext),
    .init           = qtrle_encode_init,
    FF_CODEC_ENCODE_CB(qtrle_encode_frame),
    .close          = qtrle_encode_end,
    .p.pix_fmts     = (const enum AVPixelFormat[]){
        AV_PIX_FMT_RGB24, AV_PIX_FMT_RGB555BE, AV_PIX_FMT_ARGB, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE
    },
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
