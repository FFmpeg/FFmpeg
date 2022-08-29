/*
 * Flash Screen Video decoder
 * Copyright (C) 2004 Alex Beregszaszi
 * Copyright (C) 2006 Benjamin Larsson
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
 * Flash Screen Video decoder
 * @author Alex Beregszaszi
 * @author Benjamin Larsson
 * @author Daniel Verkamp
 * @author Konstantin Shishkov
 *
 * A description of the bitstream format for Flash Screen Video version 1/2
 * is part of the SWF File Format Specification (version 10), which can be
 * downloaded from http://www.adobe.com/devnet/swf.html.
 */

#include "config_components.h"

#include <stddef.h>
#include <zlib.h>

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "zlib_wrapper.h"

typedef struct BlockInfo {
    const uint8_t *pos;
    int      size;
} BlockInfo;

typedef struct FlashSVContext {
    AVCodecContext *avctx;
    AVFrame        *frame;
    int             image_width, image_height;
    int             block_width, block_height;
    uint8_t        *tmpblock;
    int             block_size;
    int             ver;
    const uint32_t *pal;
    int             is_keyframe;
    const uint8_t  *keyframedata;
    AVBufferRef    *keyframedata_buf;
    uint8_t        *keyframe;
    BlockInfo      *blocks;
    int             color_depth;
    int             zlibprime_curr, zlibprime_prev;
    int             diff_start, diff_height;
    FFZStream       zstream;
    uint8_t         tmp[UINT16_MAX];
} FlashSVContext;

static int decode_hybrid(const uint8_t *sptr, const uint8_t *sptr_end, uint8_t *dptr, int dx, int dy,
                         int h, int w, int stride, const uint32_t *pal)
{
    int x, y;
    const uint8_t *orig_src = sptr;

    for (y = dx + h; y > dx; y--) {
        uint8_t *dst = dptr + (y * stride) + dy * 3;
        for (x = 0; x < w; x++) {
            if (sptr >= sptr_end)
                return AVERROR_INVALIDDATA;
            if (*sptr & 0x80) {
                /* 15-bit color */
                unsigned c = AV_RB16(sptr) & ~0x8000;
                unsigned b =  c        & 0x1F;
                unsigned g = (c >>  5) & 0x1F;
                unsigned r =  c >> 10;
                /* 000aaabb -> aaabbaaa  */
                *dst++ = (b << 3) | (b >> 2);
                *dst++ = (g << 3) | (g >> 2);
                *dst++ = (r << 3) | (r >> 2);
                sptr += 2;
            } else {
                /* palette index */
                uint32_t c = pal[*sptr++];
                bytestream_put_le24(&dst, c);
            }
        }
    }
    return sptr - orig_src;
}

static av_cold int flashsv_decode_end(AVCodecContext *avctx)
{
    FlashSVContext *s = avctx->priv_data;

    ff_inflate_end(&s->zstream);
    /* release the frame if needed */
    av_frame_free(&s->frame);

    /* free the tmpblock */
    av_freep(&s->tmpblock);

    return 0;
}

static av_cold int flashsv_decode_init(AVCodecContext *avctx)
{
    FlashSVContext *s = avctx->priv_data;

    s->avctx          = avctx;
    avctx->pix_fmt = AV_PIX_FMT_BGR24;

    s->frame = av_frame_alloc();
    if (!s->frame) {
        return AVERROR(ENOMEM);
    }

    return ff_inflate_init(&s->zstream, avctx);
}

