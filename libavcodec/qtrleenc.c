/*
 * Quicktime Animation (RLE) Video Encoder
 * Copyright (C) 2007 Clemens Fruhwirth
 * Copyright (C) 2007 Alexis Ballier
 *
 * This file is based on flashsvenc.c.
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

#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"

/** Maximum RLE code for bulk copy */
#define MAX_RLE_BULK   127
/** Maximum RLE code for repeat */
#define MAX_RLE_REPEAT 128
/** Maximum RLE code for skip */
#define MAX_RLE_SKIP   254

typedef struct QtrleEncContext {
    AVCodecContext *avctx;
    AVFrame frame;
    int pixel_size;
    AVPicture previous_frame;
    unsigned int max_buf_size;
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
} QtrleEncContext;

static av_cold int qtrle_encode_init(AVCodecContext *avctx)
{
    QtrleEncContext *s = avctx->priv_data;

    if (av_image_check_size(avctx->width, avctx->height, 0, avctx) < 0) {
        return -1;
    }
    s->avctx=avctx;

    switch (avctx->pix_fmt) {
    case PIX_FMT_RGB555BE:
        s->pixel_size = 2;
        break;
    case PIX_FMT_RGB24:
        s->pixel_size = 3;
        break;
    case PIX_FMT_ARGB:
        s->pixel_size = 4;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported colorspace.\n");
        break;
    }
    avctx->bits_per_coded_sample = s->pixel_size*8;

    s->rlecode_table = av_mallocz(s->avctx->width);
    s->skip_table    = av_mallocz(s->avctx->width);
    s->length_table  = av_mallocz((s->avctx->width + 1)*sizeof(int));
    if (!s->skip_table || !s->length_table || !s->rlecode_table) {
        av_log(avctx, AV_LOG_ERROR, "Error allocating memory.\n");
        return -1;
    }
    if (avpicture_alloc(&s->previous_frame, avctx->pix_fmt, avctx->width, avctx->height) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error allocating picture\n");
        return -1;
    }

    s->max_buf_size = s->avctx->width*s->avctx->height*s->pixel_size /* image base material */
                      + 15                                           /* header + footer */
                      + s->avctx->height*2                           /* skip code+rle end */
                      + s->avctx->width/MAX_RLE_BULK + 1             /* rle codes */;
    avctx->coded_frame = &s->frame;
    return 0;
}

/**
 * Compute the best RLE sequence for a line
 */
static void qtrle_encode_line(QtrleEncContext *s, AVFrame *p, int line, uint8_t **buf)
{
    int width=s->avctx->width;
    int i;
    signed char rlecode;

    /* We will use it to compute the best bulk copy sequence */
    unsigned int bulkcount;
    /* This will be the number of pixels equal to the preivous frame one's
     * starting from the ith pixel */
    unsigned int skipcount;
    /* This will be the number of consecutive equal pixels in the current
     * frame, starting from the ith one also */
    unsigned int av_uninit(repeatcount);

    /* The cost of the three different possibilities */
    int total_bulk_cost;
    int total_skip_cost;
    int total_repeat_cost;

    int temp_cost;
    int j;

    uint8_t *this_line = p->               data[0] + line*p->               linesize[0] +
        (width - 1)*s->pixel_size;
    uint8_t *prev_line = s->previous_frame.data[0] + line*s->previous_frame.linesize[0] +
        (width - 1)*s->pixel_size;

    s->length_table[width] = 0;
    skipcount = 0;

    for (i = width - 1; i >= 0; i--) {

        if (!s->frame.key_frame && !memcmp(this_line, prev_line, s->pixel_size))
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
             * thus we search for the best bulk copy to do */

            int limit = FFMIN(width - i, MAX_RLE_BULK);

            temp_cost = 1 + s->pixel_size + !i;
            total_bulk_cost = INT_MAX;

            for (j = 1; j <= limit; j++) {
                if (s->length_table[i + j] + temp_cost < total_bulk_cost) {
                    /* We have found a better bulk copy ... */
                    total_bulk_cost = s->length_table[i + j] + temp_cost;
                    bulkcount = j;
                }
                temp_cost += s->pixel_size;
            }

            s->length_table[i]  = total_bulk_cost;
            s->rlecode_table[i] = bulkcount;
        }

        this_line -= s->pixel_size;
        prev_line -= s->pixel_size;
    }

    /* Good ! Now we have the best sequence for this line, let's ouput it */

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
            bytestream_put_buffer(buf, this_line + i*s->pixel_size, rlecode*s->pixel_size);
            i += rlecode;
        }
        else {
            /* repeat the bits */
            bytestream_put_buffer(buf, this_line + i*s->pixel_size, s->pixel_size);
            i -= rlecode;
        }
    }
    bytestream_put_byte(buf, -1); // end RLE line
}

/** Encode frame including header */
static int encode_frame(QtrleEncContext *s, AVFrame *p, uint8_t *buf)
{
    int i;
    int start_line = 0;
    int end_line = s->avctx->height;
    uint8_t *orig_buf = buf;

    if (!s->frame.key_frame) {
        unsigned line_size = s->avctx->width * s->pixel_size;
        for (start_line = 0; start_line < s->avctx->height; start_line++)
            if (memcmp(p->data[0] + start_line*p->linesize[0],
                       s->previous_frame.data[0] + start_line*s->previous_frame.linesize[0],
                       line_size))
                break;

        for (end_line=s->avctx->height; end_line > start_line; end_line--)
            if (memcmp(p->data[0] + (end_line - 1)*p->linesize[0],
                       s->previous_frame.data[0] + (end_line - 1)*s->previous_frame.linesize[0],
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

static int qtrle_encode_frame(AVCodecContext *avctx, uint8_t *buf, int buf_size, void *data)
{
    QtrleEncContext * const s = avctx->priv_data;
    AVFrame *pict = data;
    AVFrame * const p = &s->frame;
    int chunksize;

    *p = *pict;

    if (buf_size < s->max_buf_size) {
        /* Upper bound check for compressed data */
        av_log(avctx, AV_LOG_ERROR, "buf_size %d <  %d\n", buf_size, s->max_buf_size);
        return -1;
    }

    if (avctx->gop_size == 0 || (s->avctx->frame_number % avctx->gop_size) == 0) {
        /* I-Frame */
        p->pict_type = AV_PICTURE_TYPE_I;
        p->key_frame = 1;
    } else {
        /* P-Frame */
        p->pict_type = AV_PICTURE_TYPE_P;
        p->key_frame = 0;
    }

    chunksize = encode_frame(s, pict, buf);

    /* save the current frame */
    av_picture_copy(&s->previous_frame, (AVPicture *)p, avctx->pix_fmt, avctx->width, avctx->height);
    return chunksize;
}

static av_cold int qtrle_encode_end(AVCodecContext *avctx)
{
    QtrleEncContext *s = avctx->priv_data;

    avpicture_free(&s->previous_frame);
    av_free(s->rlecode_table);
    av_free(s->length_table);
    av_free(s->skip_table);
    return 0;
}

AVCodec ff_qtrle_encoder = {
    .name           = "qtrle",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_QTRLE,
    .priv_data_size = sizeof(QtrleEncContext),
    .init           = qtrle_encode_init,
    .encode         = qtrle_encode_frame,
    .close          = qtrle_encode_end,
    .pix_fmts = (const enum PixelFormat[]){PIX_FMT_RGB24, PIX_FMT_RGB555BE, PIX_FMT_ARGB, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("QuickTime Animation (RLE) video"),
};
