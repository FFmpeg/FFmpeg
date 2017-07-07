/*
 * WebP (.webp) image decoder
 * Copyright (c) 2013 Aneesh Dogra <aneesh@sugarlabs.org>
 * Copyright (c) 2013 Justin Ruggles <justin.ruggles@gmail.com>
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
 * WebP image decoder
 *
 * @author Aneesh Dogra <aneesh@sugarlabs.org>
 * Container and Lossy decoding
 *
 * @author Justin Ruggles <justin.ruggles@gmail.com>
 * Lossless decoder
 * Compressed alpha for lossy
 *
 * @author James Almer <jamrial@gmail.com>
 * Exif metadata
 *
 * Unimplemented:
 *   - Animation
 *   - ICC profile
 *   - XMP metadata
 */

#include "libavutil/imgutils.h"

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "bytestream.h"
#include "exif.h"
#include "get_bits.h"
#include "internal.h"
#include "thread.h"
#include "vp8.h"

#define VP8X_FLAG_ANIMATION             0x02
#define VP8X_FLAG_XMP_METADATA          0x04
#define VP8X_FLAG_EXIF_METADATA         0x08
#define VP8X_FLAG_ALPHA                 0x10
#define VP8X_FLAG_ICC                   0x20

#define MAX_PALETTE_SIZE                256
#define MAX_CACHE_BITS                  11
#define NUM_CODE_LENGTH_CODES           19
#define HUFFMAN_CODES_PER_META_CODE     5
#define NUM_LITERAL_CODES               256
#define NUM_LENGTH_CODES                24
#define NUM_DISTANCE_CODES              40
#define NUM_SHORT_DISTANCES             120
#define MAX_HUFFMAN_CODE_LENGTH         15

static const uint16_t alphabet_sizes[HUFFMAN_CODES_PER_META_CODE] = {
    NUM_LITERAL_CODES + NUM_LENGTH_CODES,
    NUM_LITERAL_CODES, NUM_LITERAL_CODES, NUM_LITERAL_CODES,
    NUM_DISTANCE_CODES
};

static const uint8_t code_length_code_order[NUM_CODE_LENGTH_CODES] = {
    17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

static const int8_t lz77_distance_offsets[NUM_SHORT_DISTANCES][2] = {
    {  0, 1 }, {  1, 0 }, {  1, 1 }, { -1, 1 }, {  0, 2 }, {  2, 0 }, {  1, 2 }, { -1, 2 },
    {  2, 1 }, { -2, 1 }, {  2, 2 }, { -2, 2 }, {  0, 3 }, {  3, 0 }, {  1, 3 }, { -1, 3 },
    {  3, 1 }, { -3, 1 }, {  2, 3 }, { -2, 3 }, {  3, 2 }, { -3, 2 }, {  0, 4 }, {  4, 0 },
    {  1, 4 }, { -1, 4 }, {  4, 1 }, { -4, 1 }, {  3, 3 }, { -3, 3 }, {  2, 4 }, { -2, 4 },
    {  4, 2 }, { -4, 2 }, {  0, 5 }, {  3, 4 }, { -3, 4 }, {  4, 3 }, { -4, 3 }, {  5, 0 },
    {  1, 5 }, { -1, 5 }, {  5, 1 }, { -5, 1 }, {  2, 5 }, { -2, 5 }, {  5, 2 }, { -5, 2 },
    {  4, 4 }, { -4, 4 }, {  3, 5 }, { -3, 5 }, {  5, 3 }, { -5, 3 }, {  0, 6 }, {  6, 0 },
    {  1, 6 }, { -1, 6 }, {  6, 1 }, { -6, 1 }, {  2, 6 }, { -2, 6 }, {  6, 2 }, { -6, 2 },
    {  4, 5 }, { -4, 5 }, {  5, 4 }, { -5, 4 }, {  3, 6 }, { -3, 6 }, {  6, 3 }, { -6, 3 },
    {  0, 7 }, {  7, 0 }, {  1, 7 }, { -1, 7 }, {  5, 5 }, { -5, 5 }, {  7, 1 }, { -7, 1 },
    {  4, 6 }, { -4, 6 }, {  6, 4 }, { -6, 4 }, {  2, 7 }, { -2, 7 }, {  7, 2 }, { -7, 2 },
    {  3, 7 }, { -3, 7 }, {  7, 3 }, { -7, 3 }, {  5, 6 }, { -5, 6 }, {  6, 5 }, { -6, 5 },
    {  8, 0 }, {  4, 7 }, { -4, 7 }, {  7, 4 }, { -7, 4 }, {  8, 1 }, {  8, 2 }, {  6, 6 },
    { -6, 6 }, {  8, 3 }, {  5, 7 }, { -5, 7 }, {  7, 5 }, { -7, 5 }, {  8, 4 }, {  6, 7 },
    { -6, 7 }, {  7, 6 }, { -7, 6 }, {  8, 5 }, {  7, 7 }, { -7, 7 }, {  8, 6 }, {  8, 7 }
};

enum AlphaCompression {
    ALPHA_COMPRESSION_NONE,
    ALPHA_COMPRESSION_VP8L,
};

enum AlphaFilter {
    ALPHA_FILTER_NONE,
    ALPHA_FILTER_HORIZONTAL,
    ALPHA_FILTER_VERTICAL,
    ALPHA_FILTER_GRADIENT,
};

enum TransformType {
    PREDICTOR_TRANSFORM      = 0,
    COLOR_TRANSFORM          = 1,
    SUBTRACT_GREEN           = 2,
    COLOR_INDEXING_TRANSFORM = 3,
};

enum PredictionMode {
    PRED_MODE_BLACK,
    PRED_MODE_L,
    PRED_MODE_T,
    PRED_MODE_TR,
    PRED_MODE_TL,
    PRED_MODE_AVG_T_AVG_L_TR,
    PRED_MODE_AVG_L_TL,
    PRED_MODE_AVG_L_T,
    PRED_MODE_AVG_TL_T,
    PRED_MODE_AVG_T_TR,
    PRED_MODE_AVG_AVG_L_TL_AVG_T_TR,
    PRED_MODE_SELECT,
    PRED_MODE_ADD_SUBTRACT_FULL,
    PRED_MODE_ADD_SUBTRACT_HALF,
};

enum HuffmanIndex {
    HUFF_IDX_GREEN = 0,
    HUFF_IDX_RED   = 1,
    HUFF_IDX_BLUE  = 2,
    HUFF_IDX_ALPHA = 3,
    HUFF_IDX_DIST  = 4
};

/* The structure of WebP lossless is an optional series of transformation data,
 * followed by the primary image. The primary image also optionally contains
 * an entropy group mapping if there are multiple entropy groups. There is a
 * basic image type called an "entropy coded image" that is used for all of
 * these. The type of each entropy coded image is referred to by the
 * specification as its role. */
enum ImageRole {
    /* Primary Image: Stores the actual pixels of the image. */
    IMAGE_ROLE_ARGB,

    /* Entropy Image: Defines which Huffman group to use for different areas of
     *                the primary image. */
    IMAGE_ROLE_ENTROPY,

    /* Predictors: Defines which predictor type to use for different areas of
     *             the primary image. */
    IMAGE_ROLE_PREDICTOR,

    /* Color Transform Data: Defines the color transformation for different
     *                       areas of the primary image. */
    IMAGE_ROLE_COLOR_TRANSFORM,

    /* Color Index: Stored as an image of height == 1. */
    IMAGE_ROLE_COLOR_INDEXING,

    IMAGE_ROLE_NB,
};

typedef struct HuffReader {
    VLC vlc;                            /* Huffman decoder context */
    int simple;                         /* whether to use simple mode */
    int nb_symbols;                     /* number of coded symbols */
    uint16_t simple_symbols[2];         /* symbols for simple mode */
} HuffReader;

typedef struct ImageContext {
    enum ImageRole role;                /* role of this image */
    AVFrame *frame;                     /* AVFrame for data */
    int color_cache_bits;               /* color cache size, log2 */
    uint32_t *color_cache;              /* color cache data */
    int nb_huffman_groups;              /* number of huffman groups */
    HuffReader *huffman_groups;         /* reader for each huffman group */
    int size_reduction;                 /* relative size compared to primary image, log2 */
    int is_alpha_primary;
} ImageContext;

typedef struct WebPContext {
    VP8Context v;                       /* VP8 Context used for lossy decoding */
    GetBitContext gb;                   /* bitstream reader for main image chunk */
    AVFrame *alpha_frame;               /* AVFrame for alpha data decompressed from VP8L */
    AVCodecContext *avctx;              /* parent AVCodecContext */
    int initialized;                    /* set once the VP8 context is initialized */
    int has_alpha;                      /* has a separate alpha chunk */
    enum AlphaCompression alpha_compression; /* compression type for alpha chunk */
    enum AlphaFilter alpha_filter;      /* filtering method for alpha chunk */
    uint8_t *alpha_data;                /* alpha chunk data */
    int alpha_data_size;                /* alpha chunk data size */
    int has_exif;                       /* set after an EXIF chunk has been processed */
    int width;                          /* image width */
    int height;                         /* image height */
    int lossless;                       /* indicates lossless or lossy */

    int nb_transforms;                  /* number of transforms */
    enum TransformType transforms[4];   /* transformations used in the image, in order */
    int reduced_width;                  /* reduced width for index image, if applicable */
    int nb_huffman_groups;              /* number of huffman groups in the primary image */
    ImageContext image[IMAGE_ROLE_NB];  /* image context for each role */
} WebPContext;

#define GET_PIXEL(frame, x, y) \
    ((frame)->data[0] + (y) * frame->linesize[0] + 4 * (x))

#define GET_PIXEL_COMP(frame, x, y, c) \
    (*((frame)->data[0] + (y) * frame->linesize[0] + 4 * (x) + c))

static void image_ctx_free(ImageContext *img)
{
    int i, j;

    av_free(img->color_cache);
    if (img->role != IMAGE_ROLE_ARGB && !img->is_alpha_primary)
        av_frame_free(&img->frame);
    if (img->huffman_groups) {
        for (i = 0; i < img->nb_huffman_groups; i++) {
            for (j = 0; j < HUFFMAN_CODES_PER_META_CODE; j++)
                ff_free_vlc(&img->huffman_groups[i * HUFFMAN_CODES_PER_META_CODE + j].vlc);
        }
        av_free(img->huffman_groups);
    }
    memset(img, 0, sizeof(*img));
}


/* Differs from get_vlc2() in the following ways:
 *   - codes are bit-reversed
 *   - assumes 8-bit table to make reversal simpler
 *   - assumes max depth of 2 since the max code length for WebP is 15
 */
static av_always_inline int webp_get_vlc(GetBitContext *gb, VLC_TYPE (*table)[2])
{
    int n, nb_bits;
    unsigned int index;
    int code;

    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);

    index = SHOW_UBITS(re, gb, 8);
    index = ff_reverse[index];
    code  = table[index][0];
    n     = table[index][1];

    if (n < 0) {
        LAST_SKIP_BITS(re, gb, 8);
        UPDATE_CACHE(re, gb);

        nb_bits = -n;

        index = SHOW_UBITS(re, gb, nb_bits);
        index = (ff_reverse[index] >> (8 - nb_bits)) + code;
        code  = table[index][0];
        n     = table[index][1];
    }
    SKIP_BITS(re, gb, n);

    CLOSE_READER(re, gb);

    return code;
}

