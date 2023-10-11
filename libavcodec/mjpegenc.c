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

#include "config_components.h"

#include "libavutil/mem.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "jpegtables.h"
#include "mjpegenc_common.h"
#include "mjpegenc_huffman.h"
#include "mpegvideo.h"
#include "mjpeg.h"
#include "mjpegenc.h"
#include "mpegvideoenc.h"
#include "profiles.h"

/* The following is the private context of MJPEG/AMV decoder.
 * Note that when using slice threading only the main thread's
 * MpegEncContext is followed by a MjpegContext; the other threads
 * can access this shared context via MpegEncContext.mjpeg. */
typedef struct MJPEGEncContext {
    MpegEncContext mpeg;
    MJpegContext   mjpeg;
} MJPEGEncContext;

static av_cold void init_uni_ac_vlc(const uint8_t huff_size_ac[256],
                                    uint8_t *uni_ac_vlc_len)
{
    for (int i = 0; i < 128; i++) {
        int level = i - 64;
        if (!level)
            continue;
        for (int run = 0; run < 64; run++) {
            int len, code, nbits;
            int alevel = FFABS(level);

            len = (run >> 4) * huff_size_ac[0xf0];

            nbits= av_log2_16bit(alevel) + 1;
            code = ((15&run) << 4) | nbits;

            len += huff_size_ac[code] + nbits;

            uni_ac_vlc_len[UNI_AC_ENC_INDEX(run, i)] = len;
            // We ignore EOB as its just a constant which does not change generally
        }
    }
}

static void mjpeg_encode_picture_header(MpegEncContext *s)
{
    ff_mjpeg_encode_picture_header(s->avctx, &s->pb, s->cur_pic.ptr->f, s->mjpeg_ctx,
                                   s->intra_scantable.permutated, 0,
                                   s->intra_matrix, s->chroma_intra_matrix,
                                   s->slice_context_count > 1);

    s->esc_pos = put_bytes_count(&s->pb, 0);
    for (int i = 1; i < s->slice_context_count; i++)
        s->thread_context[i]->esc_pos = 0;
}

void ff_mjpeg_amv_encode_picture_header(MpegEncContext *s)
{
    MJPEGEncContext *const m = (MJPEGEncContext*)s;
    av_assert2(s->mjpeg_ctx == &m->mjpeg);
    /* s->huffman == HUFFMAN_TABLE_OPTIMAL can only be true for MJPEG. */
    if (!CONFIG_MJPEG_ENCODER || m->mjpeg.huffman != HUFFMAN_TABLE_OPTIMAL)
        mjpeg_encode_picture_header(s);
}

#if CONFIG_MJPEG_ENCODER
/**
 * Encodes and outputs the entire frame in the JPEG format.
 *
 * @param s The MpegEncContext.
 */
static void mjpeg_encode_picture_frame(MpegEncContext *s)
{
    int nbits, code, table_id;
    MJpegContext *m = s->mjpeg_ctx;
    uint8_t  *huff_size[4] = { m->huff_size_dc_luminance,
                               m->huff_size_dc_chrominance,
                               m->huff_size_ac_luminance,
                               m->huff_size_ac_chrominance };
    uint16_t *huff_code[4] = { m->huff_code_dc_luminance,
                               m->huff_code_dc_chrominance,
                               m->huff_code_ac_luminance,
                               m->huff_code_ac_chrominance };
    size_t total_bits = 0;
    size_t bytes_needed;

    s->header_bits = get_bits_diff(s);
    // Estimate the total size first
    for (int i = 0; i < m->huff_ncode; i++) {
        table_id = m->huff_buffer[i].table_id;
        code = m->huff_buffer[i].code;
        nbits = code & 0xf;

        total_bits += huff_size[table_id][code] + nbits;
    }

    bytes_needed = (total_bits + 7) / 8;
    ff_mpv_reallocate_putbitbuffer(s, bytes_needed, bytes_needed);

    for (int i = 0; i < m->huff_ncode; i++) {
        table_id = m->huff_buffer[i].table_id;
        code = m->huff_buffer[i].code;
        nbits = code & 0xf;

        put_bits(&s->pb, huff_size[table_id][code], huff_code[table_id][code]);
        if (nbits != 0) {
            put_sbits(&s->pb, nbits, m->huff_buffer[i].mant);
        }
    }

    m->huff_ncode = 0;
    s->i_tex_bits = get_bits_diff(s);
}

