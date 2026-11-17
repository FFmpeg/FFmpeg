/*
 * Zip Motion Blocks Video (ZMBV) decoder
 * Copyright (c) 2006 Konstantin Shishkov
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
 * Zip Motion Blocks Video decoder
 */

#include <stddef.h>

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/buffer.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "zlib_wrapper.h"

#include <zlib.h>

#define ZMBV_KEYFRAME 1
#define ZMBV_DELTAPAL 2

enum ZmbvFormat {
    ZMBV_FMT_NONE  = 0,
    ZMBV_FMT_1BPP  = 1,
    ZMBV_FMT_2BPP  = 2,
    ZMBV_FMT_4BPP  = 3,
    ZMBV_FMT_8BPP  = 4,
    ZMBV_FMT_15BPP = 5,
    ZMBV_FMT_16BPP = 6,
    ZMBV_FMT_24BPP = 7,
    ZMBV_FMT_32BPP = 8
};

/*
 * Decoder context
 */
typedef struct ZmbvContext {
    AVCodecContext *avctx;

    int bpp;
    unsigned int decomp_size;
    uint8_t* decomp_buf;
    uint8_t pal[768];
    int palette_dirty;
    AVFrame *prev_frame;
    int width, height;
    int fmt;
    int comp;
    int flags;
    int stride;
    int bw, bh, bx, by;
    int decomp_len;
    int got_keyframe;
    FFZStream zstream;
    int (*decode_xor)(struct ZmbvContext *c, uint8_t *output,
                      ptrdiff_t out_linesize, const uint8_t *prev,
                      ptrdiff_t prev_linesize);
} ZmbvContext;

static int get_video_buffer(AVCodecContext *avctx, AVFrame *frame)
{
    int ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF);
    if (ret < 0)
        return ret;

    if (frame->buf[0] && frame->extended_data == frame->data &&
        !frame->extended_buf && !frame->hw_frames_ctx)
        return 0;

    av_log(avctx, AV_LOG_ERROR, "Invalid software video buffer from get_buffer2()\n");
    av_frame_unref(frame);
    return AVERROR(EINVAL);
}

/* Reference a software video frame's buffers and layout. The destination is
 * either a fresh output frame or a reference previously set by this helper.
 */
static int ref_video_buffers(AVFrame *dst, const AVFrame *src)
{
    av_assert1(dst != src);
    av_assert1(dst->extended_data == dst->data && !dst->extended_buf &&
               !dst->hw_frames_ctx);
    av_assert1(!dst->side_data && !dst->metadata && !dst->private_ref &&
               !dst->opaque && !dst->opaque_ref);

    for (int i = 0; i < FF_ARRAY_ELEMS(dst->buf); i++) {
        int ret = av_buffer_replace(&dst->buf[i], src->buf[i]);
        if (ret < 0) {
            av_frame_unref(dst);
            return ret;
        }
    }

    memcpy(dst->data, src->data, sizeof(dst->data));
    memcpy(dst->linesize, src->linesize, sizeof(dst->linesize));
    dst->format = src->format;
    dst->width  = src->width;
    dst->height = src->height;

    return 0;
}

/**
 * Decode XOR'ed frame - 8bpp version
 */