static int huff_reader_get_symbol(HuffReader *r, GetBitContext *gb)
{
    if (r->simple) {
        if (r->nb_symbols == 1)
            return r->simple_symbols[0];
        else
            return r->simple_symbols[get_bits1(gb)];
    } else
        return webp_get_vlc(gb, r->vlc.table);
}

static int huff_reader_build_canonical(HuffReader *r, int *code_lengths,
                                       int alphabet_size)
{
    int len = 0, sym, code = 0, ret;
    int max_code_length = 0;
    uint16_t *codes;

    /* special-case 1 symbol since the vlc reader cannot handle it */
    for (sym = 0; sym < alphabet_size; sym++) {
        if (code_lengths[sym] > 0) {
            len++;
            code = sym;
            if (len > 1)
                break;
        }
    }
    if (len == 1) {
        r->nb_symbols = 1;
        r->simple_symbols[0] = code;
        r->simple = 1;
        return 0;
    }

    for (sym = 0; sym < alphabet_size; sym++)
        max_code_length = FFMAX(max_code_length, code_lengths[sym]);

    if (max_code_length == 0 || max_code_length > MAX_HUFFMAN_CODE_LENGTH)
        return AVERROR(EINVAL);

    codes = av_malloc_array(alphabet_size, sizeof(*codes));
    if (!codes)
        return AVERROR(ENOMEM);

    code = 0;
    r->nb_symbols = 0;
    for (len = 1; len <= max_code_length; len++) {
        for (sym = 0; sym < alphabet_size; sym++) {
            if (code_lengths[sym] != len)
                continue;
            codes[sym] = code++;
            r->nb_symbols++;
        }
        code <<= 1;
    }
    if (!r->nb_symbols) {
        av_free(codes);
        return AVERROR_INVALIDDATA;
    }

    ret = init_vlc(&r->vlc, 8, alphabet_size,
                   code_lengths, sizeof(*code_lengths), sizeof(*code_lengths),
                   codes, sizeof(*codes), sizeof(*codes), 0);
    if (ret < 0) {
        av_free(codes);
        return ret;
    }
    r->simple = 0;

    av_free(codes);
    return 0;
}

static void read_huffman_code_simple(WebPContext *s, HuffReader *hc)
{
    hc->nb_symbols = get_bits1(&s->gb) + 1;

    if (get_bits1(&s->gb))
        hc->simple_symbols[0] = get_bits(&s->gb, 8);
    else
        hc->simple_symbols[0] = get_bits1(&s->gb);

    if (hc->nb_symbols == 2)
        hc->simple_symbols[1] = get_bits(&s->gb, 8);

    hc->simple = 1;
}