/**
 * Builds all 4 optimal Huffman tables.
 *
 * Uses the data stored in the JPEG buffer to compute the tables.
 * Stores the Huffman tables in the bits_* and val_* arrays in the MJpegContext.
 *
 * @param m MJpegContext containing the JPEG buffer.
 */
static void mjpeg_build_optimal_huffman(MJpegContext *m)
{
    MJpegEncHuffmanContext dc_luminance_ctx;
    MJpegEncHuffmanContext dc_chrominance_ctx;
    MJpegEncHuffmanContext ac_luminance_ctx;
    MJpegEncHuffmanContext ac_chrominance_ctx;
    MJpegEncHuffmanContext *ctx[4] = { &dc_luminance_ctx,
                                       &dc_chrominance_ctx,
                                       &ac_luminance_ctx,
                                       &ac_chrominance_ctx };
    for (int i = 0; i < 4; i++)
        ff_mjpeg_encode_huffman_init(ctx[i]);

    for (int i = 0; i < m->huff_ncode; i++) {
        int table_id = m->huff_buffer[i].table_id;
        int code     = m->huff_buffer[i].code;

        ff_mjpeg_encode_huffman_increment(ctx[table_id], code);
    }

    ff_mjpeg_encode_huffman_close(&dc_luminance_ctx,
                                  m->bits_dc_luminance,
                                  m->val_dc_luminance, 12);
    ff_mjpeg_encode_huffman_close(&dc_chrominance_ctx,
                                  m->bits_dc_chrominance,
                                  m->val_dc_chrominance, 12);
    ff_mjpeg_encode_huffman_close(&ac_luminance_ctx,
                                  m->bits_ac_luminance,
                                  m->val_ac_luminance, 256);
    ff_mjpeg_encode_huffman_close(&ac_chrominance_ctx,
                                  m->bits_ac_chrominance,
                                  m->val_ac_chrominance, 256);

    ff_mjpeg_build_huffman_codes(m->huff_size_dc_luminance,
                                 m->huff_code_dc_luminance,
                                 m->bits_dc_luminance,
                                 m->val_dc_luminance);
    ff_mjpeg_build_huffman_codes(m->huff_size_dc_chrominance,
                                 m->huff_code_dc_chrominance,
                                 m->bits_dc_chrominance,
                                 m->val_dc_chrominance);
    ff_mjpeg_build_huffman_codes(m->huff_size_ac_luminance,
                                 m->huff_code_ac_luminance,
                                 m->bits_ac_luminance,
                                 m->val_ac_luminance);
    ff_mjpeg_build_huffman_codes(m->huff_size_ac_chrominance,
                                 m->huff_code_ac_chrominance,
                                 m->bits_ac_chrominance,
                                 m->val_ac_chrominance);
}
#endif

/**
 * Writes the complete JPEG frame when optimal huffman tables are enabled,
 * otherwise writes the stuffing.
 *
 * Header + values + stuffing.
 *
 * @param s The MpegEncContext.
 * @return int Error code, 0 if successful.
 */
