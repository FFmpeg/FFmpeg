/*
 * SGI image decoder
 * Todd Kirby <doubleshot@pacbell.net>
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
#include "avcodec.h"
#include "bytestream.h"
#include "sgi.h"

typedef struct SgiState {
    AVFrame picture;
    unsigned int width;
    unsigned int height;
    unsigned int depth;
    unsigned int bytes_per_channel;
    int linesize;
} SgiState;

/**
 * Expand an RLE row into a channel.
 * @param in_buf input buffer
 * @param in_end end of input buffer
 * @param out_buf Points to one line after the output buffer.
 * @param out_end end of line in output buffer
 * @param pixelstride pixel stride of input buffer
 * @return size of output in bytes, -1 if buffer overflows
 */
static int expand_rle_row(const uint8_t *in_buf, const uint8_t* in_end,
            unsigned char *out_buf, uint8_t* out_end, int pixelstride)
{
    unsigned char pixel, count;
    unsigned char *orig = out_buf;

    while (1) {
        if(in_buf + 1 > in_end) return -1;
        pixel = bytestream_get_byte(&in_buf);
        if (!(count = (pixel & 0x7f))) {
            return (out_buf - orig) / pixelstride;
        }

        /* Check for buffer overflow. */
        if(out_buf + pixelstride * count >= out_end) return -1;

        if (pixel & 0x80) {
            while (count--) {
                *out_buf = bytestream_get_byte(&in_buf);
                out_buf += pixelstride;
            }
        } else {
            pixel = bytestream_get_byte(&in_buf);

            while (count--) {
                *out_buf = pixel;
                out_buf += pixelstride;
            }
        }
    }
}

/**
 * Read a run length encoded SGI image.
 * @param out_buf output buffer
 * @param in_buf input buffer
 * @param in_end end of input buffer
 * @param s the current image state
 * @return 0 if no error, else return error number.
 */
static int read_rle_sgi(unsigned char* out_buf, const uint8_t *in_buf,
                        const uint8_t *in_end, SgiState* s)
{
    uint8_t *dest_row;
    unsigned int len = s->height * s->depth * 4;
    const uint8_t *start_table = in_buf;
    unsigned int y, z;
    unsigned int start_offset;

    /* size of  RLE offset and length tables */
    if(len * 2  > in_end - in_buf) {
        return AVERROR_INVALIDDATA;
    }

    in_buf -= SGI_HEADER_SIZE;
    for (z = 0; z < s->depth; z++) {
        dest_row = out_buf;
        for (y = 0; y < s->height; y++) {
            dest_row -= s->linesize;
            start_offset = bytestream_get_be32(&start_table);
            if(start_offset > in_end - in_buf) {
                return AVERROR_INVALIDDATA;
            }
            if (expand_rle_row(in_buf + start_offset, in_end, dest_row + z,
                dest_row + FFABS(s->linesize), s->depth) != s->width)
                return AVERROR_INVALIDDATA;
        }
    }
    return 0;
}

/**
 * Read an uncompressed SGI image.
 * @param out_buf output buffer
 * @param out_end end ofoutput buffer
 * @param in_buf input buffer
 * @param in_end end of input buffer
 * @param s the current image state
 * @return 0 if read success, otherwise return -1.
 */
static int read_uncompressed_sgi(unsigned char* out_buf, uint8_t* out_end,
                const uint8_t *in_buf, const uint8_t *in_end, SgiState* s)
{
    int x, y, z;
    const uint8_t *ptr;
    unsigned int offset = s->height * s->width * s->bytes_per_channel;

    /* Test buffer size. */
    if (offset * s->depth > in_end - in_buf) {
       return -1;
    }

    for (y = s->height - 1; y >= 0; y--) {
        out_end = out_buf + (y * s->linesize);
        for (x = s->width; x > 0; x--) {
            ptr = in_buf += s->bytes_per_channel;
            for(z = 0; z < s->depth; z ++) {
                memcpy(out_end, ptr, s->bytes_per_channel);
                out_end += s->bytes_per_channel;
                ptr += offset;
            }
        }
    }
    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    const uint8_t *in_buf = avpkt->data;
    int buf_size = avpkt->size;
    SgiState *s = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame *p = &s->picture;
    const uint8_t *in_end = in_buf + buf_size;
    unsigned int dimension, rle;
    int ret = 0;
    uint8_t *out_buf, *out_end;

    if (buf_size < SGI_HEADER_SIZE){
        av_log(avctx, AV_LOG_ERROR, "buf_size too small (%d)\n", buf_size);
        return -1;
    }

    /* Test for SGI magic. */
    if (bytestream_get_be16(&in_buf) != SGI_MAGIC) {
        av_log(avctx, AV_LOG_ERROR, "bad magic number\n");
        return -1;
    }

    rle = bytestream_get_byte(&in_buf);
    s->bytes_per_channel = bytestream_get_byte(&in_buf);
    dimension = bytestream_get_be16(&in_buf);
    s->width  = bytestream_get_be16(&in_buf);
    s->height = bytestream_get_be16(&in_buf);
    s->depth  = bytestream_get_be16(&in_buf);

    if (s->bytes_per_channel != 1 && (s->bytes_per_channel != 2 || rle)) {
        av_log(avctx, AV_LOG_ERROR, "wrong channel number\n");
        return -1;
    }

    /* Check for supported image dimensions. */
    if (dimension != 2 && dimension != 3) {
        av_log(avctx, AV_LOG_ERROR, "wrong dimension number\n");
        return -1;
    }

    if (s->depth == SGI_GRAYSCALE) {
        avctx->pix_fmt = s->bytes_per_channel == 2 ? PIX_FMT_GRAY16BE : PIX_FMT_GRAY8;
    } else if (s->depth == SGI_RGB) {
        avctx->pix_fmt = s->bytes_per_channel == 2 ? PIX_FMT_RGB48BE : PIX_FMT_RGB24;
    } else if (s->depth == SGI_RGBA && s->bytes_per_channel == 1) {
        avctx->pix_fmt = PIX_FMT_RGBA;
    } else {
        av_log(avctx, AV_LOG_ERROR, "wrong picture format\n");
        return -1;
    }

    if (av_image_check_size(s->width, s->height, 0, avctx))
        return -1;
    avcodec_set_dimensions(avctx, s->width, s->height);

    if (p->data[0])
        avctx->release_buffer(avctx, p);

    p->reference = 0;
    if (avctx->get_buffer(avctx, p) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed.\n");
        return -1;
    }

    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;
    out_buf = p->data[0];

    out_end = out_buf + p->linesize[0] * s->height;

    s->linesize = p->linesize[0];

    /* Skip header. */
    in_buf += SGI_HEADER_SIZE - 12;
    if (rle) {
        ret = read_rle_sgi(out_end, in_buf, in_end, s);
    } else {
        ret = read_uncompressed_sgi(out_buf, out_end, in_buf, in_end, s);
    }

    if (ret == 0) {
        *picture   = s->picture;
        *data_size = sizeof(AVPicture);
        return buf_size;
    } else {
        return -1;
    }
}

static av_cold int sgi_init(AVCodecContext *avctx){
    SgiState *s = avctx->priv_data;

    avcodec_get_frame_defaults(&s->picture);
    avctx->coded_frame = &s->picture;

    return 0;
}

static av_cold int sgi_end(AVCodecContext *avctx)
{
    SgiState * const s = avctx->priv_data;

    if (s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    return 0;
}

AVCodec ff_sgi_decoder = {
    "sgi",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_SGI,
    sizeof(SgiState),
    sgi_init,
    NULL,
    sgi_end,
    decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("SGI image"),
};