static int read_huffman_code_normal(WebPContext *s, HuffReader *hc,
                                    int alphabet_size)
{
    HuffReader code_len_hc = { { 0 }, 0, 0, { 0 } };
    int *code_lengths = NULL;
    int code_length_code_lengths[NUM_CODE_LENGTH_CODES] = { 0 };
    int i, symbol, max_symbol, prev_code_len, ret;
    int num_codes = 4 + get_bits(&s->gb, 4);

    if (num_codes > NUM_CODE_LENGTH_CODES)
        return AVERROR_INVALIDDATA;

    for (i = 0; i < num_codes; i++)
        code_length_code_lengths[code_length_code_order[i]] = get_bits(&s->gb, 3);

    ret = huff_reader_build_canonical(&code_len_hc, code_length_code_lengths,
                                      NUM_CODE_LENGTH_CODES);
    if (ret < 0)
        goto finish;

    code_lengths = av_mallocz_array(alphabet_size, sizeof(*code_lengths));
    if (!code_lengths) {
        ret = AVERROR(ENOMEM);
        goto finish;
    }

    if (get_bits1(&s->gb)) {
        int bits   = 2 + 2 * get_bits(&s->gb, 3);
        max_symbol = 2 + get_bits(&s->gb, bits);
        if (max_symbol > alphabet_size) {
            av_log(s->avctx, AV_LOG_ERROR, "max symbol %d > alphabet size %d\n",
                   max_symbol, alphabet_size);
            ret = AVERROR_INVALIDDATA;
            goto finish;
        }
    } else {
        max_symbol = alphabet_size;
    }

    prev_code_len = 8;
    symbol        = 0;
    while (symbol < alphabet_size) {
        int code_len;

        if (!max_symbol--)
            break;
        code_len = huff_reader_get_symbol(&code_len_hc, &s->gb);
        if (code_len < 16) {
            /* Code length code [0..15] indicates literal code lengths. */
            code_lengths[symbol++] = code_len;
            if (code_len)
                prev_code_len = code_len;
        } else {
            int repeat = 0, length = 0;
            switch (code_len) {
            case 16:
                /* Code 16 repeats the previous non-zero value [3..6] times,
                 * i.e., 3 + ReadBits(2) times. If code 16 is used before a
                 * non-zero value has been emitted, a value of 8 is repeated. */
                repeat = 3 + get_bits(&s->gb, 2);
                length = prev_code_len;
                break;
            case 17:
                /* Code 17 emits a streak of zeros [3..10], i.e.,
                 * 3 + ReadBits(3) times. */
                repeat = 3 + get_bits(&s->gb, 3);
                break;
            case 18:
                /* Code 18 emits a streak of zeros of length [11..138], i.e.,
                 * 11 + ReadBits(7) times. */
                repeat = 11 + get_bits(&s->gb, 7);
                break;
            }
            if (symbol + repeat > alphabet_size) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "invalid symbol %d + repeat %d > alphabet size %d\n",
                       symbol, repeat, alphabet_size);
                ret = AVERROR_INVALIDDATA;
                goto finish;
            }
            while (repeat-- > 0)
                code_lengths[symbol++] = length;
        }
    }

    ret = huff_reader_build_canonical(hc, code_lengths, alphabet_size);

finish:
    ff_free_vlc(&code_len_hc.vlc);
    av_free(code_lengths);
    return ret;
}

static int decode_entropy_coded_image(WebPContext *s, enum ImageRole role,
                                      int w, int h);

#define PARSE_BLOCK_SIZE(w, h) do {                                         \
    block_bits = get_bits(&s->gb, 3) + 2;                                   \
    blocks_w   = FFALIGN((w), 1 << block_bits) >> block_bits;               \
    blocks_h   = FFALIGN((h), 1 << block_bits) >> block_bits;               \
} while (0)

static int decode_entropy_image(WebPContext *s)
{
    ImageContext *img;
    int ret, block_bits, width, blocks_w, blocks_h, x, y, max;

    width = s->width;
    if (s->reduced_width > 0)
        width = s->reduced_width;

    PARSE_BLOCK_SIZE(width, s->height);

    ret = decode_entropy_coded_image(s, IMAGE_ROLE_ENTROPY, blocks_w, blocks_h);
    if (ret < 0)
        return ret;

    img = &s->image[IMAGE_ROLE_ENTROPY];
    img->size_reduction = block_bits;

    /* the number of huffman groups is determined by the maximum group number
     * coded in the entropy image */
    max = 0;
    for (y = 0; y < img->frame->height; y++) {
        for (x = 0; x < img->frame->width; x++) {
            int p0 = GET_PIXEL_COMP(img->frame, x, y, 1);
            int p1 = GET_PIXEL_COMP(img->frame, x, y, 2);
            int p  = p0 << 8 | p1;
            max = FFMAX(max, p);
        }
    }
    s->nb_huffman_groups = max + 1;

    return 0;
}

static int parse_transform_predictor(WebPContext *s)
{
    int block_bits, blocks_w, blocks_h, ret;

    PARSE_BLOCK_SIZE(s->width, s->height);

    ret = decode_entropy_coded_image(s, IMAGE_ROLE_PREDICTOR, blocks_w,
                                     blocks_h);
    if (ret < 0)
        return ret;

    s->image[IMAGE_ROLE_PREDICTOR].size_reduction = block_bits;

    return 0;
}

static int parse_transform_color(WebPContext *s)
{
    int block_bits, blocks_w, blocks_h, ret;

    PARSE_BLOCK_SIZE(s->width, s->height);

    ret = decode_entropy_coded_image(s, IMAGE_ROLE_COLOR_TRANSFORM, blocks_w,
                                     blocks_h);
    if (ret < 0)
        return ret;

    s->image[IMAGE_ROLE_COLOR_TRANSFORM].size_reduction = block_bits;

    return 0;
}

static int parse_transform_color_indexing(WebPContext *s)
{
    ImageContext *img;
    int width_bits, index_size, ret, x;
    uint8_t *ct;

    index_size = get_bits(&s->gb, 8) + 1;

    if (index_size <= 2)
        width_bits = 3;
    else if (index_size <= 4)
        width_bits = 2;
    else if (index_size <= 16)
        width_bits = 1;
    else
        width_bits = 0;

    ret = decode_entropy_coded_image(s, IMAGE_ROLE_COLOR_INDEXING,
                                     index_size, 1);
    if (ret < 0)
        return ret;

    img = &s->image[IMAGE_ROLE_COLOR_INDEXING];
    img->size_reduction = width_bits;
    if (width_bits > 0)
        s->reduced_width = (s->width + ((1 << width_bits) - 1)) >> width_bits;

    /* color index values are delta-coded */
    ct  = img->frame->data[0] + 4;
    for (x = 4; x < img->frame->width * 4; x++, ct++)
        ct[0] += ct[-4];

    return 0;
}

static HuffReader *get_huffman_group(WebPContext *s, ImageContext *img,
                                     int x, int y)
{
    ImageContext *gimg = &s->image[IMAGE_ROLE_ENTROPY];
    int group = 0;

    if (gimg->size_reduction > 0) {
        int group_x = x >> gimg->size_reduction;
        int group_y = y >> gimg->size_reduction;
        int g0      = GET_PIXEL_COMP(gimg->frame, group_x, group_y, 1);
        int g1      = GET_PIXEL_COMP(gimg->frame, group_x, group_y, 2);
        group       = g0 << 8 | g1;
    }

    return &img->huffman_groups[group * HUFFMAN_CODES_PER_META_CODE];
}