int ff_mjpeg_encode_stuffing(MpegEncContext *s)
{
    MJpegContext *const m = s->mjpeg_ctx;
    PutBitContext *pbc = &s->pb;
    int mb_y = s->mb_y - !s->mb_x;
    int ret;

#if CONFIG_MJPEG_ENCODER
    if (m->huffman == HUFFMAN_TABLE_OPTIMAL) {

        mjpeg_build_optimal_huffman(m);

        // Replace the VLCs with the optimal ones.
        // The default ones may be used for trellis during quantization.
        init_uni_ac_vlc(m->huff_size_ac_luminance,   m->uni_ac_vlc_len);
        init_uni_ac_vlc(m->huff_size_ac_chrominance, m->uni_chroma_ac_vlc_len);
        s->intra_ac_vlc_length      =
        s->intra_ac_vlc_last_length = m->uni_ac_vlc_len;
        s->intra_chroma_ac_vlc_length      =
        s->intra_chroma_ac_vlc_last_length = m->uni_chroma_ac_vlc_len;

        mjpeg_encode_picture_header(s);
        mjpeg_encode_picture_frame(s);
    }
#endif

    ret = ff_mpv_reallocate_putbitbuffer(s, put_bits_count(&s->pb) / 8 + 100,
                                            put_bits_count(&s->pb) / 4 + 1000);
    if (ret < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Buffer reallocation failed\n");
        goto fail;
    }

    ff_mjpeg_escape_FF(pbc, s->esc_pos);

    if (s->slice_context_count > 1 && mb_y < s->mb_height - 1)
        put_marker(pbc, RST0 + (mb_y&7));
    s->esc_pos = put_bytes_count(pbc, 0);

fail:
    for (int i = 0; i < 3; i++)
        s->last_dc[i] = 128 << s->intra_dc_precision;

    return ret;
}

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
    MJpegContext *const m = &((MJPEGEncContext*)s)->mjpeg;
    int ret, use_slices;

    s->mjpeg_ctx = m;
    use_slices = s->avctx->slices > 0 ? s->avctx->slices > 1 :
                 (s->avctx->active_thread_type & FF_THREAD_SLICE) &&
                 s->avctx->thread_count > 1;

    if (s->codec_id == AV_CODEC_ID_AMV || use_slices)
        m->huffman = HUFFMAN_TABLE_DEFAULT;

    if (s->mpv_flags & FF_MPV_FLAG_QP_RD) {
        // Used to produce garbage with MJPEG.
        av_log(s->avctx, AV_LOG_ERROR,
               "QP RD is no longer compatible with MJPEG or AMV\n");
        return AVERROR(EINVAL);
    }

    /* The following check is automatically true for AMV,
     * but it doesn't hurt either. */
    ret = ff_mjpeg_encode_check_pix_fmt(s->avctx);
    if (ret < 0)
        return ret;

    if (s->width > 65500 || s->height > 65500) {
        av_log(s, AV_LOG_ERROR, "JPEG does not support resolutions above 65500x65500\n");
        return AVERROR(EINVAL);
    }

    s->min_qcoeff=-1023;
    s->max_qcoeff= 1023;

    // Build default Huffman tables.
    // These may be overwritten later with more optimal Huffman tables, but
    // they are needed at least right now for some processes like trellis.
    ff_mjpeg_build_huffman_codes(m->huff_size_dc_luminance,
                                 m->huff_code_dc_luminance,
                                 ff_mjpeg_bits_dc_luminance,
                                 ff_mjpeg_val_dc);
    ff_mjpeg_build_huffman_codes(m->huff_size_dc_chrominance,
                                 m->huff_code_dc_chrominance,
                                 ff_mjpeg_bits_dc_chrominance,
                                 ff_mjpeg_val_dc);
    ff_mjpeg_build_huffman_codes(m->huff_size_ac_luminance,
                                 m->huff_code_ac_luminance,
                                 ff_mjpeg_bits_ac_luminance,
                                 ff_mjpeg_val_ac_luminance);
    ff_mjpeg_build_huffman_codes(m->huff_size_ac_chrominance,
                                 m->huff_code_ac_chrominance,
                                 ff_mjpeg_bits_ac_chrominance,
                                 ff_mjpeg_val_ac_chrominance);

    init_uni_ac_vlc(m->huff_size_ac_luminance,   m->uni_ac_vlc_len);
    init_uni_ac_vlc(m->huff_size_ac_chrominance, m->uni_chroma_ac_vlc_len);
    s->intra_ac_vlc_length      =
    s->intra_ac_vlc_last_length = m->uni_ac_vlc_len;
    s->intra_chroma_ac_vlc_length      =
    s->intra_chroma_ac_vlc_last_length = m->uni_chroma_ac_vlc_len;

    // Buffers start out empty.
    m->huff_ncode = 0;

    if (m->huffman == HUFFMAN_TABLE_OPTIMAL)
        return alloc_huffman(s);

    return 0;
}

