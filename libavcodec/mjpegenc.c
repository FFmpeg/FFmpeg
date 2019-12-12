/*
 * MJPEG encoder
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2003 Alex Beregszaszi
 * Copyright (c) 2003-2004 Michael Niedermayer
 *
 * Support for external huffman table, various fixes (AVID workaround),
 * aspecting, new decode_frame mechanism and apple mjpeg-b support
 *                                  by Alex Beregszaszi
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
 * MJPEG encoder.
 */

#include "libavutil/pixdesc.h"

#include "avcodec.h"
#include "jpegtables.h"
#include "mjpegenc_common.h"
#include "mpegvideo.h"
#include "mjpeg.h"
#include "mjpegenc.h"
#include "profiles.h"

static int alloc_huffman(MpegEncContext *s)
{
    MJpegContext *m = s->mjpeg_ctx;
    size_t num_mbs, num_blocks, num_codes;
    int blocks_per_mb;

    // We need to init this here as the mjpeg init is called before the common init,
    s->mb_width  = (s->width  + 15) / 16;
    s->mb_height = (s->height + 15) / 16;

    switch (s->chroma_format) {
    case CHROMA_420: blocks_per_mb =  6; break;
    case CHROMA_422: blocks_per_mb =  8; break;
    case CHROMA_444: blocks_per_mb = 12; break;
    default: av_assert0(0);
    };

    // Make sure we have enough space to hold this frame.
    num_mbs = s->mb_width * s->mb_height;
    num_blocks = num_mbs * blocks_per_mb;
    num_codes = num_blocks * 64;

    m->huff_buffer = av_malloc_array(num_codes, sizeof(MJpegHuffmanCode));
    if (!m->huff_buffer)
        return AVERROR(ENOMEM);
    return 0;
}

av_cold int ff_mjpeg_encode_init(MpegEncContext *s)
{
    MJpegContext *m;

    av_assert0(s->slice_context_count == 1);

    if (s->width > 65500 || s->height > 65500) {
        av_log(s, AV_LOG_ERROR, "JPEG does not support resolutions above 65500x65500\n");
        return AVERROR(EINVAL);
    }

    m = av_mallocz(sizeof(MJpegContext));
    if (!m)
        return AVERROR(ENOMEM);

    s->min_qcoeff=-1023;
    s->max_qcoeff= 1023;

    // Build default Huffman tables.
    // These may be overwritten later with more optimal Huffman tables, but
    // they are needed at least right now for some processes like trellis.
    ff_mjpeg_build_huffman_codes(m->huff_size_dc_luminance,
                                 m->huff_code_dc_luminance,
                                 avpriv_mjpeg_bits_dc_luminance,
                                 avpriv_mjpeg_val_dc);
    ff_mjpeg_build_huffman_codes(m->huff_size_dc_chrominance,
                                 m->huff_code_dc_chrominance,
                                 avpriv_mjpeg_bits_dc_chrominance,
                                 avpriv_mjpeg_val_dc);
    ff_mjpeg_build_huffman_codes(m->huff_size_ac_luminance,
                                 m->huff_code_ac_luminance,
                                 avpriv_mjpeg_bits_ac_luminance,
                                 avpriv_mjpeg_val_ac_luminance);
    ff_mjpeg_build_huffman_codes(m->huff_size_ac_chrominance,
                                 m->huff_code_ac_chrominance,
                                 avpriv_mjpeg_bits_ac_chrominance,
                                 avpriv_mjpeg_val_ac_chrominance);

    ff_init_uni_ac_vlc(m->huff_size_ac_luminance,   m->uni_ac_vlc_len);
    ff_init_uni_ac_vlc(m->huff_size_ac_chrominance, m->uni_chroma_ac_vlc_len);
    s->intra_ac_vlc_length      =
    s->intra_ac_vlc_last_length = m->uni_ac_vlc_len;
    s->intra_chroma_ac_vlc_length      =
    s->intra_chroma_ac_vlc_last_length = m->uni_chroma_ac_vlc_len;

    // Buffers start out empty.
    m->huff_ncode = 0;
    s->mjpeg_ctx = m;

    if(s->huffman == HUFFMAN_TABLE_OPTIMAL)
        return alloc_huffman(s);

    return 0;
}

av_cold void ff_mjpeg_encode_close(MpegEncContext *s)
{
    av_freep(&s->mjpeg_ctx->huff_buffer);
    av_freep(&s->mjpeg_ctx);
}

/**
 * Add code and table_id to the JPEG buffer.
 *
 * @param s The MJpegContext which contains the JPEG buffer.
 * @param table_id Which Huffman table the code belongs to.
 * @param code The encoded exponent of the coefficients and the run-bits.
 */
static inline void ff_mjpeg_encode_code(MJpegContext *s, uint8_t table_id, int code)
{
    MJpegHuffmanCode *c = &s->huff_buffer[s->huff_ncode++];
    c->table_id = table_id;
    c->code = code;
}