static av_always_inline void color_cache_put(ImageContext *img, uint32_t c)
{
    uint32_t cache_idx = (0x1E35A7BD * c) >> (32 - img->color_cache_bits);
    img->color_cache[cache_idx] = c;
}

static int decode_entropy_coded_image(WebPContext *s, enum ImageRole role,
                                      int w, int h)
{
    ImageContext *img;
    HuffReader *hg;
    int i, j, ret, x, y, width;

    img       = &s->image[role];
    img->role = role;

    if (!img->frame) {
        img->frame = av_frame_alloc();
        if (!img->frame)
            return AVERROR(ENOMEM);
    }

    img->frame->format = AV_PIX_FMT_ARGB;
    img->frame->width  = w;
    img->frame->height = h;

    if (role == IMAGE_ROLE_ARGB && !img->is_alpha_primary) {
        ThreadFrame pt = { .f = img->frame };
        ret = ff_thread_get_buffer(s->avctx, &pt, 0);
    } else
        ret = av_frame_get_buffer(img->frame, 1);
    if (ret < 0)
        return ret;

    if (get_bits1(&s->gb)) {
        img->color_cache_bits = get_bits(&s->gb, 4);
        if (img->color_cache_bits < 1 || img->color_cache_bits > 11) {
            av_log(s->avctx, AV_LOG_ERROR, "invalid color cache bits: %d\n",
                   img->color_cache_bits);
            return AVERROR_INVALIDDATA;
        }
        img->color_cache = av_mallocz_array(1 << img->color_cache_bits,
                                            sizeof(*img->color_cache));
        if (!img->color_cache)
            return AVERROR(ENOMEM);
    } else {
        img->color_cache_bits = 0;
    }

    img->nb_huffman_groups = 1;
    if (role == IMAGE_ROLE_ARGB && get_bits1(&s->gb)) {
        ret = decode_entropy_image(s);
        if (ret < 0)
            return ret;
        img->nb_huffman_groups = s->nb_huffman_groups;
    }
    img->huffman_groups = av_mallocz_array(img->nb_huffman_groups *
                                           HUFFMAN_CODES_PER_META_CODE,
                                           sizeof(*img->huffman_groups));
    if (!img->huffman_groups)
        return AVERROR(ENOMEM);

    for (i = 0; i < img->nb_huffman_groups; i++) {
        hg = &img->huffman_groups[i * HUFFMAN_CODES_PER_META_CODE];
        for (j = 0; j < HUFFMAN_CODES_PER_META_CODE; j++) {
            int alphabet_size = alphabet_sizes[j];
            if (!j && img->color_cache_bits > 0)
                alphabet_size += 1 << img->color_cache_bits;

            if (get_bits1(&s->gb)) {
                read_huffman_code_simple(s, &hg[j]);
            } else {
                ret = read_huffman_code_normal(s, &hg[j], alphabet_size);
                if (ret < 0)
                    return ret;
            }
        }
    }

    width = img->frame->width;
    if (role == IMAGE_ROLE_ARGB && s->reduced_width > 0)
        width = s->reduced_width;

    x = 0; y = 0;
    while (y < img->frame->height) {
        int v;

        hg = get_huffman_group(s, img, x, y);
        v = huff_reader_get_symbol(&hg[HUFF_IDX_GREEN], &s->gb);
        if (v < NUM_LITERAL_CODES) {
            /* literal pixel values */
            uint8_t *p = GET_PIXEL(img->frame, x, y);
            p[2] = v;
            p[1] = huff_reader_get_symbol(&hg[HUFF_IDX_RED],   &s->gb);
            p[3] = huff_reader_get_symbol(&hg[HUFF_IDX_BLUE],  &s->gb);
            p[0] = huff_reader_get_symbol(&hg[HUFF_IDX_ALPHA], &s->gb);
            if (img->color_cache_bits)
                color_cache_put(img, AV_RB32(p));
            x++;
            if (x == width) {
                x = 0;
                y++;
            }
        } else if (v < NUM_LITERAL_CODES + NUM_LENGTH_CODES) {
            /* LZ77 backwards mapping */
            int prefix_code, length, distance, ref_x, ref_y;

            /* parse length and distance */
            prefix_code = v - NUM_LITERAL_CODES;
            if (prefix_code < 4) {
                length = prefix_code + 1;
            } else {
                int extra_bits = (prefix_code - 2) >> 1;
                int offset     = 2 + (prefix_code & 1) << extra_bits;
                length = offset + get_bits(&s->gb, extra_bits) + 1;
            }
            prefix_code = huff_reader_get_symbol(&hg[HUFF_IDX_DIST], &s->gb);
            if (prefix_code > 39U) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "distance prefix code too large: %d\n", prefix_code);
                return AVERROR_INVALIDDATA;
            }
            if (prefix_code < 4) {
                distance = prefix_code + 1;
            } else {
                int extra_bits = prefix_code - 2 >> 1;
                int offset     = 2 + (prefix_code & 1) << extra_bits;
                distance = offset + get_bits(&s->gb, extra_bits) + 1;
            }

            /* find reference location */
            if (distance <= NUM_SHORT_DISTANCES) {
                int xi = lz77_distance_offsets[distance - 1][0];
                int yi = lz77_distance_offsets[distance - 1][1];
                distance = FFMAX(1, xi + yi * width);
            } else {
                distance -= NUM_SHORT_DISTANCES;
            }
            ref_x = x;
            ref_y = y;
            if (distance <= x) {
                ref_x -= distance;
                distance = 0;
            } else {
                ref_x = 0;
                distance -= x;
            }
            while (distance >= width) {
                ref_y--;
                distance -= width;
            }
            if (distance > 0) {
                ref_x = width - distance;
                ref_y--;
            }
            ref_x = FFMAX(0, ref_x);
            ref_y = FFMAX(0, ref_y);

            /* copy pixels
             * source and dest regions can overlap and wrap lines, so just
             * copy per-pixel */
            for (i = 0; i < length; i++) {
                uint8_t *p_ref = GET_PIXEL(img->frame, ref_x, ref_y);
                uint8_t *p     = GET_PIXEL(img->frame,     x,     y);

                AV_COPY32(p, p_ref);
                if (img->color_cache_bits)
                    color_cache_put(img, AV_RB32(p));
                x++;
                ref_x++;
                if (x == width) {
                    x = 0;
                    y++;
                }
                if (ref_x == width) {
                    ref_x = 0;
                    ref_y++;
                }
                if (y == img->frame->height || ref_y == img->frame->height)
                    break;
            }
        } else {
            /* read from color cache */
            uint8_t *p = GET_PIXEL(img->frame, x, y);
            int cache_idx = v - (NUM_LITERAL_CODES + NUM_LENGTH_CODES);

            if (!img->color_cache_bits) {
                av_log(s->avctx, AV_LOG_ERROR, "color cache not found\n");
                return AVERROR_INVALIDDATA;
            }
            if (cache_idx >= 1 << img->color_cache_bits) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "color cache index out-of-bounds\n");
                return AVERROR_INVALIDDATA;
            }
            AV_WB32(p, img->color_cache[cache_idx]);
            x++;
            if (x == width) {
                x = 0;
                y++;
            }
        }
    }

    return 0;
}