static int zmbv_decode_xor_8(ZmbvContext *c, uint8_t *output,
                             ptrdiff_t out_linesize, const uint8_t *prev,
                             ptrdiff_t prev_linesize)
{
    uint8_t *src = c->decomp_buf;
    int8_t *mvec;
    int x, y;
    int d, dx, dy, bw2, bh2;
    int block;
    int i, j;
    int mx, my;

    if (c->flags & ZMBV_DELTAPAL) {
        for (i = 0; i < 768; i++)
            c->pal[i] ^= *src++;
        c->palette_dirty = 1;
    }

    mvec = (int8_t*)src;
    src += ((c->bx * c->by * 2 + 3) & ~3);

    block = 0;
    for (y = 0; y < c->height; y += c->bh) {
        bh2 = ((c->height - y) > c->bh) ? c->bh : (c->height - y);
        for (x = 0; x < c->width; x += c->bw) {
            uint8_t *out;
            const uint8_t *tprev;

            d = mvec[block] & 1;
            dx = mvec[block] >> 1;
            dy = mvec[block + 1] >> 1;
            block += 2;

            bw2 = ((c->width - x) > c->bw) ? c->bw : (c->width - x);

            /* copy block - motion vectors out of bounds are used to zero blocks */
            out = output + x;
            tprev = prev + x + dx + dy * prev_linesize;
            mx = x + dx;
            my = y + dy;
            for (j = 0; j < bh2; j++) {
                if (my + j < 0 || my + j >= c->height) {
                    memset(out, 0, bw2);
                } else if (mx >= 0 && mx + bw2 <= c->width){
                    memcpy(out, tprev, sizeof(*out) * bw2);
                } else {
                    for (i = 0; i < bw2; i++) {
                        if (mx + i < 0 || mx + i >= c->width)
                            out[i] = 0;
                        else
                            out[i] = tprev[i];
                    }
                }
                out += out_linesize;
                tprev += prev_linesize;
            }

            if (d) { /* apply XOR'ed difference */
                if (c->decomp_len - (src - c->decomp_buf) < bw2 * bh2)
                    return AVERROR_INVALIDDATA;
                out = output + x;
                for (j = 0; j < bh2; j++) {
                    for (i = 0; i < bw2; i++)
                        out[i] ^= *src++;
                    out += out_linesize;
                }
            }
        }
        output += out_linesize * c->bh;
        prev += prev_linesize * c->bh;
    }
    if (src - c->decomp_buf != c->decomp_len)
        av_log(c->avctx, AV_LOG_ERROR, "Used %td of %i bytes\n",
               src-c->decomp_buf, c->decomp_len);
    return 0;
}

/**
 * Decode XOR'ed frame - 15bpp and 16bpp version
 */

static int zmbv_decode_xor_16(ZmbvContext *c, uint8_t *output,
                              ptrdiff_t out_linesize, const uint8_t *prev,
                              ptrdiff_t prev_linesize)
{
    uint8_t *src = c->decomp_buf;
    uint16_t *dst = (uint16_t *)output;
    const uint16_t *ref = (const uint16_t *)prev;
    ptrdiff_t out_step = out_linesize / 2;
    ptrdiff_t prev_step = prev_linesize / 2;
    int8_t *mvec;
    int x, y;
    int d, dx, dy, bw2, bh2;
    int block;
    int i, j;
    int mx, my;

    mvec = (int8_t*)src;
    src += ((c->bx * c->by * 2 + 3) & ~3);

    block = 0;
    for (y = 0; y < c->height; y += c->bh) {
        bh2 = ((c->height - y) > c->bh) ? c->bh : (c->height - y);
        for (x = 0; x < c->width; x += c->bw) {
            uint16_t *out;
            const uint16_t *tprev;

            d = mvec[block] & 1;
            dx = mvec[block] >> 1;
            dy = mvec[block + 1] >> 1;
            block += 2;

            bw2 = ((c->width - x) > c->bw) ? c->bw : (c->width - x);

            /* copy block - motion vectors out of bounds are used to zero blocks */
            out = dst + x;
            tprev = ref + x + dx + dy * prev_step;
            mx = x + dx;
            my = y + dy;
            for (j = 0; j < bh2; j++) {
                if (my + j < 0 || my + j >= c->height) {
                    memset(out, 0, bw2 * 2);
                } else if (mx >= 0 && mx + bw2 <= c->width){
                    memcpy(out, tprev, sizeof(*out) * bw2);
                } else {
                    for (i = 0; i < bw2; i++) {
                        if (mx + i < 0 || mx + i >= c->width)
                            out[i] = 0;
                        else
                            out[i] = tprev[i];
                    }
                }
                out += out_step;
                tprev += prev_step;
            }

            if (d) { /* apply XOR'ed difference */
                if (c->decomp_len - (src - c->decomp_buf) < bw2 * bh2 * 2)
                    return AVERROR_INVALIDDATA;
                out = dst + x;
                for (j = 0; j < bh2; j++){
                    for (i = 0; i < bw2; i++) {
                        out[i] ^= *((uint16_t*)src);
                        src += 2;
                    }
                    out += out_step;
                }
            }
        }
        dst += out_step * c->bh;
        ref += prev_step * c->bh;
    }
    if (src - c->decomp_buf != c->decomp_len)
        av_log(c->avctx, AV_LOG_ERROR, "Used %td of %i bytes\n",
               src-c->decomp_buf, c->decomp_len);
    return 0;
}

#ifdef ZMBV_ENABLE_24BPP
/**
 * Decode XOR'ed frame - 24bpp version
 */