static int flashsv2_prime(FlashSVContext *s, const uint8_t *src, int size)
{
    int zret; // Zlib return code
    static const uint8_t zlib_header[] = { 0x78, 0x01 };
    z_stream *const zstream = &s->zstream.zstream;
    uint8_t *data = s->tmpblock;
    unsigned remaining;

    if (!src)
        return AVERROR_INVALIDDATA;

    zstream->next_in   = src;
    zstream->avail_in  = size;
    zstream->next_out  = data;
    zstream->avail_out = s->block_size * 3;
    zret = inflate(zstream, Z_SYNC_FLUSH);
    if (zret != Z_OK && zret != Z_STREAM_END)
        return AVERROR_UNKNOWN;
    remaining = s->block_size * 3 - zstream->avail_out;

    if ((zret = inflateReset(zstream)) != Z_OK) {
        av_log(s->avctx, AV_LOG_ERROR, "Inflate reset error: %d\n", zret);
        return AVERROR_UNKNOWN;
    }

    /* Create input for zlib that is equivalent to encoding the output
     * from above and decoding it again (the net result of this is that
     * the dictionary of past decoded data is correctly primed and
     * the adler32 checksum is correctly initialized).
     * This is accomplished by synthetizing blocks of uncompressed data
     * out of the output from above. See section 3.2.4 of RFC 1951. */
    zstream->next_in  = zlib_header;
    zstream->avail_in = sizeof(zlib_header);
    zret = inflate(zstream, Z_SYNC_FLUSH);
    if (zret != Z_OK)
        return AVERROR_UNKNOWN;
    while (remaining > 0) {
        unsigned block_size = FFMIN(UINT16_MAX, remaining);
        uint8_t header[5];
        /* Bit 0: Non-last-block, bits 1-2: BTYPE for uncompressed block */
        header[0] = 0;
        /* Block size */
        AV_WL16(header + 1, block_size);
        /* Block size (one's complement) */
        AV_WL16(header + 3, block_size ^ 0xFFFF);
        zstream->next_in   = header;
        zstream->avail_in  = sizeof(header);
        zstream->next_out  = s->tmp;
        zstream->avail_out = sizeof(s->tmp);
        zret = inflate(zstream, Z_SYNC_FLUSH);
        if (zret != Z_OK)
            return AVERROR_UNKNOWN;
        zstream->next_in   = data;
        zstream->avail_in  = block_size;
        zret = inflate(zstream, Z_SYNC_FLUSH);
        if (zret != Z_OK)
            return AVERROR_UNKNOWN;
        data      += block_size;
        remaining -= block_size;
    }

    return 0;
}

static int flashsv_decode_block(AVCodecContext *avctx, const AVPacket *avpkt,
                                GetBitContext *gb, int block_size,
                                int width, int height, int x_pos, int y_pos,
                                int blk_idx)
{
    struct FlashSVContext *s = avctx->priv_data;
    z_stream *const zstream = &s->zstream.zstream;
    uint8_t *line = s->tmpblock;
    int k;
    int ret = inflateReset(zstream);
    if (ret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate reset error: %d\n", ret);
        return AVERROR_UNKNOWN;
    }
    if (s->zlibprime_curr || s->zlibprime_prev) {
        ret = flashsv2_prime(s,
                             s->blocks[blk_idx].pos,
                             s->blocks[blk_idx].size);
        if (ret < 0)
            return ret;
    }
    zstream->next_in   = avpkt->data + get_bits_count(gb) / 8;
    zstream->avail_in  = block_size;
    zstream->next_out  = s->tmpblock;
    zstream->avail_out = s->block_size * 3;
    ret = inflate(zstream, Z_FINISH);
    if (ret == Z_DATA_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Zlib resync occurred\n");
        inflateSync(zstream);
        ret = inflate(zstream, Z_FINISH);
    }

    if (ret != Z_OK && ret != Z_STREAM_END) {
        //return -1;
    }

    if (s->is_keyframe) {
        s->blocks[blk_idx].pos  = s->keyframedata + (get_bits_count(gb) / 8);
        s->blocks[blk_idx].size = block_size;
    }

    y_pos += s->diff_start;

    if (!s->color_depth) {
        /* Flash Screen Video stores the image upside down, so copy
         * lines to destination in reverse order. */
        for (k = 1; k <= s->diff_height; k++) {
            memcpy(s->frame->data[0] + x_pos * 3 +
                   (s->image_height - y_pos - k) * s->frame->linesize[0],
                   line, width * 3);
            /* advance source pointer to next line */
            line += width * 3;
        }
    } else {
        /* hybrid 15-bit/palette mode */
        ret = decode_hybrid(s->tmpblock, zstream->next_out,
                      s->frame->data[0],
                      s->image_height - (y_pos + 1 + s->diff_height),
                      x_pos, s->diff_height, width,
                      s->frame->linesize[0], s->pal);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "decode_hybrid failed\n");
            return ret;
        }
    }
    skip_bits_long(gb, 8 * block_size); /* skip the consumed bits */
    return 0;
}