/* PRED_MODE_BLACK */
static void inv_predict_0(uint8_t *p, const uint8_t *p_l, const uint8_t *p_tl,
                          const uint8_t *p_t, const uint8_t *p_tr)
{
    AV_WB32(p, 0xFF000000);
}

/* PRED_MODE_L */
static void inv_predict_1(uint8_t *p, const uint8_t *p_l, const uint8_t *p_tl,
                          const uint8_t *p_t, const uint8_t *p_tr)
{
    AV_COPY32(p, p_l);
}

/* PRED_MODE_T */
static void inv_predict_2(uint8_t *p, const uint8_t *p_l, const uint8_t *p_tl,
                          const uint8_t *p_t, const uint8_t *p_tr)
{
    AV_COPY32(p, p_t);
}

/* PRED_MODE_TR */
static void inv_predict_3(uint8_t *p, const uint8_t *p_l, const uint8_t *p_tl,
                          const uint8_t *p_t, const uint8_t *p_tr)
{
    AV_COPY32(p, p_tr);
}

/* PRED_MODE_TL */
static void inv_predict_4(uint8_t *p, const uint8_t *p_l, const uint8_t *p_tl,
                          const uint8_t *p_t, const uint8_t *p_tr)
{
    AV_COPY32(p, p_tl);
}

/* PRED_MODE_AVG_T_AVG_L_TR */
static void inv_predict_5(uint8_t *p, const uint8_t *p_l, const uint8_t *p_tl,
                          const uint8_t *p_t, const uint8_t *p_tr)
{
    p[0] = p_t[0] + (p_l[0] + p_tr[0] >> 1) >> 1;
    p[1] = p_t[1] + (p_l[1] + p_tr[1] >> 1) >> 1;
    p[2] = p_t[2] + (p_l[2] + p_tr[2] >> 1) >> 1;
    p[3] = p_t[3] + (p_l[3] + p_tr[3] >> 1) >> 1;
}

/* PRED_MODE_AVG_L_TL */
static void inv_predict_6(uint8_t *p, const uint8_t *p_l, const uint8_t *p_tl,
                          const uint8_t *p_t, const uint8_t *p_tr)
{
    p[0] = p_l[0] + p_tl[0] >> 1;
    p[1] = p_l[1] + p_tl[1] >> 1;
    p[2] = p_l[2] + p_tl[2] >> 1;
    p[3] = p_l[3] + p_tl[3] >> 1;
}

/* PRED_MODE_AVG_L_T */
static void inv_predict_7(uint8_t *p, const uint8_t *p_l, const uint8_t *p_tl,
                          const uint8_t *p_t, const uint8_t *p_tr)
{
    p[0] = p_l[0] + p_t[0] >> 1;
    p[1] = p_l[1] + p_t[1] >> 1;
    p[2] = p_l[2] + p_t[2] >> 1;
    p[3] = p_l[3] + p_t[3] >> 1;
}

/* PRED_MODE_AVG_TL_T */
static void inv_predict_8(uint8_t *p, const uint8_t *p_l, const uint8_t *p_tl,
                          const uint8_t *p_t, const uint8_t *p_tr)
{
    p[0] = p_tl[0] + p_t[0] >> 1;
    p[1] = p_tl[1] + p_t[1] >> 1;
    p[2] = p_tl[2] + p_t[2] >> 1;
    p[3] = p_tl[3] + p_t[3] >> 1;
}

/* PRED_MODE_AVG_T_TR */
static void inv_predict_9(uint8_t *p, const uint8_t *p_l, const uint8_t *p_tl,
                          const uint8_t *p_t, const uint8_t *p_tr)
{
    p[0] = p_t[0] + p_tr[0] >> 1;
    p[1] = p_t[1] + p_tr[1] >> 1;
    p[2] = p_t[2] + p_tr[2] >> 1;
    p[3] = p_t[3] + p_tr[3] >> 1;
}

/* PRED_MODE_AVG_AVG_L_TL_AVG_T_TR */
static void inv_predict_10(uint8_t *p, const uint8_t *p_l, const uint8_t *p_tl,
                           const uint8_t *p_t, const uint8_t *p_tr)
{
    p[0] = (p_l[0] + p_tl[0] >> 1) + (p_t[0] + p_tr[0] >> 1) >> 1;
    p[1] = (p_l[1] + p_tl[1] >> 1) + (p_t[1] + p_tr[1] >> 1) >> 1;
    p[2] = (p_l[2] + p_tl[2] >> 1) + (p_t[2] + p_tr[2] >> 1) >> 1;
    p[3] = (p_l[3] + p_tl[3] >> 1) + (p_t[3] + p_tr[3] >> 1) >> 1;
}

/* PRED_MODE_SELECT */
static void inv_predict_11(uint8_t *p, const uint8_t *p_l, const uint8_t *p_tl,
                           const uint8_t *p_t, const uint8_t *p_tr)
{
    int diff = (FFABS(p_l[0] - p_tl[0]) - FFABS(p_t[0] - p_tl[0])) +
               (FFABS(p_l[1] - p_tl[1]) - FFABS(p_t[1] - p_tl[1])) +
               (FFABS(p_l[2] - p_tl[2]) - FFABS(p_t[2] - p_tl[2])) +
               (FFABS(p_l[3] - p_tl[3]) - FFABS(p_t[3] - p_tl[3]));
    if (diff <= 0)
        AV_COPY32(p, p_t);
    else
        AV_COPY32(p, p_l);
}

/* PRED_MODE_ADD_SUBTRACT_FULL */
static void inv_predict_12(uint8_t *p, const uint8_t *p_l, const uint8_t *p_tl,
                           const uint8_t *p_t, const uint8_t *p_tr)
{
    p[0] = av_clip_uint8(p_l[0] + p_t[0] - p_tl[0]);
    p[1] = av_clip_uint8(p_l[1] + p_t[1] - p_tl[1]);
    p[2] = av_clip_uint8(p_l[2] + p_t[2] - p_tl[2]);
    p[3] = av_clip_uint8(p_l[3] + p_t[3] - p_tl[3]);
}

static av_always_inline uint8_t clamp_add_subtract_half(int a, int b, int c)
{
    int d = a + b >> 1;
    return av_clip_uint8(d + (d - c) / 2);
}

/* PRED_MODE_ADD_SUBTRACT_HALF */
static void inv_predict_13(uint8_t *p, const uint8_t *p_l, const uint8_t *p_tl,
                           const uint8_t *p_t, const uint8_t *p_tr)
{
    p[0] = clamp_add_subtract_half(p_l[0], p_t[0], p_tl[0]);
    p[1] = clamp_add_subtract_half(p_l[1], p_t[1], p_tl[1]);
    p[2] = clamp_add_subtract_half(p_l[2], p_t[2], p_tl[2]);
    p[3] = clamp_add_subtract_half(p_l[3], p_t[3], p_tl[3]);
}

typedef void (*inv_predict_func)(uint8_t *p, const uint8_t *p_l,
                                 const uint8_t *p_tl, const uint8_t *p_t,
                                 const uint8_t *p_tr);

static const inv_predict_func inverse_predict[14] = {
    inv_predict_0,  inv_predict_1,  inv_predict_2,  inv_predict_3,
    inv_predict_4,  inv_predict_5,  inv_predict_6,  inv_predict_7,
    inv_predict_8,  inv_predict_9,  inv_predict_10, inv_predict_11,
    inv_predict_12, inv_predict_13,
};

