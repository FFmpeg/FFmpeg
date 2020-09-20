/*
 * Kodak PhotoCD (a.k.a. ImagePac) image decoder
 *
 * Copyright (c) 1996-2002 Gerd Knorr
 * Copyright (c) 2010 Kenneth Vermeirsch
 * Copyright (c) 2020 Paul B Mahol
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
 * Kodak PhotoCD (a.k.a. ImagePac) image decoder
 *
 * Supports resolutions up to 3072x2048.
 */

#define CACHED_BITSTREAM_READER !ARCH_X86_32

#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "bytestream.h"
#include "get_bits.h"
#include "internal.h"
#include "thread.h"

typedef struct PhotoCDContext {
    AVClass *class;
    int      lowres;

    GetByteContext gb;
    int      thumbnails;  //* number of thumbnails; 0 for normal image */
    int      resolution;
    int      orientation;

    int      streampos;

    uint8_t  bits[256];
    uint16_t codes[256];
    uint8_t  syms[256];

    VLC      vlc[3];
} PhotoCDContext;

typedef struct ImageInfo {
    uint32_t start;
    uint16_t width, height;
} ImageInfo;

static const ImageInfo img_info[6] = {
    {8192,    192, 128},
    {47104,   384, 256},
    {196608,  768, 512},
    {0,      1536, 1024},
    {0,      3072, 2048},
    {0,      6144, 4096},
};

static av_noinline void interp_lowres(PhotoCDContext *s, AVFrame *picture,
                                      int width, int height)
{
    GetByteContext *gb = &s->gb;
    int start = s->streampos + img_info[2].start;
    uint8_t *ptr, *ptr1, *ptr2;
    uint8_t *dst;
    int fill;

    ptr  = picture->data[0];
    ptr1 = picture->data[1];
    ptr2 = picture->data[2];

    bytestream2_seek(gb, start, SEEK_SET);

    for (int y = 0; y < height; y += 2) {
        dst = ptr;
        for (int x = 0; x < width - 1; x++) {
            fill = bytestream2_get_byte(gb);
            *(dst++) = fill;
            *(dst++) = (fill + bytestream2_peek_byte(gb) + 1) >> 1;
        }
        fill      = bytestream2_get_byte(gb);
        *(dst++) = fill;
        *(dst++) = fill;

        ptr += picture->linesize[0] << 1;

        dst = ptr;
        for (int x = 0; x < width - 1; x++) {
            fill = bytestream2_get_byte(gb);
            *(dst++) =  fill;
            *(dst++) = (fill + bytestream2_peek_byte(gb) + 1) >> 1;
        }
        fill      = bytestream2_get_byte(gb);
        *(dst++) = fill;
        *(dst++) = fill;

        ptr += picture->linesize[0] << 1;

        dst = ptr1;
        for (int x = 0; x < (width >> 1) - 1; x++) {
            fill = bytestream2_get_byte(gb);
            *(dst++) =  fill;
            *(dst++) = (fill + bytestream2_peek_byte(gb) + 1) >> 1;
        }
        fill      = bytestream2_get_byte(gb);
        *(dst++) = fill;
        *(dst++) = fill;

        ptr1 += picture->linesize[1] << 1;

        dst = ptr2;
        for (int x = 0; x < (width >> 1) - 1; x++) {
            fill = bytestream2_get_byte(gb);
            *(dst++) =  fill;
            *(dst++) = (fill + bytestream2_peek_byte(gb) + 1) >> 1;
        }
        fill      = bytestream2_get_byte(gb);
        *(dst++) = fill;
        *(dst++) = fill;

        ptr2 += picture->linesize[2] << 1;
    }

    s->streampos += bytestream2_tell(gb) - start;
}

static av_noinline void interp_lines(uint8_t *ptr, int linesize,
                                     int width, int height)
{
    const uint8_t *src1;
    uint8_t *dst;
    int x;

    for (int y = 0; y < height - 2; y += 2) {
        const uint8_t *src1 = ptr;
        uint8_t *dst = ptr + linesize;
        const uint8_t *src2 = dst + linesize;
        for (x = 0; x < width - 2; x += 2) {
            dst[x]     = (src1[x] + src2[x] + 1) >> 1;
            dst[x + 1] = (src1[x] + src2[x] + src1[x + 2] + src2[x + 2] + 2) >> 2;
        }
        dst[x] = dst[x + 1] = (src1[x] + src2[x] + 1) >> 1;

        ptr += linesize << 1;
    }

    src1 = ptr;
    dst = ptr + linesize;
    for (x = 0; x < width - 2; x += 2) {
        dst[x]     = src1[x];
        dst[x + 1] = (src1[x] + src1[x + 2] + 1) >> 1;
    }
    dst[x] = dst[x + 1] = src1[x];
}