/**
 * Add the coefficient's data to the JPEG buffer.
 *
 * @param s The MJpegContext which contains the JPEG buffer.
 * @param table_id Which Huffman table the code belongs to.
 * @param val The coefficient.
 * @param run The run-bits.
 */
static void ff_mjpeg_encode_coef(MJpegContext *s, uint8_t table_id, int val, int run)
{
    int mant, code;

    if (val == 0) {
        av_assert0(run == 0);
        ff_mjpeg_encode_code(s, table_id, 0);
    } else {
        mant = val;
        if (val < 0) {
            val = -val;
            mant--;
        }

        code = (run << 4) | (av_log2_16bit(val) + 1);

        s->huff_buffer[s->huff_ncode].mant = mant;
        ff_mjpeg_encode_code(s, table_id, code);
    }
}

/**
 * Add the block's data into the JPEG buffer.
 *
 * @param s The MJpegEncContext that contains the JPEG buffer.
 * @param block The block.
 * @param n The block's index or number.
 */
static void record_block(MpegEncContext *s, int16_t *block, int n)
{
    int i, j, table_id;
    int component, dc, last_index, val, run;
    MJpegContext *m = s->mjpeg_ctx;

    /* DC coef */
    component = (n <= 3 ? 0 : (n&1) + 1);
    table_id = (n <= 3 ? 0 : 1);
    dc = block[0]; /* overflow is impossible */
    val = dc - s->last_dc[component];

    ff_mjpeg_encode_coef(m, table_id, val, 0);

    s->last_dc[component] = dc;

    /* AC coefs */

    run = 0;
    last_index = s->block_last_index[n];
    table_id |= 2;

    for(i=1;i<=last_index;i++) {
        j = s->intra_scantable.permutated[i];
        val = block[j];

        if (val == 0) {
            run++;
        } else {
            while (run >= 16) {
                ff_mjpeg_encode_code(m, table_id, 0xf0);
                run -= 16;
            }
            ff_mjpeg_encode_coef(m, table_id, val, run);
            run = 0;
        }
    }

    /* output EOB only if not already 64 values */
    if (last_index < 63 || run != 0)
        ff_mjpeg_encode_code(m, table_id, 0);
}

static void encode_block(MpegEncContext *s, int16_t *block, int n)
{
    int mant, nbits, code, i, j;
    int component, dc, run, last_index, val;
    MJpegContext *m = s->mjpeg_ctx;
    uint8_t *huff_size_ac;
    uint16_t *huff_code_ac;

    /* DC coef */
    component = (n <= 3 ? 0 : (n&1) + 1);
    dc = block[0]; /* overflow is impossible */
    val = dc - s->last_dc[component];
    if (n < 4) {
        ff_mjpeg_encode_dc(&s->pb, val, m->huff_size_dc_luminance, m->huff_code_dc_luminance);
        huff_size_ac = m->huff_size_ac_luminance;
        huff_code_ac = m->huff_code_ac_luminance;
    } else {
        ff_mjpeg_encode_dc(&s->pb, val, m->huff_size_dc_chrominance, m->huff_code_dc_chrominance);
        huff_size_ac = m->huff_size_ac_chrominance;
        huff_code_ac = m->huff_code_ac_chrominance;
    }
    s->last_dc[component] = dc;

    /* AC coefs */

    run = 0;
    last_index = s->block_last_index[n];
    for(i=1;i<=last_index;i++) {
        j = s->intra_scantable.permutated[i];
        val = block[j];
        if (val == 0) {
            run++;
        } else {
            while (run >= 16) {
                put_bits(&s->pb, huff_size_ac[0xf0], huff_code_ac[0xf0]);
                run -= 16;
            }
            mant = val;
            if (val < 0) {
                val = -val;
                mant--;
            }

            nbits= av_log2_16bit(val) + 1;
            code = (run << 4) | nbits;

            put_bits(&s->pb, huff_size_ac[code], huff_code_ac[code]);

            put_sbits(&s->pb, nbits, mant);
            run = 0;
        }
    }

    /* output EOB only if not already 64 values */
    if (last_index < 63 || run != 0)
        put_bits(&s->pb, huff_size_ac[0], huff_code_ac[0]);
}