static void inverse_prediction(AVFrame *frame, enum PredictionMode m, int x, int y)
{
    uint8_t *dec, *p_l, *p_tl, *p_t, *p_tr;
    uint8_t p[4];

    dec  = GET_PIXEL(frame, x,     y);
    p_l  = GET_PIXEL(frame, x - 1, y);
    p_tl = GET_PIXEL(frame, x - 1, y - 1);
    p_t  = GET_PIXEL(frame, x,     y - 1);
    if (x == frame->width - 1)
        p_tr = GET_PIXEL(frame, 0, y);
    else
        p_tr = GET_PIXEL(frame, x + 1, y - 1);

    inverse_predict[m](p, p_l, p_tl, p_t, p_tr);

    dec[0] += p[0];
    dec[1] += p[1];
    dec[2] += p[2];
    dec[3] += p[3];
}

static int apply_predictor_transform(WebPContext *s)
{
    ImageContext *img  = &s->image[IMAGE_ROLE_ARGB];
    ImageContext *pimg = &s->image[IMAGE_ROLE_PREDICTOR];
    int x, y;

    for (y = 0; y < img->frame->height; y++) {
        for (x = 0; x < img->frame->width; x++) {
            int tx = x >> pimg->size_reduction;
            int ty = y >> pimg->size_reduction;
            enum PredictionMode m = GET_PIXEL_COMP(pimg->frame, tx, ty, 2);

            if (x == 0) {
                if (y == 0)
                    m = PRED_MODE_BLACK;
                else
                    m = PRED_MODE_T;
            } else if (y == 0)
                m = PRED_MODE_L;

            if (m > 13) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "invalid predictor mode: %d\n", m);
                return AVERROR_INVALIDDATA;
            }
            inverse_prediction(img->frame, m, x, y);
        }
    }
    return 0;
}

static av_always_inline uint8_t color_transform_delta(uint8_t color_pred,
                                                      uint8_t color)
{
    return (int)ff_u8_to_s8(color_pred) * ff_u8_to_s8(color) >> 5;
}

static int apply_color_transform(WebPContext *s)
{
    ImageContext *img, *cimg;
    int x, y, cx, cy;
    uint8_t *p, *cp;

    img  = &s->image[IMAGE_ROLE_ARGB];
    cimg = &s->image[IMAGE_ROLE_COLOR_TRANSFORM];

    for (y = 0; y < img->frame->height; y++) {
        for (x = 0; x < img->frame->width; x++) {
            cx = x >> cimg->size_reduction;
            cy = y >> cimg->size_reduction;
            cp = GET_PIXEL(cimg->frame, cx, cy);
            p  = GET_PIXEL(img->frame,   x,  y);

            p[1] += color_transform_delta(cp[3], p[2]);
            p[3] += color_transform_delta(cp[2], p[2]) +
                    color_transform_delta(cp[1], p[1]);
        }
    }
    return 0;
}

static int apply_subtract_green_transform(WebPContext *s)
{
    int x, y;
    ImageContext *img = &s->image[IMAGE_ROLE_ARGB];

    for (y = 0; y < img->frame->height; y++) {
        for (x = 0; x < img->frame->width; x++) {
            uint8_t *p = GET_PIXEL(img->frame, x, y);
            p[1] += p[2];
            p[3] += p[2];
        }
    }
    return 0;
}

static int apply_color_indexing_transform(WebPContext *s)
{
    ImageContext *img;
    ImageContext *pal;
    int i, x, y;
    uint8_t *p;

    img = &s->image[IMAGE_ROLE_ARGB];
    pal = &s->image[IMAGE_ROLE_COLOR_INDEXING];

    if (pal->size_reduction > 0) {
        GetBitContext gb_g;
        uint8_t *line;
        int pixel_bits = 8 >> pal->size_reduction;

        line = av_malloc(img->frame->linesize[0] + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!line)
            return AVERROR(ENOMEM);

        for (y = 0; y < img->frame->height; y++) {
            p = GET_PIXEL(img->frame, 0, y);
            memcpy(line, p, img->frame->linesize[0]);
            init_get_bits(&gb_g, line, img->frame->linesize[0] * 8);
            skip_bits(&gb_g, 16);
            i = 0;
            for (x = 0; x < img->frame->width; x++) {
                p    = GET_PIXEL(img->frame, x, y);
                p[2] = get_bits(&gb_g, pixel_bits);
                i++;
                if (i == 1 << pal->size_reduction) {
                    skip_bits(&gb_g, 24);
                    i = 0;
                }
            }
        }
        av_free(line);
    }

    // switch to local palette if it's worth initializing it
    if (img->frame->height * img->frame->width > 300) {
        uint8_t palette[256 * 4];
        const int size = pal->frame->width * 4;
        av_assert0(size <= 1024U);
        memcpy(palette, GET_PIXEL(pal->frame, 0, 0), size);   // copy palette
        // set extra entries to transparent black
        memset(palette + size, 0, 256 * 4 - size);
        for (y = 0; y < img->frame->height; y++) {
            for (x = 0; x < img->frame->width; x++) {
                p = GET_PIXEL(img->frame, x, y);
                i = p[2];
                AV_COPY32(p, &palette[i * 4]);
            }
        }
    } else {
        for (y = 0; y < img->frame->height; y++) {
            for (x = 0; x < img->frame->width; x++) {
                p = GET_PIXEL(img->frame, x, y);
                i = p[2];
                if (i >= pal->frame->width) {
                    AV_WB32(p, 0x00000000);
                } else {
                    const uint8_t *pi = GET_PIXEL(pal->frame, i, 0);
                    AV_COPY32(p, pi);
                }
            }
        }
    }

    return 0;
}

static void update_canvas_size(AVCodecContext *avctx, int w, int h)
{
    WebPContext *s = avctx->priv_data;
    if (s->width && s->width != w) {
        av_log(avctx, AV_LOG_WARNING, "Width mismatch. %d != %d\n",
               s->width, w);
    }
    s->width = w;
    if (s->height && s->height != h) {
        av_log(avctx, AV_LOG_WARNING, "Height mismatch. %d != %d\n",
               s->height, h);
    }
    s->height = h;
}