static int flashsv_decode_frame(AVCodecContext *avctx, AVFrame *rframe,
                                int *got_frame, AVPacket *avpkt)
{
    int buf_size = avpkt->size;
    FlashSVContext *s = avctx->priv_data;
    int h_blocks, v_blocks, h_part, v_part, i, j, ret;
    GetBitContext gb;
    int last_blockwidth = s->block_width;
    int last_blockheight= s->block_height;

    /* no supplementary picture */
    if (buf_size == 0)
        return 0;
    if (buf_size < 4)
        return -1;

    if ((ret = init_get_bits8(&gb, avpkt->data, buf_size)) < 0)
        return ret;

    /* start to parse the bitstream */
    s->block_width  = 16 * (get_bits(&gb, 4) + 1);
    s->image_width  = get_bits(&gb, 12);
    s->block_height = 16 * (get_bits(&gb, 4) + 1);
    s->image_height = get_bits(&gb, 12);

    if (   last_blockwidth != s->block_width
        || last_blockheight!= s->block_height)
        av_freep(&s->blocks);

    if (s->ver == 2) {
        skip_bits(&gb, 6);
        if (get_bits1(&gb)) {
            avpriv_request_sample(avctx, "iframe");
            return AVERROR_PATCHWELCOME;
        }
        if (get_bits1(&gb)) {
            avpriv_request_sample(avctx, "Custom palette");
            return AVERROR_PATCHWELCOME;
        }
    }

    /* calculate number of blocks and size of border (partial) blocks */
    h_blocks = s->image_width  / s->block_width;
    h_part   = s->image_width  % s->block_width;
    v_blocks = s->image_height / s->block_height;
    v_part   = s->image_height % s->block_height;

    /* the block size could change between frames, make sure the buffer
     * is large enough, if not, get a larger one */
    if (s->block_size < s->block_width * s->block_height) {
        int tmpblock_size = 3 * s->block_width * s->block_height, err;

        if ((err = av_reallocp(&s->tmpblock, tmpblock_size)) < 0) {
            s->block_size = 0;
            av_log(avctx, AV_LOG_ERROR,
                   "Cannot allocate decompression buffer.\n");
            return err;
        }
    }
    s->block_size = s->block_width * s->block_height;

    /* initialize the image size once */
    if (avctx->width == 0 && avctx->height == 0) {
        if ((ret = ff_set_dimensions(avctx, s->image_width, s->image_height)) < 0)
            return ret;
    }

    /* check for changes of image width and image height */
    if (avctx->width != s->image_width || avctx->height != s->image_height) {
        av_log(avctx, AV_LOG_ERROR,
               "Frame width or height differs from first frame!\n");
        av_log(avctx, AV_LOG_ERROR, "fh = %d, fv %d  vs  ch = %d, cv = %d\n",
               avctx->height, avctx->width, s->image_height, s->image_width);
        return AVERROR_INVALIDDATA;
    }

    /* we care for keyframes only in Screen Video v2 */
    s->is_keyframe = (avpkt->flags & AV_PKT_FLAG_KEY) && (s->ver == 2);
    if (s->is_keyframe) {
        int err = av_buffer_replace(&s->keyframedata_buf, avpkt->buf);
        if (err < 0)
            return err;
        s->keyframedata = avpkt->data;
        if (s->blocks)
            memset(s->blocks, 0, (v_blocks + !!v_part) * (h_blocks + !!h_part) *
                                 sizeof(s->blocks[0]));
    }
    if(s->ver == 2 && !s->blocks)
        s->blocks = av_mallocz((v_blocks + !!v_part) * (h_blocks + !!h_part) *
                               sizeof(s->blocks[0]));

    ff_dlog(avctx, "image: %dx%d block: %dx%d num: %dx%d part: %dx%d\n",
            s->image_width, s->image_height, s->block_width, s->block_height,
            h_blocks, v_blocks, h_part, v_part);

    if ((ret = ff_reget_buffer(avctx, s->frame, 0)) < 0)
        return ret;

    /* loop over all block columns */
    for (j = 0; j < v_blocks + (v_part ? 1 : 0); j++) {

        int y_pos  = j * s->block_height; // vertical position in frame
        int cur_blk_height = (j < v_blocks) ? s->block_height : v_part;

        /* loop over all block rows */
        for (i = 0; i < h_blocks + (h_part ? 1 : 0); i++) {
            int x_pos = i * s->block_width; // horizontal position in frame
            int cur_blk_width = (i < h_blocks) ? s->block_width : h_part;
            int has_diff = 0;

            /* get the size of the compressed zlib chunk */
            int size = get_bits(&gb, 16);

            s->color_depth    = 0;
            s->zlibprime_curr = 0;
            s->zlibprime_prev = 0;
            s->diff_start     = 0;
            s->diff_height    = cur_blk_height;

            if (8 * size > get_bits_left(&gb)) {
                av_frame_unref(s->frame);
                return AVERROR_INVALIDDATA;
            }

            if (s->ver == 2 && size) {
                skip_bits(&gb, 3);
                s->color_depth    = get_bits(&gb, 2);
                has_diff          = get_bits1(&gb);
                s->zlibprime_curr = get_bits1(&gb);
                s->zlibprime_prev = get_bits1(&gb);

                if (s->color_depth != 0 && s->color_depth != 2) {
                    av_log(avctx, AV_LOG_ERROR,
                           "%dx%d invalid color depth %d\n",
                           i, j, s->color_depth);
                    return AVERROR_INVALIDDATA;
                }

                if (has_diff) {
                    if (size < 3) {
                        av_log(avctx, AV_LOG_ERROR, "size too small for diff\n");
                        return AVERROR_INVALIDDATA;
                    }
                    if (!s->keyframe) {
                        av_log(avctx, AV_LOG_ERROR,
                               "Inter frame without keyframe\n");
                        return AVERROR_INVALIDDATA;
                    }
                    s->diff_start  = get_bits(&gb, 8);
                    s->diff_height = get_bits(&gb, 8);
                    if (s->diff_start + s->diff_height > cur_blk_height) {
                        av_log(avctx, AV_LOG_ERROR,
                               "Block parameters invalid: %d + %d > %d\n",
                               s->diff_start, s->diff_height, cur_blk_height);
                        return AVERROR_INVALIDDATA;
                    }
                    av_log(avctx, AV_LOG_DEBUG,
                           "%dx%d diff start %d height %d\n",
                           i, j, s->diff_start, s->diff_height);
                    size -= 2;
                }

                if (s->zlibprime_prev)
                    av_log(avctx, AV_LOG_DEBUG, "%dx%d zlibprime_prev\n", i, j);

                if (s->zlibprime_curr) {
                    int col = get_bits(&gb, 8);
                    int row = get_bits(&gb, 8);
                    av_log(avctx, AV_LOG_DEBUG, "%dx%d zlibprime_curr %dx%d\n",
                           i, j, col, row);
                    if (size < 3) {
                        av_log(avctx, AV_LOG_ERROR, "size too small for zlibprime_curr\n");
                        return AVERROR_INVALIDDATA;
                    }
                    size -= 2;
                    avpriv_request_sample(avctx, "zlibprime_curr");
                    return AVERROR_PATCHWELCOME;
                }
                if (!s->blocks && (s->zlibprime_curr || s->zlibprime_prev)) {
                    av_log(avctx, AV_LOG_ERROR,
                           "no data available for zlib priming\n");
                    return AVERROR_INVALIDDATA;
                }
                size--; // account for flags byte
            }

            if (has_diff) {
                int k;
                int off = (s->image_height - y_pos - 1) * s->frame->linesize[0];

                for (k = 0; k < cur_blk_height; k++) {
                    int x = off - k * s->frame->linesize[0] + x_pos * 3;
                    memcpy(s->frame->data[0] + x, s->keyframe + x,
                           cur_blk_width * 3);
                }
            }

            /* skip unchanged blocks, which have size 0 */
            if (size) {
                if (flashsv_decode_block(avctx, avpkt, &gb, size,
                                         cur_blk_width, cur_blk_height,
                                         x_pos, y_pos,
                                         i + j * (h_blocks + !!h_part)))
                    av_log(avctx, AV_LOG_ERROR,
                           "error in decompression of block %dx%d\n", i, j);
            }
        }
    }
    if (s->is_keyframe && s->ver == 2) {
        if (!s->keyframe) {
            s->keyframe = av_malloc(s->frame->linesize[0] * avctx->height);
            if (!s->keyframe) {
                av_log(avctx, AV_LOG_ERROR, "Cannot allocate image data\n");
                return AVERROR(ENOMEM);
            }
        }
        memcpy(s->keyframe, s->frame->data[0],
               s->frame->linesize[0] * avctx->height);
    }

    if ((ret = av_frame_ref(rframe, s->frame)) < 0)
        return ret;

    *got_frame = 1;

    if ((get_bits_count(&gb) / 8) != buf_size)
        av_log(avctx, AV_LOG_ERROR, "buffer not fully consumed (%d != %d)\n",
               buf_size, (get_bits_count(&gb) / 8));

    /* report that the buffer was completely consumed */
    return buf_size;
}

