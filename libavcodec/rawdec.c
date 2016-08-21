/*
 * Raw Video Decoder
 * Copyright (c) 2001 Fabrice Bellard
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
 * Raw Video Decoder
 */

#include "avcodec.h"
#include "bswapdsp.h"
#include "get_bits.h"
#include "internal.h"
#include "raw.h"
#include "libavutil/avassert.h"
#include "libavutil/buffer.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"

typedef struct RawVideoContext {
    AVClass *av_class;
    AVBufferRef *palette;
    int frame_size;  /* size of the frame in bytes */
    int flip;
    int is_1_2_4_8_bpp; // 1, 2, 4 and 8 bpp in avi/mov, 1 and 8 bpp in nut
    int is_mono;
    int is_pal8;
    int is_nut_mono;
    int is_nut_pal8;
    int is_yuv2;
    int is_lt_16bpp; // 16bpp pixfmt and bits_per_coded_sample < 16
    int tff;

    BswapDSPContext bbdsp;
    void *bitstream_buf;
    unsigned int bitstream_buf_size;
} RawVideoContext;

static const AVOption options[]={
{"top", "top field first", offsetof(RawVideoContext, tff), AV_OPT_TYPE_BOOL, {.i64 = -1}, -1, 1, AV_OPT_FLAG_DECODING_PARAM|AV_OPT_FLAG_VIDEO_PARAM},
{NULL}
};