static int vp8_lossless_decode_frame(AVCodecContext *avctx, AVFrame *p,
                                     int *got_frame, uint8_t *data_start,
                                     unsigned int data_size, int is_alpha_chunk)
{
    WebPContext *s = avctx->priv_data;
    int w, h, ret, i, used;

    if (!is_alpha_chunk) {
        s->lossless = 1;
        avctx->pix_fmt = AV_PIX_FMT_ARGB;
    }

    ret = init_get_bits8(&s->gb, data_start, data_size);
    if (ret < 0)
        return ret;

    if (!is_alpha_chunk) {
        if (get_bits(&s->gb, 8) != 0x2F) {
            av_log(avctx, AV_LOG_ERROR, "Invalid WebP Lossless signature\n");
            return AVERROR_INVALIDDATA;
        }

        w = get_bits(&s->gb, 14) + 1;
        h = get_bits(&s->gb, 14) + 1;

        update_canvas_size(avctx, w, h);

        ret = ff_set_dimensions(avctx, s->width, s->height);
        if (ret < 0)
            return ret;

        s->has_alpha = get_bits1(&s->gb);

        if (get_bits(&s->gb, 3) != 0x0) {
            av_log(avctx, AV_LOG_ERROR, "Invalid WebP Lossless version\n");
            return AVERROR_INVALIDDATA;
        }
    } else {
        if (!s->width || !s->height)
            return AVERROR_BUG;
        w = s->width;
        h = s->height;
    }

    /* parse transformations */
    s->nb_transforms = 0;
    s->reduced_width = 0;
    used = 0;
    while (get_bits1(&s->gb)) {
        enum TransformType transform = get_bits(&s->gb, 2);
        if (used & (1 << transform)) {
            av_log(avctx, AV_LOG_ERROR, "Transform %d used more than once\n",
                   transform);
            ret = AVERROR_INVALIDDATA;
            goto free_and_return;
        }
        used |= (1 << transform);
        s->transforms[s->nb_transforms++] = transform;
        switch (transform) {
        case PREDICTOR_TRANSFORM:
            ret = parse_transform_predictor(s);
            break;
        case COLOR_TRANSFORM:
            ret = parse_transform_color(s);
            break;
        case COLOR_INDEXING_TRANSFORM:
            ret = parse_transform_color_indexing(s);
            break;
        }
        if (ret < 0)
            goto free_and_return;
    }

    /* decode primary image */
    s->image[IMAGE_ROLE_ARGB].frame = p;
    if (is_alpha_chunk)
        s->image[IMAGE_ROLE_ARGB].is_alpha_primary = 1;
    ret = decode_entropy_coded_image(s, IMAGE_ROLE_ARGB, w, h);
    if (ret < 0)
        goto free_and_return;

    /* apply transformations */
    for (i = s->nb_transforms - 1; i >= 0; i--) {
        switch (s->transforms[i]) {
        case PREDICTOR_TRANSFORM:
            ret = apply_predictor_transform(s);
            break;
        case COLOR_TRANSFORM:
            ret = apply_color_transform(s);
            break;
        case SUBTRACT_GREEN:
            ret = apply_subtract_green_transform(s);
            break;
        case COLOR_INDEXING_TRANSFORM:
            ret = apply_color_indexing_transform(s);
            break;
        }
        if (ret < 0)
            goto free_and_return;
    }

    *got_frame   = 1;
    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;
    ret          = data_size;

free_and_return:
    for (i = 0; i < IMAGE_ROLE_NB; i++)
        image_ctx_free(&s->image[i]);

    return ret;
}

static void alpha_inverse_prediction(AVFrame *frame, enum AlphaFilter m)
{
    int x, y, ls;
    uint8_t *dec;

    ls = frame->linesize[3];

    /* filter first row using horizontal filter */
    dec = frame->data[3] + 1;
    for (x = 1; x < frame->width; x++, dec++)
        *dec += *(dec - 1);

    /* filter first column using vertical filter */
    dec = frame->data[3] + ls;
    for (y = 1; y < frame->height; y++, dec += ls)
        *dec += *(dec - ls);

    /* filter the rest using the specified filter */
    switch (m) {
    case ALPHA_FILTER_HORIZONTAL:
        for (y = 1; y < frame->height; y++) {
            dec = frame->data[3] + y * ls + 1;
            for (x = 1; x < frame->width; x++, dec++)
                *dec += *(dec - 1);
        }
        break;
    case ALPHA_FILTER_VERTICAL:
        for (y = 1; y < frame->height; y++) {
            dec = frame->data[3] + y * ls + 1;
            for (x = 1; x < frame->width; x++, dec++)
                *dec += *(dec - ls);
        }
        break;
    case ALPHA_FILTER_GRADIENT:
        for (y = 1; y < frame->height; y++) {
            dec = frame->data[3] + y * ls + 1;
            for (x = 1; x < frame->width; x++, dec++)
                dec[0] += av_clip_uint8(*(dec - 1) + *(dec - ls) - *(dec - ls - 1));
        }
        break;
    }
}

static int vp8_lossy_decode_alpha(AVCodecContext *avctx, AVFrame *p,
                                  uint8_t *data_start,
                                  unsigned int data_size)
{
    WebPContext *s = avctx->priv_data;
    int x, y, ret;

    if (s->alpha_compression == ALPHA_COMPRESSION_NONE) {
        GetByteContext gb;

        bytestream2_init(&gb, data_start, data_size);
        for (y = 0; y < s->height; y++)
            bytestream2_get_buffer(&gb, p->data[3] + p->linesize[3] * y,
                                   s->width);
    } else if (s->alpha_compression == ALPHA_COMPRESSION_VP8L) {
        uint8_t *ap, *pp;
        int alpha_got_frame = 0;

        s->alpha_frame = av_frame_alloc();
        if (!s->alpha_frame)
            return AVERROR(ENOMEM);

        ret = vp8_lossless_decode_frame(avctx, s->alpha_frame, &alpha_got_frame,
                                        data_start, data_size, 1);
        if (ret < 0) {
            av_frame_free(&s->alpha_frame);
            return ret;
        }
        if (!alpha_got_frame) {
            av_frame_free(&s->alpha_frame);
            return AVERROR_INVALIDDATA;
        }

        /* copy green component of alpha image to alpha plane of primary image */
        for (y = 0; y < s->height; y++) {
            ap = GET_PIXEL(s->alpha_frame, 0, y) + 2;
            pp = p->data[3] + p->linesize[3] * y;
            for (x = 0; x < s->width; x++) {
                *pp = *ap;
                pp++;
                ap += 4;
            }
        }
        av_frame_free(&s->alpha_frame);
    }

    /* apply alpha filtering */
    if (s->alpha_filter)
        alpha_inverse_prediction(p, s->alpha_filter);

    return 0;
}

static int vp8_lossy_decode_frame(AVCodecContext *avctx, AVFrame *p,
                                  int *got_frame, uint8_t *data_start,
                                  unsigned int data_size)
{
    WebPContext *s = avctx->priv_data;
    AVPacket pkt;
    int ret;

    if (!s->initialized) {
        ff_vp8_decode_init(avctx);
        s->initialized = 1;
    }
    avctx->pix_fmt = s->has_alpha ? AV_PIX_FMT_YUVA420P : AV_PIX_FMT_YUV420P;
    s->lossless = 0;

    if (data_size > INT_MAX) {
        av_log(avctx, AV_LOG_ERROR, "unsupported chunk size\n");
        return AVERROR_PATCHWELCOME;
    }

    av_init_packet(&pkt);
    pkt.data = data_start;
    pkt.size = data_size;

    ret = ff_vp8_decode_frame(avctx, p, got_frame, &pkt);
    if (ret < 0)
        return ret;

    if (!*got_frame)
        return AVERROR_INVALIDDATA;

    update_canvas_size(avctx, avctx->width, avctx->height);

    if (s->has_alpha) {
        ret = vp8_lossy_decode_alpha(avctx, p, s->alpha_data,
                                     s->alpha_data_size);
        if (ret < 0)
            return ret;
    }
    return ret;
}