#if CONFIG_FLASHSV_DECODER
const FFCodec ff_flashsv_decoder = {
    .p.name         = "flashsv",
    CODEC_LONG_NAME("Flash Screen Video v1"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_FLASHSV,
    .priv_data_size = sizeof(FlashSVContext),
    .init           = flashsv_decode_init,
    .close          = flashsv_decode_end,
    FF_CODEC_DECODE_CB(flashsv_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .p.pix_fmts     = (const enum AVPixelFormat[]) { AV_PIX_FMT_BGR24, AV_PIX_FMT_NONE },
};
#endif /* CONFIG_FLASHSV_DECODER */

#if CONFIG_FLASHSV2_DECODER
static const uint32_t ff_flashsv2_default_palette[128] = {
    0x000000, 0x333333, 0x666666, 0x999999, 0xCCCCCC, 0xFFFFFF,
    0x330000, 0x660000, 0x990000, 0xCC0000, 0xFF0000, 0x003300,
    0x006600, 0x009900, 0x00CC00, 0x00FF00, 0x000033, 0x000066,
    0x000099, 0x0000CC, 0x0000FF, 0x333300, 0x666600, 0x999900,
    0xCCCC00, 0xFFFF00, 0x003333, 0x006666, 0x009999, 0x00CCCC,
    0x00FFFF, 0x330033, 0x660066, 0x990099, 0xCC00CC, 0xFF00FF,
    0xFFFF33, 0xFFFF66, 0xFFFF99, 0xFFFFCC, 0xFF33FF, 0xFF66FF,
    0xFF99FF, 0xFFCCFF, 0x33FFFF, 0x66FFFF, 0x99FFFF, 0xCCFFFF,
    0xCCCC33, 0xCCCC66, 0xCCCC99, 0xCCCCFF, 0xCC33CC, 0xCC66CC,
    0xCC99CC, 0xCCFFCC, 0x33CCCC, 0x66CCCC, 0x99CCCC, 0xFFCCCC,
    0x999933, 0x999966, 0x9999CC, 0x9999FF, 0x993399, 0x996699,
    0x99CC99, 0x99FF99, 0x339999, 0x669999, 0xCC9999, 0xFF9999,
    0x666633, 0x666699, 0x6666CC, 0x6666FF, 0x663366, 0x669966,
    0x66CC66, 0x66FF66, 0x336666, 0x996666, 0xCC6666, 0xFF6666,
    0x333366, 0x333399, 0x3333CC, 0x3333FF, 0x336633, 0x339933,
    0x33CC33, 0x33FF33, 0x663333, 0x993333, 0xCC3333, 0xFF3333,
    0x003366, 0x336600, 0x660033, 0x006633, 0x330066, 0x663300,
    0x336699, 0x669933, 0x993366, 0x339966, 0x663399, 0x996633,
    0x6699CC, 0x99CC66, 0xCC6699, 0x66CC99, 0x9966CC, 0xCC9966,
    0x99CCFF, 0xCCFF99, 0xFF99CC, 0x99FFCC, 0xCC99FF, 0xFFCC99,
    0x111111, 0x222222, 0x444444, 0x555555, 0xAAAAAA, 0xBBBBBB,
    0xDDDDDD, 0xEEEEEE
};

static av_cold int flashsv2_decode_init(AVCodecContext *avctx)
{
    FlashSVContext *s = avctx->priv_data;
    int ret;

    ret = flashsv_decode_init(avctx);
    if (ret < 0)
        return ret;
    s->pal = ff_flashsv2_default_palette;
    s->ver = 2;

    return 0;
}

static av_cold int flashsv2_decode_end(AVCodecContext *avctx)
{
    FlashSVContext *s = avctx->priv_data;

    av_buffer_unref(&s->keyframedata_buf);
    s->keyframedata = NULL;
    av_freep(&s->blocks);
    av_freep(&s->keyframe);
    flashsv_decode_end(avctx);

    return 0;
}

const FFCodec ff_flashsv2_decoder = {
    .p.name         = "flashsv2",
    CODEC_LONG_NAME("Flash Screen Video v2"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_FLASHSV2,
    .priv_data_size = sizeof(FlashSVContext),
    .init           = flashsv2_decode_init,
    .close          = flashsv2_decode_end,
    FF_CODEC_DECODE_CB(flashsv_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .p.pix_fmts     = (const enum AVPixelFormat[]) { AV_PIX_FMT_BGR24, AV_PIX_FMT_NONE },
};
#endif /* CONFIG_FLASHSV2_DECODER */