static av_noinline void interp_pixels(uint8_t *ptr, int linesize,
                                      int width, int height)
{
    for (int y = height - 2; y >= 0; y -= 2) {
        const uint8_t *src = ptr + (y >> 1) * linesize;
        uint8_t *dst = ptr +  y * linesize;

        dst[width - 2] = dst[width - 1] = src[(width >> 1) - 1];
        for (int x = width - 4; x >= 0; x -= 2) {
            dst[x]     =  src[x >> 1];
            dst[x + 1] = (src[x >> 1] + src[(x >> 1) + 1] + 1) >> 1;
        }
    }
}

static av_noinline int read_hufftable(AVCodecContext *avctx, VLC *vlc)
{
    PhotoCDContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    int start = s->streampos;
    int count, ret;

    bytestream2_seek(gb, start, SEEK_SET);

    count = bytestream2_get_byte(gb) + 1;
    if (bytestream2_get_bytes_left(gb) < count * 4)
        return AVERROR_INVALIDDATA;

    for (int j = 0; j < count; j++) {
        const int bit  = bytestream2_get_byteu(gb) + 1;
        const int code = bytestream2_get_be16u(gb);
        const int sym  = bytestream2_get_byteu(gb);

        if (bit > 16)
            return AVERROR_INVALIDDATA;

        s->bits[j]  = bit;
        s->codes[j] = code >> (16 - bit);
        s->syms[j]  = sym;
    }

    ff_free_vlc(vlc);
    ret = ff_init_vlc_sparse(vlc, 12, count,
                             s->bits,  sizeof(*s->bits),  sizeof(*s->bits),
                             s->codes, sizeof(*s->codes), sizeof(*s->codes),
                             s->syms,  sizeof(*s->syms),  sizeof(*s->syms), 0);

    s->streampos = bytestream2_tell(gb);

    return ret;
}

static av_noinline int decode_huff(AVCodecContext *avctx, AVFrame *frame,
                                   int target_res, int curr_res)
{
    PhotoCDContext *s = avctx->priv_data;
    GetBitContext g;
    GetByteContext *gb = &s->gb;
    int ret, y = 0, type, height;
    int start = s->streampos;
    unsigned shiftreg;
    const int scaling = target_res - curr_res;
    const uint8_t type2idx[] = { 0, 0xff, 1, 2 };

    bytestream2_seek(gb, start, SEEK_SET);
    ret = init_get_bits8(&g, gb->buffer, bytestream2_get_bytes_left(gb));
    if (ret < 0)
        return ret;

    height = img_info[curr_res].height;

    while (y < height) {
        uint8_t *data;
        int x2, idx;

        for (; get_bits_left(&g) > 0;) {
            if (show_bits(&g, 12) == 0xfff)
                break;
            skip_bits(&g, 8);
        }

        shiftreg = show_bits(&g, 24);
        while (shiftreg != 0xfffffe) {
            if (get_bits_left(&g) <= 0)
                return AVERROR_INVALIDDATA;
            skip_bits(&g, 1);
            shiftreg = show_bits(&g, 24);
        }
        skip_bits(&g, 24);
        y = show_bits(&g, 15) & 0x1fff;
        if (y >= height)
            break;
        type = get_bits(&g, 2);
        skip_bits(&g, 14);

        if (type == 1)
            return AVERROR_INVALIDDATA;
        idx  = type2idx[type];

        data = frame->data[idx] + (y >> !!idx) * frame->linesize[idx];

        x2 = avctx->width >> (scaling + !!idx);
        for (int x = 0; x < x2; x++) {
            int m;

            if (get_bits_left(&g) <= 0)
                return AVERROR_INVALIDDATA;
            m = get_vlc2(&g, s->vlc[idx].table, s->vlc[idx].bits, 2);
            if (m < 0)
                return AVERROR_INVALIDDATA;
            m = sign_extend(m, 8);
            data[x] = av_clip_uint8(data[x] + m);
        }
    }

    s->streampos += (get_bits_count(&g) + 7) >> 3;
    s->streampos  = (s->streampos + 0x6000 + 2047) & ~0x7ff;

    return 0;
}