static int webp_decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                             AVPacket *avpkt)
{
    AVFrame * const p = data;
    WebPContext *s = avctx->priv_data;
    GetByteContext gb;
    int ret;
    uint32_t chunk_type, chunk_size;
    int vp8x_flags = 0;

    s->avctx     = avctx;
    s->width     = 0;
    s->height    = 0;
    *got_frame   = 0;
    s->has_alpha = 0;
    s->has_exif  = 0;
    bytestream2_init(&gb, avpkt->data, avpkt->size);

    if (bytestream2_get_bytes_left(&gb) < 12)
        return AVERROR_INVALIDDATA;

    if (bytestream2_get_le32(&gb) != MKTAG('R', 'I', 'F', 'F')) {
        av_log(avctx, AV_LOG_ERROR, "missing RIFF tag\n");
        return AVERROR_INVALIDDATA;
    }

    chunk_size = bytestream2_get_le32(&gb);
    if (bytestream2_get_bytes_left(&gb) < chunk_size)
        return AVERROR_INVALIDDATA;

    if (bytestream2_get_le32(&gb) != MKTAG('W', 'E', 'B', 'P')) {
        av_log(avctx, AV_LOG_ERROR, "missing WEBP tag\n");
        return AVERROR_INVALIDDATA;
    }

    while (bytestream2_get_bytes_left(&gb) > 8) {
        char chunk_str[5] = { 0 };

        chunk_type = bytestream2_get_le32(&gb);
        chunk_size = bytestream2_get_le32(&gb);
        if (chunk_size == UINT32_MAX)
            return AVERROR_INVALIDDATA;
        chunk_size += chunk_size & 1;

        if (bytestream2_get_bytes_left(&gb) < chunk_size)
            return AVERROR_INVALIDDATA;

        switch (chunk_type) {
        case MKTAG('V', 'P', '8', ' '):
            if (!*got_frame) {
                ret = vp8_lossy_decode_frame(avctx, p, got_frame,
                                             avpkt->data + bytestream2_tell(&gb),
                                             chunk_size);
                if (ret < 0)
                    return ret;
            }
            bytestream2_skip(&gb, chunk_size);
            break;
        case MKTAG('V', 'P', '8', 'L'):
            if (!*got_frame) {
                ret = vp8_lossless_decode_frame(avctx, p, got_frame,
                                                avpkt->data + bytestream2_tell(&gb),
                                                chunk_size, 0);
                if (ret < 0)
                    return ret;
                avctx->properties |= FF_CODEC_PROPERTY_LOSSLESS;
            }
            bytestream2_skip(&gb, chunk_size);
            break;
        case MKTAG('V', 'P', '8', 'X'):
            if (s->width || s->height || *got_frame) {
                av_log(avctx, AV_LOG_ERROR, "Canvas dimensions are already set\n");
                return AVERROR_INVALIDDATA;
            }
            vp8x_flags = bytestream2_get_byte(&gb);
            bytestream2_skip(&gb, 3);
            s->width  = bytestream2_get_le24(&gb) + 1;
            s->height = bytestream2_get_le24(&gb) + 1;
            ret = av_image_check_size(s->width, s->height, 0, avctx);
            if (ret < 0)
                return ret;
            break;
        case MKTAG('A', 'L', 'P', 'H'): {
            int alpha_header, filter_m, compression;

            if (!(vp8x_flags & VP8X_FLAG_ALPHA)) {
                av_log(avctx, AV_LOG_WARNING,
                       "ALPHA chunk present, but alpha bit not set in the "
                       "VP8X header\n");
            }
            if (chunk_size == 0) {
                av_log(avctx, AV_LOG_ERROR, "invalid ALPHA chunk size\n");
                return AVERROR_INVALIDDATA;
            }
            alpha_header       = bytestream2_get_byte(&gb);
            s->alpha_data      = avpkt->data + bytestream2_tell(&gb);
            s->alpha_data_size = chunk_size - 1;
            bytestream2_skip(&gb, s->alpha_data_size);

            filter_m    = (alpha_header >> 2) & 0x03;
            compression =  alpha_header       & 0x03;

            if (compression > ALPHA_COMPRESSION_VP8L) {
                av_log(avctx, AV_LOG_VERBOSE,
                       "skipping unsupported ALPHA chunk\n");
            } else {
                s->has_alpha         = 1;
                s->alpha_compression = compression;
                s->alpha_filter      = filter_m;
            }

            break;
        }
        case MKTAG('E', 'X', 'I', 'F'): {
            int le, ifd_offset, exif_offset = bytestream2_tell(&gb);
            AVDictionary *exif_metadata = NULL;
            GetByteContext exif_gb;

            if (s->has_exif) {
                av_log(avctx, AV_LOG_VERBOSE, "Ignoring extra EXIF chunk\n");
                goto exif_end;
            }
            if (!(vp8x_flags & VP8X_FLAG_EXIF_METADATA))
                av_log(avctx, AV_LOG_WARNING,
                       "EXIF chunk present, but Exif bit not set in the "
                       "VP8X header\n");

            s->has_exif = 1;
            bytestream2_init(&exif_gb, avpkt->data + exif_offset,
                             avpkt->size - exif_offset);
            if (ff_tdecode_header(&exif_gb, &le, &ifd_offset) < 0) {
                av_log(avctx, AV_LOG_ERROR, "invalid TIFF header "
                       "in Exif data\n");
                goto exif_end;
            }

            bytestream2_seek(&exif_gb, ifd_offset, SEEK_SET);
            if (avpriv_exif_decode_ifd(avctx, &exif_gb, le, 0, &exif_metadata) < 0) {
                av_log(avctx, AV_LOG_ERROR, "error decoding Exif data\n");
                goto exif_end;
            }

            av_dict_copy(&((AVFrame *) data)->metadata, exif_metadata, 0);

exif_end:
            av_dict_free(&exif_metadata);
            bytestream2_skip(&gb, chunk_size);
            break;
        }
        case MKTAG('I', 'C', 'C', 'P'):
        case MKTAG('A', 'N', 'I', 'M'):
        case MKTAG('A', 'N', 'M', 'F'):
        case MKTAG('X', 'M', 'P', ' '):
            AV_WL32(chunk_str, chunk_type);
            av_log(avctx, AV_LOG_WARNING, "skipping unsupported chunk: %s\n",
                   chunk_str);
            bytestream2_skip(&gb, chunk_size);
            break;
        default:
            AV_WL32(chunk_str, chunk_type);
            av_log(avctx, AV_LOG_VERBOSE, "skipping unknown chunk: %s\n",
                   chunk_str);
            bytestream2_skip(&gb, chunk_size);
            break;
        }
    }

    if (!*got_frame) {
        av_log(avctx, AV_LOG_ERROR, "image data not found\n");
        return AVERROR_INVALIDDATA;
    }

    return avpkt->size;
}

static av_cold int webp_decode_close(AVCodecContext *avctx)
{
    WebPContext *s = avctx->priv_data;

    if (s->initialized)
        return ff_vp8_decode_free(avctx);

    return 0;
}

AVCodec ff_webp_decoder = {
    .name           = "webp",
    .long_name      = NULL_IF_CONFIG_SMALL("WebP image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_WEBP,
    .priv_data_size = sizeof(WebPContext),
    .decode         = webp_decode_frame,
    .close          = webp_decode_close,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
};