static int zmbv_decode_xor_24(ZmbvContext *c, uint8_t *output,
                              ptrdiff_t out_linesize, const uint8_t *prev,
                              ptrdiff_t prev_linesize)
{
    uint8_t *src = c->decomp_buf;
    ptrdiff_t stride = out_linesize;
    ptrdiff_t pstride = prev_linesize;
    int8_t *mvec;
    int x, y;
    int d, dx, dy, bw2, bh2;
    int block;
    int i, j;
    int mx, my;

    mvec = (int8_t*)src;
    src += ((c->bx * c->by * 2 + 3) & ~3);

    block = 0;
    for (y = 0; y < c->height; y += c->bh) {
        bh2 = ((c->height - y) > c->bh) ? c->bh : (c->height - y);
        for (x = 0; x < c->width; x += c->bw) {
            uint8_t *out;
            const uint8_t *tprev;

            d = mvec[block] & 1;
            dx = mvec[block] >> 1;
            dy = mvec[block + 1] >> 1;
            block += 2;

            bw2 = ((c->width - x) > c->bw) ? c->bw : (c->width - x);

            /* copy block - motion vectors out of bounds are used to zero blocks */
            out = output + x * 3;
            tprev = prev + (x + dx) * 3 + dy * pstride;
            mx = x + dx;
            my = y + dy;
            for (j = 0; j < bh2; j++) {
                if (my + j < 0 || my + j >= c->height) {
                    memset(out, 0, bw2 * 3);
                } else if (mx >= 0 && mx + bw2 <= c->width){
                    memcpy(out, tprev, 3 * bw2);
                } else {
                    for (i = 0; i < bw2; i++){
                        if (mx + i < 0 || mx + i >= c->width) {
                            out[i * 3 + 0] = 0;
                            out[i * 3 + 1] = 0;
                            out[i * 3 + 2] = 0;
                        } else {
                            out[i * 3 + 0] = tprev[i * 3 + 0];
                            out[i * 3 + 1] = tprev[i * 3 + 1];
                            out[i * 3 + 2] = tprev[i * 3 + 2];
                        }
                    }
                }
                out += stride;
                tprev += pstride;
            }

            if (d) { /* apply XOR'ed difference */
                if (c->decomp_len - (src - c->decomp_buf) < bw2 * bh2 * 3)
                    return AVERROR_INVALIDDATA;
                out = output + x * 3;
                for (j = 0; j < bh2; j++) {
                    for (i = 0; i < bw2; i++) {
                        out[i * 3 + 0] ^= *src++;
                        out[i * 3 + 1] ^= *src++;
                        out[i * 3 + 2] ^= *src++;
                    }
                    out += stride;
                }
            }
        }
        output += stride * c->bh;
        prev += pstride * c->bh;
    }
    if (src - c->decomp_buf != c->decomp_len)
        av_log(c->avctx, AV_LOG_ERROR, "Used %td of %i bytes\n",
               src-c->decomp_buf, c->decomp_len);
    return 0;
}
#endif //ZMBV_ENABLE_24BPP

/**
 * Decode XOR'ed frame - 32bpp version
 */