static int photocd_decode_frame(AVCodecContext *avctx, void *data,
                                int *got_frame, AVPacket *avpkt)
{
    PhotoCDContext *s = avctx->priv_data;
    ThreadFrame frame = { .f = data };
    const uint8_t *buf = avpkt->data;
    GetByteContext *gb = &s->gb;
    AVFrame *p = data;
    uint8_t *ptr, *ptr1, *ptr2;
    int ret;

    if (avpkt->size < img_info[0].start)
        return AVERROR_INVALIDDATA;

    if (!memcmp("PCD_OPA", buf, 7)) {
        s->thumbnails = AV_RL16(buf + 10);
        av_log(avctx, AV_LOG_WARNING, "this is a thumbnails file, "
               "reading first thumbnail only\n");
    } else if (avpkt->size < 786432) {
        return AVERROR_INVALIDDATA;
    } else if (memcmp("PCD_IPI", buf + 0x800, 7)) {
        return AVERROR_INVALIDDATA;
    }

    s->orientation = s->thumbnails ? buf[12] & 3 : buf[0x48] & 3;

    if (s->thumbnails)
        s->resolution = 0;
    else if (avpkt->size <= 788480)
        s->resolution = 2;
    else
        s->resolution = av_clip(4 - s->lowres, 0, 4);

    ret = ff_set_dimensions(avctx, img_info[s->resolution].width, img_info[s->resolution].height);
    if (ret < 0)
        return ret;

    if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
        return ret;

    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    bytestream2_init(gb, avpkt->data, avpkt->size);

    if (s->resolution < 3) {
        ptr  = p->data[0];
        ptr1 = p->data[1];
        ptr2 = p->data[2];

        if (s->thumbnails)
            bytestream2_seek(gb, 10240, SEEK_SET);
        else
            bytestream2_seek(gb, img_info[s->resolution].start, SEEK_SET);

        for (int y = 0; y < avctx->height; y += 2) {
            bytestream2_get_buffer(gb, ptr, avctx->width);
            ptr += p->linesize[0];

            bytestream2_get_buffer(gb, ptr, avctx->width);
            ptr += p->linesize[0];

            bytestream2_get_buffer(gb, ptr1, avctx->width >> 1);
            ptr1 += p->linesize[1];

            bytestream2_get_buffer(gb, ptr2, avctx->width >> 1);
            ptr2 += p->linesize[2];
        }
    } else {
        s->streampos = 0;
        ptr  = p->data[0];
        ptr1 = p->data[1];
        ptr2 = p->data[2];

        interp_lowres(s, p, img_info[2].width, img_info[2].height);

        interp_lines(ptr1, p->linesize[1], img_info[2].width, img_info[2].height);
        interp_lines(ptr2, p->linesize[2], img_info[2].width, img_info[2].height);

        if (s->resolution == 4) {
            interp_pixels(ptr1, p->linesize[1], img_info[3].width, img_info[3].height);
            interp_lines (ptr1, p->linesize[1], img_info[3].width, img_info[3].height);
            interp_pixels(ptr2, p->linesize[2], img_info[3].width, img_info[3].height);
            interp_lines (ptr2, p->linesize[2], img_info[3].width, img_info[3].height);
        }

        interp_lines(ptr, p->linesize[0], img_info[3].width, img_info[3].height);

        s->streampos = 0xc2000;
        for (int n = 0; n < 3; n++) {
            if ((ret = read_hufftable(avctx, &s->vlc[n])) < 0)
                return ret;
        }
        s->streampos = (s->streampos + 2047) & ~0x3ff;
        if (decode_huff(avctx, p, s->resolution, 3) < 0)
            return AVERROR_INVALIDDATA;

        if (s->resolution == 4) {
            interp_pixels(ptr, p->linesize[0], img_info[4].width, img_info[4].height);
            interp_lines (ptr, p->linesize[0], img_info[4].width, img_info[4].height);

            for (int n = 0; n < 3; n++) {
                if ((ret = read_hufftable(avctx, &s->vlc[n])) < 0)
                    return ret;
            }
            s->streampos = (s->streampos + 2047) & ~0x3ff;
            if (decode_huff(avctx, p, 4, 4) < 0)
                return AVERROR_INVALIDDATA;
        }
    }

    {
        ptr1 = p->data[1];
        ptr2 = p->data[2];

        for (int y = 0; y < avctx->height >> 1; y++) {
            for (int x = 0; x < avctx->width >> 1; x++) {
                ptr1[x] = av_clip_uint8(ptr1[x] - 28);
                ptr2[x] = av_clip_uint8(ptr2[x] - 9);
            }

            ptr1 += p->linesize[1];
            ptr2 += p->linesize[2];
        }
    }

    *got_frame = 1;

    return 0;
}

static av_cold int photocd_decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt         = AV_PIX_FMT_YUV420P;
    avctx->colorspace      = AVCOL_SPC_BT709;
    avctx->color_primaries = AVCOL_PRI_BT709;
    avctx->color_trc       = AVCOL_TRC_IEC61966_2_1;
    avctx->color_range     = AVCOL_RANGE_JPEG;

    return 0;
}

static av_cold int photocd_decode_close(AVCodecContext *avctx)
{
    PhotoCDContext *s = avctx->priv_data;

    for (int i = 0; i < 3; i++)
        ff_free_vlc(&s->vlc[i]);

    return 0;
}

#define OFFSET(x) offsetof(PhotoCDContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "lowres",  "Lower the decoding resolution by a power of two",
        OFFSET(lowres), AV_OPT_TYPE_INT,  { .i64 = 0 }, 0, 4, VD },
    { NULL },
};

static const AVClass photocd_class = {
    .class_name = "photocd",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_photocd_decoder = {
    .name           = "photocd",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_PHOTOCD,
    .priv_data_size = sizeof(PhotoCDContext),
    .priv_class     = &photocd_class,
    .init           = photocd_decode_init,
    .close          = photocd_decode_close,
    .decode         = photocd_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
    .long_name      = NULL_IF_CONFIG_SMALL("Kodak Photo CD"),
};
