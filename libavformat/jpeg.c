/*
 * JPEG image format
 * Copyright (c) 2003 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avformat.h"

static int jpeg_probe(AVProbeData *pd)
{
    if (pd->buf_size >= 64 &&
        pd->buf[0] == 0xff && pd->buf[1] == 0xd8 && pd->buf[2] == 0xff)
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

typedef struct JpegOpaque {
    int (*alloc_cb)(void *opaque, AVImageInfo *info);
    void *opaque;
    int ret_code;
} JpegOpaque;

/* called by the codec to allocate the image */
static int jpeg_get_buffer(AVCodecContext *c, AVFrame *picture)
{
    JpegOpaque *jctx = c->opaque;
    AVImageInfo info1, *info = &info1;
    int ret, i;

    info->width = c->width;
    info->height = c->height;
    switch(c->pix_fmt) {
    case PIX_FMT_YUV420P:
        info->pix_fmt = PIX_FMT_YUVJ420P;
        break;
    case PIX_FMT_YUV422P:
        info->pix_fmt = PIX_FMT_YUVJ422P;
        break;
    case PIX_FMT_YUV444P:
        info->pix_fmt = PIX_FMT_YUVJ444P;
        break;
    default:
        return -1;
    }
    ret = jctx->alloc_cb(jctx->opaque, info);
    if (ret) {
        jctx->ret_code = ret;
        return -1;
    } else {
        for(i=0;i<3;i++) {
            picture->data[i] = info->pict.data[i];
            picture->linesize[i] = info->pict.linesize[i];
        }
        return 0;
    }
}

static void jpeg_img_copy(uint8_t *dst, int dst_wrap, 
                     uint8_t *src, int src_wrap,
                     int width, int height)
{
    for(;height > 0; height--) {
        memcpy(dst, src, width);
        dst += dst_wrap;
        src += src_wrap;
    }
}

/* XXX: libavcodec is broken for truncated jpegs! */
#define IO_BUF_SIZE (1024*1024)

static int jpeg_read(ByteIOContext *f, 
                     int (*alloc_cb)(void *opaque, AVImageInfo *info), void *opaque)
{
    AVCodecContext *c;
    AVFrame *picture, picture1;
    int len, size, got_picture, i;
    uint8_t *inbuf_ptr, inbuf[IO_BUF_SIZE];
    JpegOpaque jctx;

    jctx.alloc_cb = alloc_cb;
    jctx.opaque = opaque;
    jctx.ret_code = -1; /* default return code is error */
    
    c = avcodec_alloc_context();
    if (!c)
        return -1;
    picture= avcodec_alloc_frame();
    if (!picture) {
        av_free(c);
        return -1;
    }
    c->opaque = &jctx;
    c->get_buffer = jpeg_get_buffer;
    c->flags |= CODEC_FLAG_TRUNCATED; /* we dont send complete frames */
    if (avcodec_open(c, &mjpeg_decoder) < 0)
        goto fail1;
    for(;;) {
        size = get_buffer(f, inbuf, sizeof(inbuf));
        if (size == 0)
            break;
        inbuf_ptr = inbuf;
        while (size > 0) {
            len = avcodec_decode_video(c, &picture1, &got_picture, 
                                       inbuf_ptr, size);
            if (len < 0)
                goto fail;
            if (got_picture)
                goto the_end;
            size -= len;
            inbuf_ptr += len;
        }
    }
 the_end:
    /* XXX: currently, the mjpeg decoder does not use AVFrame, so we
       must do it by hand */
    if (jpeg_get_buffer(c, picture) < 0)
        goto fail;
    for(i=0;i<3;i++) {
        int w, h;
        w = c->width;
        h = c->height;
        if (i >= 1) {
            switch(c->pix_fmt) {
            default:
            case PIX_FMT_YUV420P:
                w = (w + 1) >> 1;
                h = (h + 1) >> 1;
                break;
            case PIX_FMT_YUV422P:
                w = (w + 1) >> 1;
                break;
            case PIX_FMT_YUV444P:
                break;
            }
        }
        jpeg_img_copy(picture->data[i], picture->linesize[i],
                 picture1.data[i], picture1.linesize[i],
                 w, h);
    }
    jctx.ret_code = 0;
 fail:
    avcodec_close(c);
 fail1:
    av_free(picture);
    av_free(c);
    return jctx.ret_code;
}

#ifdef CONFIG_ENCODERS
static int jpeg_write(ByteIOContext *pb, AVImageInfo *info)
{
    AVCodecContext *c;
    uint8_t *outbuf = NULL;
    int outbuf_size, ret, size, i;
    AVFrame *picture;

    ret = -1;
    c = avcodec_alloc_context();
    if (!c)
        return -1;
    picture = avcodec_alloc_frame();
    if (!picture)
        goto fail2;
    c->width = info->width;
    c->height = info->height;
    /* XXX: currently move that to the codec ? */
    switch(info->pix_fmt) {
    case PIX_FMT_YUVJ420P:
        c->pix_fmt = PIX_FMT_YUV420P;
        break;
    case PIX_FMT_YUVJ422P:
        c->pix_fmt = PIX_FMT_YUV422P;
        break;
    case PIX_FMT_YUVJ444P:
        c->pix_fmt = PIX_FMT_YUV444P;
        break;
    default:
        goto fail1;
    }
    for(i=0;i<3;i++) {
        picture->data[i] = info->pict.data[i];
        picture->linesize[i] = info->pict.linesize[i];
    }
    /* set the quality */
    picture->quality = 3; /* XXX: a parameter should be used */
    c->flags |= CODEC_FLAG_QSCALE;
    
    if (avcodec_open(c, &mjpeg_encoder) < 0)
        goto fail1;
    
    /* XXX: needs to sort out that size problem */
    outbuf_size = 1000000;
    outbuf = av_malloc(outbuf_size);

    size = avcodec_encode_video(c, outbuf, outbuf_size, picture);
    if (size < 0)
        goto fail;
    put_buffer(pb, outbuf, size);
    put_flush_packet(pb);
    ret = 0;

 fail:
    avcodec_close(c);
    av_free(outbuf);
 fail1:
    av_free(picture);
 fail2:
    av_free(c);
    return ret;
}
#endif //CONFIG_ENCODERS

AVImageFormat jpeg_image_format = {
    "jpeg",
    "jpg,jpeg",
    jpeg_probe,
    jpeg_read,
    (1 << PIX_FMT_YUVJ420P) | (1 << PIX_FMT_YUVJ422P) | (1 << PIX_FMT_YUVJ444P),
#ifdef CONFIG_ENCODERS
    jpeg_write,
#else
    NULL,
#endif //CONFIG_ENCODERS
};