static int zmbv_decode_xor_32(ZmbvContext *c, uint8_t *output,
                              ptrdiff_t out_linesize, const uint8_t *prev,
                              ptrdiff_t prev_linesize)
{
    uint8_t *src = c->decomp_buf;
    uint32_t *dst = (uint32_t *)output;
    const uint32_t *ref = (const uint32_t *)prev;
    ptrdiff_t out_step = out_linesize / 4;
    ptrdiff_t prev_step = prev_linesize / 4;
    int8_t *mvec;
    int x, y;
    int d, dx, dy, bw2, bh2;
    int block;
    int i, j;
    int mx, my;

    mvec = (int8_t*)src;
    src += ((c->bx * c->by * 2 + 3) & ~3);

    block = 0;
    for (y = 0; y < c->height; y += c->bh) {
        bh2 = ((c->height - y) > c->bh) ? c->bh : (c->height - y);
        for (x = 0; x < c->width; x += c->bw) {
            uint32_t *out;
            const uint32_t *tprev;

            d = mvec[block] & 1;
            dx = mvec[block] >> 1;
            dy = mvec[block + 1] >> 1;
            block += 2;

            bw2 = ((c->width - x) > c->bw) ? c->bw : (c->width - x);

            /* copy block - motion vectors out of bounds are used to zero blocks */
            out = dst + x;
            tprev = ref + x + dx + dy * prev_step;
            mx = x + dx;
            my = y + dy;
            for (j = 0; j < bh2; j++) {
                if (my + j < 0 || my + j >= c->height) {
                    memset(out, 0, bw2 * 4);
                } else if (mx >= 0 && mx + bw2 <= c->width){
                    memcpy(out, tprev, sizeof(*out) * bw2);
                } else {
                    for (i = 0; i < bw2; i++){
                        if (mx + i < 0 || mx + i >= c->width)
                            out[i] = 0;
                        else
                            out[i] = tprev[i];
                    }
                }
                out += out_step;
                tprev += prev_step;
            }

            if (d) { /* apply XOR'ed difference */
                if (c->decomp_len - (src - c->decomp_buf) < bw2 * bh2 * 4)
                    return AVERROR_INVALIDDATA;
                out = dst + x;
                for (j = 0; j < bh2; j++){
                    for (i = 0; i < bw2; i++) {
                        out[i] ^= *((uint32_t *) src);
                        src += 4;
                    }
                    out += out_step;
                }
            }
        }
        dst += out_step * c->bh;
        ref += prev_step * c->bh;
    }
    if (src - c->decomp_buf != c->decomp_len)
        av_log(c->avctx, AV_LOG_ERROR, "Used %td of %i bytes\n",
               src-c->decomp_buf, c->decomp_len);
    return 0;
}

/**
 * Decode intraframe
 */