static av_cold int mjpeg_encode_close(AVCodecContext *avctx)
{
    MJPEGEncContext *const mjpeg = avctx->priv_data;
    av_freep(&mjpeg->mjpeg.huff_buffer);
    ff_mpv_encode_end(avctx);
    return 0;
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
 * @param s The MpegEncContext that contains the JPEG buffer.
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
    if (s->mjpeg_ctx->huffman == HUFFMAN_TABLE_OPTIMAL) {
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
    int chroma_v_shift = 1; /* AMV is 420-only */

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

#define OFFSET(x) offsetof(MJPEGEncContext, mjpeg.x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
FF_MPV_COMMON_OPTS
{ "huffman", "Huffman table strategy", OFFSET(huffman), AV_OPT_TYPE_INT, { .i64 = HUFFMAN_TABLE_OPTIMAL }, 0, NB_HUFFMAN_TABLE_OPTION - 1, VE, .unit = "huffman" },
    { "default", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = HUFFMAN_TABLE_DEFAULT }, INT_MIN, INT_MAX, VE, .unit = "huffman" },
    { "optimal", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = HUFFMAN_TABLE_OPTIMAL }, INT_MIN, INT_MAX, VE, .unit = "huffman" },
{ "force_duplicated_matrix", "Always write luma and chroma matrix for mjpeg, useful for rtp streaming.", OFFSET(force_duplicated_matrix), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, VE },
{ NULL},
};

#if CONFIG_MJPEG_ENCODER
static const AVClass mjpeg_class = {
    .class_name = "mjpeg encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_mjpeg_encoder = {
    .p.name         = "mjpeg",
    CODEC_LONG_NAME("MJPEG (Motion JPEG)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MJPEG,
    .priv_data_size = sizeof(MJPEGEncContext),
    .init           = ff_mpv_encode_init,
    FF_CODEC_ENCODE_CB(ff_mpv_encode_picture),
    .close          = mjpeg_encode_close,
    .p.capabilities = AV_CODEC_CAP_SLICE_THREADS | AV_CODEC_CAP_FRAME_THREADS |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_ICC_PROFILES,
    .p.pix_fmts     = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUV420P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_NONE
    },
    .color_ranges   = AVCOL_RANGE_MPEG | AVCOL_RANGE_JPEG,
    .p.priv_class   = &mjpeg_class,
    .p.profiles     = NULL_IF_CONFIG_SMALL(ff_mjpeg_profiles),
};
#endif

#if CONFIG_AMV_ENCODER
static const AVClass amv_class = {
    .class_name = "amv encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_amv_encoder = {
    .p.name         = "amv",
    CODEC_LONG_NAME("AMV Video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AMV,
    .priv_data_size = sizeof(MJPEGEncContext),
    .init           = ff_mpv_encode_init,
    FF_CODEC_ENCODE_CB(amv_encode_picture),
    .close          = mjpeg_encode_close,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .p.pix_fmts     = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_NONE
    },
    .color_ranges   = AVCOL_RANGE_JPEG,
    .p.priv_class   = &amv_class,
    .p.capabilities = AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
};
#endif