static const AVClass rawdec_class = {
    .class_name = "rawdec",
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static av_cold int raw_init_decoder(AVCodecContext *avctx)
{
    RawVideoContext *context = avctx->priv_data;
    const AVPixFmtDescriptor *desc;

    ff_bswapdsp_init(&context->bbdsp);

    if (   avctx->codec_tag == MKTAG('r','a','w',' ')
        || avctx->codec_tag == MKTAG('N','O','1','6'))
        avctx->pix_fmt = avpriv_find_pix_fmt(avpriv_pix_fmt_bps_mov,
                                      avctx->bits_per_coded_sample);
    else if (avctx->codec_tag == MKTAG('W', 'R', 'A', 'W'))
        avctx->pix_fmt = avpriv_find_pix_fmt(avpriv_pix_fmt_bps_avi,
                                      avctx->bits_per_coded_sample);
    else if (avctx->codec_tag && (avctx->codec_tag & 0xFFFFFF) != MKTAG('B','I','T', 0))
        avctx->pix_fmt = avpriv_find_pix_fmt(ff_raw_pix_fmt_tags, avctx->codec_tag);
    else if (avctx->pix_fmt == AV_PIX_FMT_NONE && avctx->bits_per_coded_sample)
        avctx->pix_fmt = avpriv_find_pix_fmt(avpriv_pix_fmt_bps_avi,
                                      avctx->bits_per_coded_sample);

    desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    if (!desc) {
        av_log(avctx, AV_LOG_ERROR, "Invalid pixel format.\n");
        return AVERROR(EINVAL);
    }

    if (desc->flags & (AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_PSEUDOPAL)) {
        context->palette = av_buffer_alloc(AVPALETTE_SIZE);
        if (!context->palette)
            return AVERROR(ENOMEM);
        if (desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL)
            avpriv_set_systematic_pal2((uint32_t*)context->palette->data, avctx->pix_fmt);
        else {
            memset(context->palette->data, 0, AVPALETTE_SIZE);
            if (avctx->bits_per_coded_sample == 1)
                memset(context->palette->data, 0xff, 4);
        }
    }

    if ((avctx->extradata_size >= 9 &&
         !memcmp(avctx->extradata + avctx->extradata_size - 9, "BottomUp", 9)) ||
        avctx->codec_tag == MKTAG('c','y','u','v') ||
        avctx->codec_tag == MKTAG(3, 0, 0, 0) ||
        avctx->codec_tag == MKTAG('W','R','A','W'))
        context->flip = 1;

    if (avctx->pix_fmt == AV_PIX_FMT_MONOWHITE ||
        avctx->pix_fmt == AV_PIX_FMT_MONOBLACK)
        context->is_mono = 1;
    else if (avctx->pix_fmt == AV_PIX_FMT_PAL8)
        context->is_pal8 = 1;

    if (avctx->codec_tag == MKTAG('B','1','W','0') ||
        avctx->codec_tag == MKTAG('B','0','W','1'))
        context->is_nut_mono = 1;
    else if (avctx->codec_tag == MKTAG('P','A','L',8))
        context->is_nut_pal8 = 1;

    if (avctx->codec_tag == AV_RL32("yuv2") &&
        avctx->pix_fmt   == AV_PIX_FMT_YUYV422)
        context->is_yuv2 = 1;

    return 0;
}

static void flip(AVCodecContext *avctx, AVFrame *frame)
{
    frame->data[0]     += frame->linesize[0] * (avctx->height - 1);
    frame->linesize[0] *= -1;
}

/*
 * Scale sample to 16-bit resolution
 */
#define SCALE16(x, bits) (((x) << (16 - (bits))) | ((x) >> (2 * (bits) - 16)))

/**
 * Scale buffer to 16 bits per coded sample resolution
 */
#define MKSCALE16(name, r16, w16) \
static void name(AVCodecContext *avctx, uint8_t * dst, const uint8_t *buf, int buf_size, int packed) \
{ \
    int i; \
    if (!packed) { \
        for (i = 0; i + 1 < buf_size; i += 2) \
            w16(dst + i, SCALE16(r16(buf + i), avctx->bits_per_coded_sample)); \
    } else { \
        GetBitContext gb; \
        init_get_bits(&gb, buf, buf_size * 8); \
        for (i = 0; i < avctx->width * avctx->height; i++) { \
            int sample = get_bits(&gb, avctx->bits_per_coded_sample); \
            w16(dst + i*2, SCALE16(sample, avctx->bits_per_coded_sample)); \
        } \
   } \
}

MKSCALE16(scale16be, AV_RB16, AV_WB16)
MKSCALE16(scale16le, AV_RL16, AV_WL16)

static int raw_decode(AVCodecContext *avctx, void *data, int *got_frame,
                      AVPacket *avpkt)
{
    const AVPixFmtDescriptor *desc;
    RawVideoContext *context       = avctx->priv_data;
    const uint8_t *buf             = avpkt->data;
    int buf_size                   = avpkt->size;
    int linesize_align             = 4;
    int stride;
    int res, len;
    int need_copy;

    AVFrame   *frame   = data;

    if (avctx->width <= 0) {
        av_log(avctx, AV_LOG_ERROR, "width is not set\n");
        return AVERROR_INVALIDDATA;
    }
    if (avctx->height <= 0) {
        av_log(avctx, AV_LOG_ERROR, "height is not set\n");
        return AVERROR_INVALIDDATA;
    }

    if (context->is_nut_mono)
        stride = avctx->width / 8 + (avctx->width & 7 ? 1 : 0);
    else if (context->is_nut_pal8)
        stride = avctx->width;
    else
        stride = avpkt->size / avctx->height;

    av_log(avctx, AV_LOG_DEBUG, "PACKET SIZE: %d, STRIDE: %d\n", avpkt->size, stride);

    if (stride == 0 || avpkt->size < stride * avctx->height) {
        av_log(avctx, AV_LOG_ERROR, "Packet too small (%d)\n", avpkt->size);
        return AVERROR_INVALIDDATA;
    }

    desc = av_pix_fmt_desc_get(avctx->pix_fmt);

    if ((avctx->bits_per_coded_sample == 8 || avctx->bits_per_coded_sample == 4 ||
         avctx->bits_per_coded_sample == 2 || avctx->bits_per_coded_sample == 1 ||
         (avctx->bits_per_coded_sample == 0 && (context->is_nut_pal8 || context->is_mono)) ) &&
        (context->is_mono || context->is_pal8) &&
        (!avctx->codec_tag || avctx->codec_tag == MKTAG('r','a','w',' ') ||
                context->is_nut_mono || context->is_nut_pal8)) {
        context->is_1_2_4_8_bpp = 1;
        if (context->is_mono) {
            int row_bytes = avctx->width / 8 + (avctx->width & 7 ? 1 : 0);
            context->frame_size = av_image_get_buffer_size(avctx->pix_fmt,
                                                           FFALIGN(row_bytes, 16) * 8,
                                                           avctx->height, 1);
        } else
            context->frame_size = av_image_get_buffer_size(avctx->pix_fmt,
                                                           FFALIGN(avctx->width, 16),
                                                           avctx->height, 1);
    } else {
        context->is_lt_16bpp = av_get_bits_per_pixel(desc) == 16 && avctx->bits_per_coded_sample && avctx->bits_per_coded_sample < 16;
        context->frame_size = av_image_get_buffer_size(avctx->pix_fmt, avctx->width,
                                                       avctx->height, 1);
    }
    if (context->frame_size < 0)
        return context->frame_size;

    need_copy = !avpkt->buf || context->is_1_2_4_8_bpp || context->is_yuv2 || context->is_lt_16bpp;

    frame->pict_type        = AV_PICTURE_TYPE_I;
    frame->key_frame        = 1;

    res = ff_decode_frame_props(avctx, frame);
    if (res < 0)
        return res;

    av_frame_set_pkt_pos     (frame, avctx->internal->pkt->pos);
    av_frame_set_pkt_duration(frame, avctx->internal->pkt->duration);

    if (context->tff >= 0) {
        frame->interlaced_frame = 1;
        frame->top_field_first  = context->tff;
    }

    if ((res = av_image_check_size(avctx->width, avctx->height, 0, avctx)) < 0)
        return res;

    if (need_copy)
        frame->buf[0] = av_buffer_alloc(FFMAX(context->frame_size, buf_size));
    else
        frame->buf[0] = av_buffer_ref(avpkt->buf);
    if (!frame->buf[0])
        return AVERROR(ENOMEM);

    // 1, 2, 4 and 8 bpp in avi/mov, 1 and 8 bpp in nut
    if (context->is_1_2_4_8_bpp) {
        int i, j, row_pix = 0;
        uint8_t *dst = frame->buf[0]->data;
        buf_size = context->frame_size - (context->is_pal8 ? AVPALETTE_SIZE : 0);
        if (avctx->bits_per_coded_sample == 8 || context->is_nut_pal8 || context->is_mono) {
            int pix_per_byte = context->is_mono ? 8 : 1;
            for (i = 0, j = 0; j < buf_size && i<avpkt->size; i++, j++) {
                dst[j] = buf[i];
                row_pix += pix_per_byte;
                if (row_pix >= avctx->width) {
                    i += stride - (i % stride) - 1;
                    j += 16 - (j % 16) - 1;
                    row_pix = 0;
                }
            }
        } else if (avctx->bits_per_coded_sample == 4) {
            for (i = 0, j = 0; 2 * j + 1 < buf_size && i<avpkt->size; i++, j++) {
                dst[2 * j + 0] = buf[i] >> 4;
                dst[2 * j + 1] = buf[i] & 15;
                row_pix += 2;
                if (row_pix >= avctx->width) {
                    i += stride - (i % stride) - 1;
                    j += 8 - (j % 8) - 1;
                    row_pix = 0;
                }
            }
        } else if (avctx->bits_per_coded_sample == 2) {
            for (i = 0, j = 0; 4 * j + 3 < buf_size && i<avpkt->size; i++, j++) {
                dst[4 * j + 0] = buf[i] >> 6;
                dst[4 * j + 1] = buf[i] >> 4 & 3;
                dst[4 * j + 2] = buf[i] >> 2 & 3;
                dst[4 * j + 3] = buf[i]      & 3;
                row_pix += 4;
                if (row_pix >= avctx->width) {
                    i += stride - (i % stride) - 1;
                    j += 4 - (j % 4) - 1;
                    row_pix = 0;
                }
            }
        } else {
            av_assert0(avctx->bits_per_coded_sample == 1);
            for (i = 0, j = 0; 8 * j + 7 < buf_size && i<avpkt->size; i++, j++) {
                dst[8 * j + 0] = buf[i] >> 7;
                dst[8 * j + 1] = buf[i] >> 6 & 1;
                dst[8 * j + 2] = buf[i] >> 5 & 1;
                dst[8 * j + 3] = buf[i] >> 4 & 1;
                dst[8 * j + 4] = buf[i] >> 3 & 1;
                dst[8 * j + 5] = buf[i] >> 2 & 1;
                dst[8 * j + 6] = buf[i] >> 1 & 1;
                dst[8 * j + 7] = buf[i]      & 1;
                row_pix += 8;
                if (row_pix >= avctx->width) {
                    i += stride - (i % stride) - 1;
                    j += 2 - (j % 2) - 1;
                    row_pix = 0;
                }
            }
        }
        linesize_align = 16;
        buf = dst;
    } else if (context->is_lt_16bpp) {
        uint8_t *dst = frame->buf[0]->data;
        int packed = (avctx->codec_tag & 0xFFFFFF) == MKTAG('B','I','T', 0);
        int swap   =  avctx->codec_tag >> 24;

        if (packed && swap) {
            av_fast_padded_malloc(&context->bitstream_buf, &context->bitstream_buf_size, buf_size);
            if (!context->bitstream_buf)
                return AVERROR(ENOMEM);
            if (swap == 16)
                context->bbdsp.bswap16_buf(context->bitstream_buf, (const uint16_t*)buf, buf_size / 2);
            else if (swap == 32)
                context->bbdsp.bswap_buf(context->bitstream_buf, (const uint32_t*)buf, buf_size / 4);
            else
                return AVERROR_INVALIDDATA;
            buf = context->bitstream_buf;
        }

        if (desc->flags & AV_PIX_FMT_FLAG_BE)
            scale16be(avctx, dst, buf, buf_size, packed);
        else
            scale16le(avctx, dst, buf, buf_size, packed);

        buf = dst;
    } else if (need_copy) {
        memcpy(frame->buf[0]->data, buf, buf_size);
        buf = frame->buf[0]->data;
    }

    if (avctx->codec_tag == MKTAG('A', 'V', '1', 'x') ||
        avctx->codec_tag == MKTAG('A', 'V', 'u', 'p'))
        buf += buf_size - context->frame_size;

    len = context->frame_size - (avctx->pix_fmt==AV_PIX_FMT_PAL8 ? AVPALETTE_SIZE : 0);
    if (buf_size < len && ((avctx->codec_tag & 0xFFFFFF) != MKTAG('B','I','T', 0) || !need_copy)) {
        av_log(avctx, AV_LOG_ERROR, "Invalid buffer size, packet size %d < expected frame_size %d\n", buf_size, len);
        av_buffer_unref(&frame->buf[0]);
        return AVERROR(EINVAL);
    }

    if ((res = av_image_fill_arrays(frame->data, frame->linesize,
                                    buf, avctx->pix_fmt,
                                    avctx->width, avctx->height, 1)) < 0) {
        av_buffer_unref(&frame->buf[0]);
        return res;
    }

    if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        const uint8_t *pal = av_packet_get_side_data(avpkt, AV_PKT_DATA_PALETTE,
                                                     NULL);
        int ret;
        if (!context->palette)
            context->palette = av_buffer_alloc(AVPALETTE_SIZE);
        if (!context->palette) {
            av_buffer_unref(&frame->buf[0]);
            return AVERROR(ENOMEM);
        }
        ret = av_buffer_make_writable(&context->palette);
        if (ret < 0) {
            av_buffer_unref(&frame->buf[0]);
            return ret;
        }

        if (pal) {
            memcpy(context->palette->data, pal, AVPALETTE_SIZE);
            frame->palette_has_changed = 1;
        } else if (context->is_nut_pal8) {
            int vid_size = avctx->width * avctx->height;
            int pal_size = avpkt->size - vid_size;

            if (avpkt->size > vid_size && pal_size <= AVPALETTE_SIZE) {
                pal = avpkt->data + vid_size;
                memcpy(context->palette->data, pal, pal_size);
                frame->palette_has_changed = 1;
            }
        }
    }

    if ((avctx->pix_fmt==AV_PIX_FMT_RGB24    ||
        avctx->pix_fmt==AV_PIX_FMT_BGR24     ||
        avctx->pix_fmt==AV_PIX_FMT_GRAY8     ||
        avctx->pix_fmt==AV_PIX_FMT_RGB555LE  ||
        avctx->pix_fmt==AV_PIX_FMT_RGB555BE  ||
        avctx->pix_fmt==AV_PIX_FMT_RGB565LE  ||
        avctx->pix_fmt==AV_PIX_FMT_MONOWHITE ||
        avctx->pix_fmt==AV_PIX_FMT_MONOBLACK ||
        avctx->pix_fmt==AV_PIX_FMT_PAL8) &&
        FFALIGN(frame->linesize[0], linesize_align) * avctx->height <= buf_size)
        frame->linesize[0] = FFALIGN(frame->linesize[0], linesize_align);

    if (avctx->pix_fmt == AV_PIX_FMT_NV12 && avctx->codec_tag == MKTAG('N', 'V', '1', '2') &&
        FFALIGN(frame->linesize[0], linesize_align) * avctx->height +
        FFALIGN(frame->linesize[1], linesize_align) * ((avctx->height + 1) / 2) <= buf_size) {
        int la0 = FFALIGN(frame->linesize[0], linesize_align);
        frame->data[1] += (la0 - frame->linesize[0]) * avctx->height;
        frame->linesize[0] = la0;
        frame->linesize[1] = FFALIGN(frame->linesize[1], linesize_align);
    }

    if ((avctx->pix_fmt == AV_PIX_FMT_PAL8 && buf_size < context->frame_size) ||
        (desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL)) {
        frame->buf[1]  = av_buffer_ref(context->palette);
        if (!frame->buf[1]) {
            av_buffer_unref(&frame->buf[0]);
            return AVERROR(ENOMEM);
        }
        frame->data[1] = frame->buf[1]->data;
    }

    if (avctx->pix_fmt == AV_PIX_FMT_BGR24 &&
        ((frame->linesize[0] + 3) & ~3) * avctx->height <= buf_size)
        frame->linesize[0] = (frame->linesize[0] + 3) & ~3;

    if (context->flip)
        flip(avctx, frame);

    if (avctx->codec_tag == MKTAG('Y', 'V', '1', '2') ||
        avctx->codec_tag == MKTAG('Y', 'V', '1', '6') ||
        avctx->codec_tag == MKTAG('Y', 'V', '2', '4') ||
        avctx->codec_tag == MKTAG('Y', 'V', 'U', '9'))
        FFSWAP(uint8_t *, frame->data[1], frame->data[2]);

    if (avctx->codec_tag == AV_RL32("I420") && (avctx->width+1)*(avctx->height+1) * 3/2 == buf_size) {
        frame->data[1] = frame->data[1] +  (avctx->width+1)*(avctx->height+1) -avctx->width*avctx->height;
        frame->data[2] = frame->data[2] + ((avctx->width+1)*(avctx->height+1) -avctx->width*avctx->height)*5/4;
    }

    if (avctx->codec_tag == AV_RL32("yuv2") &&
        avctx->pix_fmt   == AV_PIX_FMT_YUYV422) {
        int x, y;
        uint8_t *line = frame->data[0];
        for (y = 0; y < avctx->height; y++) {
            for (x = 0; x < avctx->width; x++)
                line[2 * x + 1] ^= 0x80;
            line += frame->linesize[0];
        }
    }

    if (avctx->codec_tag == AV_RL32("b64a") &&
        avctx->pix_fmt   == AV_PIX_FMT_RGBA64BE) {
        uint8_t *dst = frame->data[0];
        uint64_t v;
        int x;
        for (x = 0; x >> 3 < avctx->width * avctx->height; x += 8) {
            v = AV_RB64(&dst[x]);
            AV_WB64(&dst[x], v << 16 | v >> 48);
        }
    }

    if (avctx->field_order > AV_FIELD_PROGRESSIVE) { /* we have interlaced material flagged in container */
        frame->interlaced_frame = 1;
        if (avctx->field_order == AV_FIELD_TT || avctx->field_order == AV_FIELD_TB)
            frame->top_field_first = 1;
    }

    *got_frame = 1;
    return buf_size;
}

static av_cold int raw_close_decoder(AVCodecContext *avctx)
{
    RawVideoContext *context = avctx->priv_data;

    av_buffer_unref(&context->palette);
    return 0;
}

AVCodec ff_rawvideo_decoder = {
    .name           = "rawvideo",
    .long_name      = NULL_IF_CONFIG_SMALL("raw video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_RAWVIDEO,
    .priv_data_size = sizeof(RawVideoContext),
    .init           = raw_init_decoder,
    .close          = raw_close_decoder,
    .decode         = raw_decode,
    .priv_class     = &rawdec_class,
    .capabilities   = AV_CODEC_CAP_PARAM_CHANGE,
};