void ff_mjpeg_encode_mb(MpegEncContext *s, int16_t block[12][64])
{
    int i;
    if (s->huffman == HUFFMAN_TABLE_OPTIMAL) {
        if (s->chroma_format == CHROMA_444) {
            record_block(s, block[0], 0);
            record_block(s, block[2], 2);
            record_block(s, block[4], 4);
            record_block(s, block[8], 8);
            record_block(s, block[5], 5);
            record_block(s, block[9], 9);

            if (16*s->mb_x+8 < s->width) {
                record_block(s, block[1], 1);
                record_block(s, block[3], 3);
                record_block(s, block[6], 6);
                record_block(s, block[10], 10);
                record_block(s, block[7], 7);
                record_block(s, block[11], 11);
            }
        } else {
            for(i=0;i<5;i++) {
                record_block(s, block[i], i);
            }
            if (s->chroma_format == CHROMA_420) {
                record_block(s, block[5], 5);
            } else {
                record_block(s, block[6], 6);
                record_block(s, block[5], 5);
                record_block(s, block[7], 7);
            }
        }
    } else {
        if (s->chroma_format == CHROMA_444) {
            encode_block(s, block[0], 0);
            encode_block(s, block[2], 2);
            encode_block(s, block[4], 4);
            encode_block(s, block[8], 8);
            encode_block(s, block[5], 5);
            encode_block(s, block[9], 9);

            if (16*s->mb_x+8 < s->width) {
                encode_block(s, block[1], 1);
                encode_block(s, block[3], 3);
                encode_block(s, block[6], 6);
                encode_block(s, block[10], 10);
                encode_block(s, block[7], 7);
                encode_block(s, block[11], 11);
            }
        } else {
            for(i=0;i<5;i++) {
                encode_block(s, block[i], i);
            }
            if (s->chroma_format == CHROMA_420) {
                encode_block(s, block[5], 5);
            } else {
                encode_block(s, block[6], 6);
                encode_block(s, block[5], 5);
                encode_block(s, block[7], 7);
            }
        }

        s->i_tex_bits += get_bits_diff(s);
    }
}

#if CONFIG_AMV_ENCODER
// maximum over s->mjpeg_vsample[i]
#define V_MAX 2
static int amv_encode_picture(AVCodecContext *avctx, AVPacket *pkt,
                              const AVFrame *pic_arg, int *got_packet)
{
    MpegEncContext *s = avctx->priv_data;
    AVFrame *pic;
    int i, ret;
    int chroma_h_shift, chroma_v_shift;

    av_pix_fmt_get_chroma_sub_sample(avctx->pix_fmt, &chroma_h_shift, &chroma_v_shift);

    if ((avctx->height & 15) && avctx->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL) {
        av_log(avctx, AV_LOG_ERROR,
               "Heights which are not a multiple of 16 might fail with some decoders, "
               "use vstrict=-1 / -strict -1 to use %d anyway.\n", avctx->height);
        av_log(avctx, AV_LOG_WARNING, "If you have a device that plays AMV videos, please test if videos "
               "with such heights work with it and report your findings to ffmpeg-devel@ffmpeg.org\n");
        return AVERROR_EXPERIMENTAL;
    }

    pic = av_frame_clone(pic_arg);
    if (!pic)
        return AVERROR(ENOMEM);
    //picture should be flipped upside-down
    for(i=0; i < 3; i++) {
        int vsample = i ? 2 >> chroma_v_shift : 2;
        pic->data[i] += pic->linesize[i] * (vsample * s->height / V_MAX - 1);
        pic->linesize[i] *= -1;
    }
    ret = ff_mpv_encode_picture(avctx, pkt, pic, got_packet);
    av_frame_free(&pic);
    return ret;
}
#endif

#define OFFSET(x) offsetof(MpegEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
FF_MPV_COMMON_OPTS
{ "pred", "Prediction method", OFFSET(pred), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, 3, VE, "pred" },
    { "left",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, INT_MIN, INT_MAX, VE, "pred" },
    { "plane",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 2 }, INT_MIN, INT_MAX, VE, "pred" },
    { "median", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 3 }, INT_MIN, INT_MAX, VE, "pred" },
{ "huffman", "Huffman table strategy", OFFSET(huffman), AV_OPT_TYPE_INT, { .i64 = HUFFMAN_TABLE_OPTIMAL }, 0, NB_HUFFMAN_TABLE_OPTION - 1, VE, "huffman" },
    { "default", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = HUFFMAN_TABLE_DEFAULT }, INT_MIN, INT_MAX, VE, "huffman" },
    { "optimal", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = HUFFMAN_TABLE_OPTIMAL }, INT_MIN, INT_MAX, VE, "huffman" },
{ NULL},
};

#if CONFIG_MJPEG_ENCODER
static const AVClass mjpeg_class = {
    .class_name = "mjpeg encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_mjpeg_encoder = {
    .name           = "mjpeg",
    .long_name      = NULL_IF_CONFIG_SMALL("MJPEG (Motion JPEG)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MJPEG,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_mpv_encode_init,
    .encode2        = ff_mpv_encode_picture,
    .close          = ff_mpv_encode_end,
    .capabilities   = AV_CODEC_CAP_SLICE_THREADS | AV_CODEC_CAP_FRAME_THREADS | AV_CODEC_CAP_INTRA_ONLY,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_NONE
    },
    .priv_class     = &mjpeg_class,
    .profiles       = NULL_IF_CONFIG_SMALL(ff_mjpeg_profiles),
};
#endif

#if CONFIG_AMV_ENCODER
static const AVClass amv_class = {
    .class_name = "amv encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_amv_encoder = {
    .name           = "amv",
    .long_name      = NULL_IF_CONFIG_SMALL("AMV Video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AMV,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_mpv_encode_init,
    .encode2        = amv_encode_picture,
    .close          = ff_mpv_encode_end,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_NONE
    },
    .priv_class     = &amv_class,
};
#endif