static int zmbv_decode_intra(ZmbvContext *c, uint8_t *dst,
                             ptrdiff_t dst_linesize)
{
    uint8_t *src = c->decomp_buf;
    int y;

    /* make the palette available on the way out */
    if (c->fmt == ZMBV_FMT_8BPP) {
        memcpy(c->pal, src, 768);
        src += 768;
    }

    for (y = 0; y < c->height; y++) {
        memcpy(dst, src, c->stride);
        dst += dst_linesize;
        src += c->stride;
    }
    return 0;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    ZmbvContext * const c = avctx->priv_data;
    int zret = Z_OK; // Zlib return code
    int len = buf_size;
    int hi_ver, lo_ver, ret;
    int expected_size;
    int direct = 0;

    /* parse header */
    if (len < 1)
        return AVERROR_INVALIDDATA;
    c->flags = buf[0];
    buf++; len--;
    if (c->flags & ZMBV_KEYFRAME) {
        c->got_keyframe = 0;

        if (len < 6)
            return AVERROR_INVALIDDATA;
        hi_ver = buf[0];
        lo_ver = buf[1];
        c->comp = buf[2];
        c->fmt = buf[3];
        c->bw = buf[4];
        c->bh = buf[5];
        c->decode_xor = NULL;

        buf += 6;
        len -= 6;
        av_log(avctx, AV_LOG_DEBUG,
               "Flags=%X ver=%i.%i comp=%i fmt=%i blk=%ix%i\n",
               c->flags,hi_ver,lo_ver,c->comp,c->fmt,c->bw,c->bh);
        if (hi_ver != 0 || lo_ver != 1) {
            avpriv_request_sample(avctx, "Version %i.%i", hi_ver, lo_ver);
            return AVERROR_PATCHWELCOME;
        }
        if (c->bw == 0 || c->bh == 0) {
            avpriv_request_sample(avctx, "Block size %ix%i", c->bw, c->bh);
            return AVERROR_PATCHWELCOME;
        }
        if (c->comp != 0 && c->comp != 1) {
            avpriv_request_sample(avctx, "Compression type %i", c->comp);
            return AVERROR_PATCHWELCOME;
        }

        switch (c->fmt) {
        case ZMBV_FMT_8BPP:
            c->bpp = 8;
            c->decode_xor = zmbv_decode_xor_8;
            avctx->pix_fmt = AV_PIX_FMT_PAL8;
            c->stride = c->width;
            break;
        case ZMBV_FMT_15BPP:
        case ZMBV_FMT_16BPP:
            c->bpp = 16;
            c->decode_xor = zmbv_decode_xor_16;
            if (c->fmt == ZMBV_FMT_15BPP)
                avctx->pix_fmt = AV_PIX_FMT_RGB555LE;
            else
                avctx->pix_fmt = AV_PIX_FMT_RGB565LE;
            c->stride = c->width * 2;
            break;
#ifdef ZMBV_ENABLE_24BPP
        case ZMBV_FMT_24BPP:
            c->bpp = 24;
            c->decode_xor = zmbv_decode_xor_24;
            avctx->pix_fmt = AV_PIX_FMT_BGR24;
            c->stride = c->width * 3;
            break;
#endif //ZMBV_ENABLE_24BPP
        case ZMBV_FMT_32BPP:
            c->bpp = 32;
            c->decode_xor = zmbv_decode_xor_32;
            avctx->pix_fmt = AV_PIX_FMT_BGR0;
            c->stride = c->width * 4;
            break;
        default:
            c->decode_xor = NULL;
            avpriv_request_sample(avctx, "Format %i", c->fmt);
            return AVERROR_PATCHWELCOME;
        }

        zret = inflateReset(&c->zstream.zstream);
        if (zret != Z_OK) {
            av_log(avctx, AV_LOG_ERROR, "Inflate reset error: %d\n", zret);
            return AVERROR_UNKNOWN;
        }

        c->bx = (c->width + c->bw - 1) / c->bw;
        c->by = (c->height+ c->bh - 1) / c->bh;
        /* A keyframe may change the pixel format; drop the reference frame
         * so a P frame never references a frame in a different format.
         */
        av_frame_unref(c->prev_frame);
        c->got_keyframe = 1;
    }
    if (c->flags & ZMBV_KEYFRAME) {
        expected_size = avctx->width * avctx->height * (c->bpp / 8);
    } else {
        expected_size = (c->bx * c->by * 2 + 3) & ~3;
    }
    if (avctx->pix_fmt == AV_PIX_FMT_PAL8 &&
        (c->flags & (ZMBV_DELTAPAL | ZMBV_KEYFRAME)))
        expected_size += 768;

    if (!c->got_keyframe) {
        av_log(avctx, AV_LOG_ERROR, "Error! Got no format or no keyframe!\n");
        return AVERROR_INVALIDDATA;
    }
    if (!(c->flags & ZMBV_KEYFRAME) && !c->prev_frame->data[0]) {
        av_log(avctx, AV_LOG_ERROR, "Missing reference frame.\n");
        return AVERROR_INVALIDDATA;
    }

    if (c->comp == 0) { // uncompressed data
        if (c->decomp_size < len) {
            av_log(avctx, AV_LOG_ERROR, "Buffer too small\n");
            return AVERROR_INVALIDDATA;
        }
        memcpy(c->decomp_buf, buf, len);
        c->decomp_len = len;
    } else { // ZLIB-compressed data
        z_stream *const zstream = &c->zstream.zstream;
        uint8_t *out = c->decomp_buf;
        int out_size = c->decomp_size;

        /* A packed keyframe without a palette is inflated straight into
         * the output frame, which saves the copy from decomp_buf.
         */
        if ((c->flags & ZMBV_KEYFRAME) && c->fmt != ZMBV_FMT_8BPP) {
            ret = get_video_buffer(avctx, frame);
            if (ret < 0)
                return ret;
            if (frame->linesize[0] == c->stride) {
                direct   = 1;
                out      = frame->data[0];
                out_size = expected_size;
            }
        }

        zstream->total_in  = zstream->total_out = 0;
        zstream->next_in   = buf;
        zstream->avail_in  = len;
        zstream->next_out  = out;
        zstream->avail_out = out_size;
        zret = inflate(zstream, Z_SYNC_FLUSH);
        if (zret != Z_OK && zret != Z_STREAM_END) {
            av_log(avctx, AV_LOG_ERROR, "inflate error %d\n", zret);
            return AVERROR_INVALIDDATA;
        }
        if (direct && !zstream->avail_out && zret != Z_STREAM_END) {
            /* Any further output means the payload exceeds the frame. */
            zstream->next_out  = c->decomp_buf;
            zstream->avail_out = 1;
            zret = inflate(zstream, Z_SYNC_FLUSH);
            if (zret != Z_OK && zret != Z_STREAM_END && zret != Z_BUF_ERROR) {
                av_log(avctx, AV_LOG_ERROR, "inflate error %d\n", zret);
                return AVERROR_INVALIDDATA;
            }
        }
        c->decomp_len = zstream->total_out;
    }
    if (expected_size > c->decomp_len ||
        (c->flags & ZMBV_KEYFRAME) && expected_size < c->decomp_len) {
        av_log(avctx, AV_LOG_ERROR, "decompressed size %d is incorrect, expected %d\n", c->decomp_len, expected_size);
        return AVERROR_INVALIDDATA;
    }

    /* A payload that is only the motion vector table has no block data.
     * Zero vectors let us reuse the reference if its palette is current.
     * A rejected delta can leave a palette change pending.
     */
    if (!(c->flags & (ZMBV_KEYFRAME | ZMBV_DELTAPAL)) &&
        !c->palette_dirty && c->decomp_len == expected_size) {
        const uint8_t *mvec = c->decomp_buf;
        int i, n = c->bx * c->by * 2;

        for (i = 0; i < n; i++)
            if (mvec[i])
                break;
        if (i == n) {
            ret = ref_video_buffers(frame, c->prev_frame);
            if (ret < 0)
                return ret;
            ret = ff_decode_frame_props(avctx, frame);
            if (ret < 0)
                return ret;
            ret = ff_attach_decode_data(avctx, frame);
            if (ret < 0)
                return ret;
            frame->pict_type = AV_PICTURE_TYPE_P;
            *got_frame = 1;
            return buf_size;
        }
    }

    /* Decode directly into the output frame. It is kept as the reference
     * frame for the next P frame, hence the reference flag.
     */
    if (!frame->buf[0] &&
        (ret = get_video_buffer(avctx, frame)) < 0)
        return ret;

    if (c->flags & ZMBV_KEYFRAME) {
        frame->flags |= AV_FRAME_FLAG_KEY;
        frame->pict_type = AV_PICTURE_TYPE_I;
        if (!direct)
            zmbv_decode_intra(c, frame->data[0], frame->linesize[0]);
    } else {
        frame->flags &= ~AV_FRAME_FLAG_KEY;
        frame->pict_type = AV_PICTURE_TYPE_P;
        if (c->decomp_len < 2LL * ((c->width + c->bw - 1) / c->bw) * ((c->height + c->bh - 1) / c->bh))
            return AVERROR_INVALIDDATA;
        if (c->decomp_len) {
            if ((ret = c->decode_xor(c, frame->data[0], frame->linesize[0],
                                     c->prev_frame->data[0],
                                     c->prev_frame->linesize[0])) < 0)
                return ret;
        }
    }

    if (c->fmt == ZMBV_FMT_8BPP) {
        for (int j = 0; j < 256; j++)
            AV_WN32(&frame->data[1][j * 4], 0xFFU << 24 | AV_RB24(&c->pal[j * 3]));
    }

    ret = ref_video_buffers(c->prev_frame, frame);
    if (ret < 0)
        return ret;
    c->palette_dirty = 0;

    *got_frame = 1;

    /* always report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    ZmbvContext * const c = avctx->priv_data;

    c->avctx = avctx;

    c->width = avctx->width;
    c->height = avctx->height;

    c->bpp = avctx->bits_per_coded_sample;

    if ((avctx->width + 255ULL) * (avctx->height + 64ULL) > FFMIN(avctx->max_pixels, INT_MAX / 4) ) {
        av_log(avctx, AV_LOG_ERROR, "Internal buffer (decomp_size) larger than max_pixels or too large\n");
        return AVERROR_INVALIDDATA;
    }

    c->decomp_size = (avctx->width + 255) * 4 * (avctx->height + 64);

    /* Allocate decompression buffer */
    c->decomp_buf = av_mallocz(c->decomp_size);
    if (!c->decomp_buf) {
        av_log(avctx, AV_LOG_ERROR,
                "Can't allocate decompression buffer.\n");
        return AVERROR(ENOMEM);
    }

    c->prev_frame = av_frame_alloc();
    if (!c->prev_frame)
        return AVERROR(ENOMEM);

    return ff_inflate_init(&c->zstream, avctx);
}

static av_cold void decode_flush(AVCodecContext *avctx)
{
    ZmbvContext *const c = avctx->priv_data;

    av_frame_unref(c->prev_frame);
    c->got_keyframe = 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    ZmbvContext * const c = avctx->priv_data;

    av_freep(&c->decomp_buf);
    av_frame_free(&c->prev_frame);
    ff_inflate_end(&c->zstream);

    return 0;
}

const FFCodec ff_zmbv_decoder = {
    .p.name         = "zmbv",
    CODEC_LONG_NAME("Zip Motion Blocks Video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_ZMBV,
    .priv_data_size = sizeof(ZmbvContext),
    .init           = decode_init,
    .flush          = decode_flush,
    .close          = decode_end,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
