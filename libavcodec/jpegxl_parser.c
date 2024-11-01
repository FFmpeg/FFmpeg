/**
 * JPEG XL parser
 * Copyright (c) 2023 Leo Izen <leo.izen@gmail.com>
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

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "libavutil/attributes.h"
#include "libavutil/error.h"
#include "libavutil/intmath.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/pixfmt.h"

#include "bytestream.h"
#include "codec_id.h"
#define UNCHECKED_BITSTREAM_READER 0
#define BITSTREAM_READER_LE
#include "get_bits.h"
#include "jpegxl.h"
#include "jpegxl_parse.h"
#include "parser.h"
#include "vlc.h"

#define JXL_FLAG_NOISE 1
#define JXL_FLAG_PATCHES 2
#define JXL_FLAG_SPLINES 16
#define JXL_FLAG_USE_LF_FRAME 32
#define JXL_FLAG_SKIP_ADAPTIVE_LF_SMOOTH 128

#define MAX_PREFIX_ALPHABET_SIZE (1u << 15)

#define clog1p(x) (ff_log2(x) + !!(x))
#define unpack_signed(x) (((x) & 1 ? -(x)-1 : (x))/2)
#define div_ceil(x, y) (((x) - 1) / (y) + 1)
#define vlm(a,b) {.sym = (a), .len = (b)}

typedef struct JXLHybridUintConf {
    int split_exponent;
    uint32_t msb_in_token;
    uint32_t lsb_in_token;
} JXLHybridUintConf;

typedef struct JXLSymbolDistribution {
    JXLHybridUintConf config;
    int log_bucket_size;
    /* this is the actual size of the alphabet */
    int alphabet_size;
    /* ceil(log(alphabet_size)) */
    int log_alphabet_size;

    /* for prefix code distributions */
    VLC vlc;
    /* in case bits == 0 */
    uint32_t default_symbol;

    /*
     * each (1 << log_alphabet_size) length
     * with log_alphabet_size <= 8
     */
    /* frequencies associated with this Distribution */
    uint32_t freq[258];
    /* cutoffs for using the symbol table */
    uint16_t cutoffs[258];
    /* the symbol table for this distribution */
    uint16_t symbols[258];
    /* the offset for symbols */
    uint16_t offsets[258];

    /* if this distribution contains only one symbol this is its index */
    int uniq_pos;
} JXLSymbolDistribution;

typedef struct JXLDistributionBundle {
    /* lz77 flags */
    int lz77_enabled;
    uint32_t lz77_min_symbol;
    uint32_t lz77_min_length;
    JXLHybridUintConf lz_len_conf;

    /* one entry for each distribution */
    uint8_t *cluster_map;
    /* length of cluster_map */
    int num_dist;

    /* one for each cluster */
    JXLSymbolDistribution *dists;
    int num_clusters;

    /* whether to use brotli prefixes or ans */
    int use_prefix_code;
    /* bundle log alphabet size, dist ones may be smaller */
    int log_alphabet_size;
} JXLDistributionBundle;

typedef struct JXLEntropyDecoder {

    /* state is a positive 32-bit integer, or -1 if unset */
    int64_t state;

    /* lz77 values */
    uint32_t num_to_copy;
    uint32_t copy_pos;
    uint32_t num_decoded;

    /* length is (1 << 20) */
    /* if lz77 is enabled for this bundle */
    /* if lz77 is disabled it's NULL */
    uint32_t *window;

    /* primary bundle associated with this distribution */
    JXLDistributionBundle bundle;

    /* for av_log */
    void *logctx;
} JXLEntropyDecoder;

typedef struct JXLFrame {
    FFJXLFrameType type;
    FFJXLFrameEncoding encoding;

    int is_last;
    int full_frame;

    uint32_t total_length;
    uint32_t body_length;
} JXLFrame;

typedef struct JXLCodestream {
    FFJXLMetadata meta;
    JXLFrame frame;
} JXLCodestream;

typedef struct JXLParseContext {
    ParseContext pc;
    JXLCodestream codestream;

    /* using ISOBMFF-based container */
    int container;
    int skip;
    int copied;
    int collected_size;
    int codestream_length;
    int skipped_icc;
    int next;

    uint8_t cs_buffer[4096 + AV_INPUT_BUFFER_PADDING_SIZE];
} JXLParseContext;

/* used for reading brotli prefixes */
static const VLCElem level0_table[16] = {
    vlm(0, 2), vlm(4, 2), vlm(3, 2), vlm(2, 3), vlm(0, 2), vlm(4, 2), vlm(3, 2), vlm(1, 4),
    vlm(0, 2), vlm(4, 2), vlm(3, 2), vlm(2, 3), vlm(0, 2), vlm(4, 2), vlm(3, 2), vlm(5, 4),
};

/* prefix table for populating ANS distribution */
static const VLCElem dist_prefix_table[128] = {
    vlm(10, 3), vlm(12, 7), vlm(7, 3), vlm(3, 4), vlm(6, 3), vlm(8, 3), vlm(9, 3), vlm(5, 4),
    vlm(10, 3), vlm(4, 4),  vlm(7, 3), vlm(1, 4), vlm(6, 3), vlm(8, 3), vlm(9, 3), vlm(2, 4),
    vlm(10, 3), vlm(0, 5),  vlm(7, 3), vlm(3, 4), vlm(6, 3), vlm(8, 3), vlm(9, 3), vlm(5, 4),
    vlm(10, 3), vlm(4, 4),  vlm(7, 3), vlm(1, 4), vlm(6, 3), vlm(8, 3), vlm(9, 3), vlm(2, 4),
    vlm(10, 3), vlm(11, 6), vlm(7, 3), vlm(3, 4), vlm(6, 3), vlm(8, 3), vlm(9, 3), vlm(5, 4),
    vlm(10, 3), vlm(4, 4),  vlm(7, 3), vlm(1, 4), vlm(6, 3), vlm(8, 3), vlm(9, 3), vlm(2, 4),
    vlm(10, 3), vlm(0, 5),  vlm(7, 3), vlm(3, 4), vlm(6, 3), vlm(8, 3), vlm(9, 3), vlm(5, 4),
    vlm(10, 3), vlm(4, 4),  vlm(7, 3), vlm(1, 4), vlm(6, 3), vlm(8, 3), vlm(9, 3), vlm(2, 4),
    vlm(10, 3), vlm(13, 7), vlm(7, 3), vlm(3, 4), vlm(6, 3), vlm(8, 3), vlm(9, 3), vlm(5, 4),
    vlm(10, 3), vlm(4, 4),  vlm(7, 3), vlm(1, 4), vlm(6, 3), vlm(8, 3), vlm(9, 3), vlm(2, 4),
    vlm(10, 3), vlm(0, 5),  vlm(7, 3), vlm(3, 4), vlm(6, 3), vlm(8, 3), vlm(9, 3), vlm(5, 4),
    vlm(10, 3), vlm(4, 4),  vlm(7, 3), vlm(1, 4), vlm(6, 3), vlm(8, 3), vlm(9, 3), vlm(2, 4),
    vlm(10, 3), vlm(11, 6), vlm(7, 3), vlm(3, 4), vlm(6, 3), vlm(8, 3), vlm(9, 3), vlm(5, 4),
    vlm(10, 3), vlm(4, 4),  vlm(7, 3), vlm(1, 4), vlm(6, 3), vlm(8, 3), vlm(9, 3), vlm(2, 4),
    vlm(10, 3), vlm(0, 5),  vlm(7, 3), vlm(3, 4), vlm(6, 3), vlm(8, 3), vlm(9, 3), vlm(5, 4),
    vlm(10, 3), vlm(4, 4),  vlm(7, 3), vlm(1, 4), vlm(6, 3), vlm(8, 3), vlm(9, 3), vlm(2, 4),
};

static const uint8_t prefix_codelen_map[18] = {
    1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};

/**
 * Read a variable-length 8-bit integer.
 * Used when populating the ANS frequency tables.
 */
static av_always_inline uint8_t jxl_u8(GetBitContext *gb)
{
    int n;
    if (!get_bits1(gb))
        return 0;
    n = get_bits(gb, 3);

    return get_bitsz(gb, n) | (1 << n);
}

/* read a U32(c_i + u(u_i)) */
static av_always_inline uint32_t jxl_u32(GetBitContext *gb,
                        uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3,
                        uint32_t u0, uint32_t u1, uint32_t u2, uint32_t u3)
{
    const uint32_t constants[4] = {c0, c1, c2, c3};
    const uint32_t ubits    [4] = {u0, u1, u2, u3};
    uint32_t ret, choice = get_bits(gb, 2);

    ret = constants[choice];
    if (ubits[choice])
        ret += get_bits_long(gb, ubits[choice]);

    return ret;
}

/* read a U64() */
static uint64_t jxl_u64(GetBitContext *gb)
{
    uint64_t shift = 12, ret;

    switch (get_bits(gb, 2)) {
    case 1:
        ret = 1 + get_bits(gb, 4);
        break;
    case 2:
        ret = 17 + get_bits(gb, 8);
        break;
    case 3:
        ret = get_bits(gb, 12);
        while (get_bits1(gb)) {
            if (shift < 60) {
                ret |= (uint64_t)get_bits(gb, 8) << shift;
                shift += 8;
            } else {
                ret |= (uint64_t)get_bits(gb, 4) << shift;
                break;
            }
        }
        break;
    default:
        ret = 0;
    }

    return ret;
}

static int read_hybrid_uint_conf(GetBitContext *gb, JXLHybridUintConf *conf, int log_alphabet_size)
{
    conf->split_exponent = get_bitsz(gb, clog1p(log_alphabet_size));
    if (conf->split_exponent == log_alphabet_size) {
        conf->msb_in_token = conf->lsb_in_token = 0;
        return 0;
    }

    conf->msb_in_token = get_bitsz(gb, clog1p(conf->split_exponent));
    if (conf->msb_in_token > conf->split_exponent)
        return AVERROR_INVALIDDATA;
    conf->lsb_in_token = get_bitsz(gb, clog1p(conf->split_exponent - conf->msb_in_token));
    if (conf->msb_in_token + conf->lsb_in_token > conf->split_exponent)
        return AVERROR_INVALIDDATA;

    return 0;
}

static int read_hybrid_uint(GetBitContext *gb, const JXLHybridUintConf *conf, uint32_t token, uint32_t *hybrid_uint)
{
    uint32_t n, low, split = 1 << conf->split_exponent;

    if (token < split) {
        *hybrid_uint = token;
        return 0;
    }

    n = conf->split_exponent - conf->lsb_in_token - conf->msb_in_token +
        ((token - split) >> (conf->msb_in_token + conf->lsb_in_token));
    if (n >= 32)
        return AVERROR_INVALIDDATA;
    low = token & ((1 << conf->lsb_in_token) - 1);
    token >>= conf->lsb_in_token;
    token &= (1 << conf->msb_in_token) - 1;
    token |= 1 << conf->msb_in_token;
    *hybrid_uint = (((token << n) | get_bits_long(gb, n)) << conf->lsb_in_token ) | low;

    return 0;
}

static inline uint32_t read_prefix_symbol(GetBitContext *gb, const JXLSymbolDistribution *dist)
{
    if (!dist->vlc.bits)
        return dist->default_symbol;

    return get_vlc2(gb, dist->vlc.table, dist->vlc.bits, 1);
}

static uint32_t read_ans_symbol(GetBitContext *gb, JXLEntropyDecoder *dec, const JXLSymbolDistribution *dist)
{
    uint32_t index, i, pos, symbol, offset;

    if (dec->state < 0)
        dec->state = get_bits_long(gb, 32);

    index = dec->state & 0xFFF;
    i = index >> dist->log_bucket_size;
    pos = index & ((1 << dist->log_bucket_size) - 1);
    symbol = pos >= dist->cutoffs[i] ? dist->symbols[i] : i;
    offset = pos >= dist->cutoffs[i] ? dist->offsets[i] + pos : pos;
    dec->state = dist->freq[symbol] * (dec->state >> 12) + offset;
    if (dec->state < (1 << 16))
        dec->state = (dec->state << 16) | get_bits(gb, 16);
    dec->state &= 0xFFFFFFFF;

    return symbol;
}

static int decode_hybrid_varlen_uint(GetBitContext *gb, JXLEntropyDecoder *dec,
                                     const JXLDistributionBundle *bundle,
                                     uint32_t context, uint32_t *hybrid_uint)
{
    int ret;
    uint32_t token, distance;
    const JXLSymbolDistribution *dist;

    if (dec->num_to_copy > 0) {
        *hybrid_uint = dec->window[dec->copy_pos++ & 0xFFFFF];
        dec->num_to_copy--;
        dec->window[dec->num_decoded++ & 0xFFFFF] = *hybrid_uint;
        return 0;
    }

    if (context >= bundle->num_dist)
        return AVERROR(EINVAL);
    if (bundle->cluster_map[context] >= bundle->num_clusters)
        return AVERROR_INVALIDDATA;

    dist = &bundle->dists[bundle->cluster_map[context]];
    if (bundle->use_prefix_code)
        token = read_prefix_symbol(gb, dist);
    else
        token = read_ans_symbol(gb, dec, dist);

    if (bundle->lz77_enabled && token >= bundle->lz77_min_symbol) {
        const JXLSymbolDistribution *lz77dist = &bundle->dists[bundle->cluster_map[bundle->num_dist - 1]];
        ret = read_hybrid_uint(gb, &bundle->lz_len_conf, token - bundle->lz77_min_symbol, &dec->num_to_copy);
        if (ret < 0)
            return ret;
        dec->num_to_copy += bundle->lz77_min_length;
        if (bundle->use_prefix_code)
            token = read_prefix_symbol(gb, lz77dist);
        else
            token = read_ans_symbol(gb, dec, lz77dist);
        ret = read_hybrid_uint(gb, &lz77dist->config, token, &distance);
        if (ret < 0)
            return ret;
        distance++;
        distance = FFMIN3(distance, dec->num_decoded, 1 << 20);
        dec->copy_pos = dec->num_decoded - distance;
        return decode_hybrid_varlen_uint(gb, dec, bundle, context, hybrid_uint);
    }
    ret = read_hybrid_uint(gb, &dist->config, token, hybrid_uint);
    if (ret < 0)
        return ret;
    if (bundle->lz77_enabled)
        dec->window[dec->num_decoded++ & 0xFFFFF] = *hybrid_uint;

    return 0;
}

static int populate_distribution(GetBitContext *gb, JXLSymbolDistribution *dist, int log_alphabet_size)
{
    int len = 0, shift, omit_log = -1, omit_pos = -1;
    int prev = 0, num_same = 0;
    uint32_t total_count = 0;
    uint8_t logcounts[258] = { 0 };
    uint8_t same[258] = { 0 };
    const int table_size = 1 << log_alphabet_size;
    dist->uniq_pos = -1;

    if (get_bits1(gb)) {
        /* simple code */
        if (get_bits1(gb)) {
            uint8_t v1 = jxl_u8(gb);
            uint8_t v2 = jxl_u8(gb);
            if (v1 == v2)
                return AVERROR_INVALIDDATA;
            dist->freq[v1] = get_bits(gb, 12);
            dist->freq[v2] = (1 << 12) - dist->freq[v1];
            if (!dist->freq[v1])
                dist->uniq_pos = v2;
            dist->alphabet_size = 1 + FFMAX(v1, v2);
        } else {
            uint8_t x = jxl_u8(gb);
            dist->freq[x] = 1 << 12;
            dist->uniq_pos = x;
            dist->alphabet_size = 1 + x;
        }
        if (dist->alphabet_size > table_size)
            return AVERROR_INVALIDDATA;

        return 0;
    }

    if (get_bits1(gb)) {
        /* flat code */
        dist->alphabet_size = jxl_u8(gb) + 1;
        if (dist->alphabet_size > table_size)
            return AVERROR_INVALIDDATA;
        for (int i = 0; i < dist->alphabet_size; i++)
            dist->freq[i] = (1 << 12) / dist->alphabet_size;
        for (int i = 0; i < (1 << 12) % dist->alphabet_size; i++)
            dist->freq[i]++;
        return 0;
    }

    do {
        if (!get_bits1(gb))
            break;
    } while (++len < 3);

    shift = (get_bitsz(gb, len) | (1 << len)) - 1;
    if (shift > 13)
        return AVERROR_INVALIDDATA;

    dist->alphabet_size = jxl_u8(gb) + 3;
    if (dist->alphabet_size > table_size)
        return AVERROR_INVALIDDATA;

    for (int i = 0; i < dist->alphabet_size; i++) {
        logcounts[i] = get_vlc2(gb, dist_prefix_table, 7, 1);
        if (logcounts[i] == 13) {
            int rle = jxl_u8(gb);
            same[i] = rle + 5;
            i += rle + 3;
            continue;
        }
        if (logcounts[i] > omit_log) {
            omit_log = logcounts[i];
            omit_pos = i;
        }
    }
    if (omit_pos < 0 || omit_pos + 1 < dist->alphabet_size && logcounts[omit_pos + 1] == 13)
        return AVERROR_INVALIDDATA;

    for (int i = 0; i < dist->alphabet_size; i++) {
        if (same[i]) {
            num_same = same[i] - 1;
            prev = i > 0 ? dist->freq[i - 1] : 0;
        }
        if (num_same) {
            dist->freq[i] = prev;
            num_same--;
        } else {
            if (i == omit_pos || !logcounts[i])
                continue;
            if (logcounts[i] == 1) {
                dist->freq[i] = 1;
            } else {
                int bitcount = FFMIN(FFMAX(0, shift - ((12 - logcounts[i] + 1) >> 1)), logcounts[i] - 1);
                dist->freq[i] = (1 << (logcounts[i] - 1)) + (get_bitsz(gb, bitcount) << (logcounts[i] - 1 - bitcount));
            }
        }
        total_count += dist->freq[i];
    }
    dist->freq[omit_pos] = (1 << 12) - total_count;

    return 0;
}

static void dist_bundle_close(JXLDistributionBundle *bundle)
{
    if (bundle->use_prefix_code && bundle->dists)
        for (int i = 0; i < bundle->num_clusters; i++)
            ff_vlc_free(&bundle->dists[i].vlc);
    av_freep(&bundle->dists);
    av_freep(&bundle->cluster_map);
}


static int read_distribution_bundle(GetBitContext *gb, JXLEntropyDecoder *dec,
                                    JXLDistributionBundle *bundle, int num_dist, int disallow_lz77);

static int read_dist_clustering(GetBitContext *gb, JXLEntropyDecoder *dec, JXLDistributionBundle *bundle)
{
    int ret;

    bundle->cluster_map = av_malloc(bundle->num_dist);
    if (!bundle->cluster_map)
        return AVERROR(ENOMEM);

    if (bundle->num_dist == 1) {
        bundle->cluster_map[0] = 0;
        bundle->num_clusters = 1;
        return 0;
    }

    if (get_bits1(gb)) {
        /* simple clustering */
        uint32_t nbits = get_bits(gb, 2);
        for (int i = 0; i < bundle->num_dist; i++)
            bundle->cluster_map[i] = get_bitsz(gb, nbits);
    } else {
        /* complex clustering */
        int use_mtf = get_bits1(gb);
        JXLDistributionBundle nested = { 0 };
        /* num_dist == 1 prevents this from recursing again */
        ret = read_distribution_bundle(gb, dec, &nested, 1, bundle->num_dist <= 2);
        if (ret < 0) {
            dist_bundle_close(&nested);
            return ret;
        }
        for (int i = 0; i < bundle->num_dist; i++) {
            uint32_t clust;
            ret = decode_hybrid_varlen_uint(gb, dec, &nested, 0, &clust);
            if (ret < 0) {
                dist_bundle_close(&nested);
                return ret;
            }
            bundle->cluster_map[i] = clust;
        }
        dec->state = -1;
        /* it's not going to necessarily be zero after reading */
        dec->num_to_copy = 0;
        dist_bundle_close(&nested);
        if (use_mtf) {
            uint8_t mtf[256];
            for (int i = 0; i < 256; i++)
                mtf[i] = i;
            for (int i = 0; i < bundle->num_dist; i++) {
                int index = bundle->cluster_map[i];
                bundle->cluster_map[i] = mtf[index];
                if (index) {
                    int value = mtf[index];
                    for (int j = index; j > 0; j--)
                        mtf[j] = mtf[j - 1];
                    mtf[0] = value;
                }
            }
        }
    }
    for (int i = 0; i < bundle->num_dist; i++) {
        if (bundle->cluster_map[i] >= bundle->num_clusters)
            bundle->num_clusters = bundle->cluster_map[i] + 1;
    }

    if (bundle->num_clusters > bundle->num_dist)
        return AVERROR_INVALIDDATA;

    return 0;
}

static int gen_alias_map(JXLEntropyDecoder *dec, JXLSymbolDistribution *dist, int log_alphabet_size)
{
    uint32_t bucket_size, table_size;
    uint8_t overfull[256], underfull[256];
    int overfull_pos = 0, underfull_pos = 0;
    dist->log_bucket_size = 12 - log_alphabet_size;
    bucket_size = 1 << dist->log_bucket_size;
    table_size = 1 << log_alphabet_size;

    if (dist->uniq_pos >= 0) {
        for (int i = 0; i < table_size; i++) {
            dist->symbols[i] = dist->uniq_pos;
            dist->offsets[i] = bucket_size * i;
            dist->cutoffs[i] = 0;
        }
        return 0;
    }

    for (int i = 0; i < dist->alphabet_size; i++) {
        dist->cutoffs[i] = dist->freq[i];
        dist->symbols[i] = i;
        if (dist->cutoffs[i] > bucket_size)
            overfull[overfull_pos++] = i;
        else if (dist->cutoffs[i] < bucket_size)
            underfull[underfull_pos++] = i;
    }

    for (int i = dist->alphabet_size; i < table_size; i++) {
        dist->cutoffs[i] = 0;
        underfull[underfull_pos++] = i;
    }

    while (overfull_pos) {
        int o, u, by;
        /* this should be impossible */
        if (!underfull_pos)
            return AVERROR_INVALIDDATA;
        u = underfull[--underfull_pos];
        o = overfull[--overfull_pos];
        by = bucket_size - dist->cutoffs[u];
        dist->cutoffs[o] -= by;
        dist->symbols[u] = o;
        dist->offsets[u] = dist->cutoffs[o];
        if (dist->cutoffs[o] < bucket_size)
            underfull[underfull_pos++] = o;
        else if (dist->cutoffs[o] > bucket_size)
            overfull[overfull_pos++] = o;
    }

    for (int i = 0; i < table_size; i++) {
        if (dist->cutoffs[i] == bucket_size) {
            dist->symbols[i] = i;
            dist->offsets[i] = 0;
            dist->cutoffs[i] = 0;
        } else {
            dist->offsets[i] -= dist->cutoffs[i];
        }
    }

    return 0;
}

static int read_simple_vlc_prefix(GetBitContext *gb, JXLEntropyDecoder *dec, JXLSymbolDistribution *dist)
{
    int nsym, tree_select, bits;

    int8_t lens[4];
    int16_t symbols[4];

    nsym = 1 + get_bits(gb, 2);
    for (int i = 0; i < nsym; i++)
        symbols[i] = get_bitsz(gb, dist->log_alphabet_size);
    if (nsym == 4)
        tree_select = get_bits1(gb);
    switch (nsym) {
    case 1:
        dist->vlc.bits = 0;
        dist->default_symbol = symbols[0];
        return 0;
    case 2:
        bits = 1;
        lens[0] = 1, lens[1] = 1, lens[2] = 0, lens[3] = 0;
        if (symbols[1] < symbols[0])
            FFSWAP(int16_t, symbols[0], symbols[1]);
        break;
    case 3:
        bits = 2;
        lens[0] = 1, lens[1] = 2, lens[2] = 2, lens[3] = 0;
        if (symbols[2] < symbols[1])
            FFSWAP(int16_t, symbols[1], symbols[2]);
        break;
    case 4:
        if (tree_select) {
            bits = 3;
            lens[0] = 1, lens[1] = 2, lens[2] = 3, lens[3] = 3;
            if (symbols[3] < symbols[2])
                FFSWAP(int16_t, symbols[2], symbols[3]);
        } else {
            bits = 2;
            lens[0] = 2, lens[1] = 2, lens[2] = 2, lens[3] = 2;
            while (1) {
                if (symbols[1] < symbols[0])
                    FFSWAP(int16_t, symbols[0], symbols[1]);
                if (symbols[3] < symbols[2])
                    FFSWAP(int16_t, symbols[2], symbols[3]);
                if (symbols[1] <= symbols[2])
                    break;
                FFSWAP(int16_t, symbols[1], symbols[2]);
            }
        }
        break;
    default:
        // Challenge Complete! How did we get here?
        return AVERROR_BUG;
    }

    return ff_vlc_init_from_lengths(&dist->vlc, bits, nsym, lens, 1, symbols,
                                    2, 2, 0, VLC_INIT_LE, dec->logctx);
}

static int read_vlc_prefix(GetBitContext *gb, JXLEntropyDecoder *dec, JXLSymbolDistribution *dist)
{
    int8_t level1_lens[18] = { 0 };
    int8_t level1_lens_s[18] = { 0 };
    int16_t level1_syms[18] = { 0 };
    uint32_t level1_codecounts[19] = { 0 };
    uint8_t *buf = NULL;
    int8_t *level2_lens, *level2_lens_s;
    int16_t *level2_syms;
    uint32_t *level2_codecounts;

    int repeat_count_prev = 0, repeat_count_zero = 0, prev = 8;
    int total_code = 0, len, hskip, num_codes = 0, ret;

    VLC level1_vlc = { 0 };

    if (dist->alphabet_size == 1) {
        dist->vlc.bits = 0;
        dist->default_symbol = 0;
        return 0;
    }

    hskip = get_bits(gb, 2);
    if (hskip == 1)
        return read_simple_vlc_prefix(gb, dec, dist);

    level1_codecounts[0] = hskip;
    for (int i = hskip; i < 18; i++) {
        len = level1_lens[prefix_codelen_map[i]] = get_vlc2(gb, level0_table, 4, 1);
        if (len < 0) {
            ret = AVERROR_INVALIDDATA;
            goto end;
        }
        level1_codecounts[len]++;
        if (len) {
            total_code += (32 >> len);
            num_codes++;
        }
        if (total_code >= 32) {
            level1_codecounts[0] += 18 - i - 1;
            break;
        }
    }

    if (total_code != 32 && num_codes >= 2 || num_codes < 1) {
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

    for (int i = 1; i < 19; i++)
         level1_codecounts[i] += level1_codecounts[i - 1];

    for (int i = 17; i >= 0; i--) {
        int idx = --level1_codecounts[level1_lens[i]];
        level1_lens_s[idx] = level1_lens[i];
        level1_syms[idx] = i;
    }

    ret = ff_vlc_init_from_lengths(&level1_vlc, 5, 18, level1_lens_s, 1, level1_syms, 2, 2,
        0, VLC_INIT_LE, dec->logctx);
    if (ret < 0)
        goto end;

    buf = av_mallocz(MAX_PREFIX_ALPHABET_SIZE * (2 * sizeof(int8_t) + sizeof(int16_t) + sizeof(uint32_t))
                     + sizeof(uint32_t));
    if (!buf) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    level2_lens = (int8_t *)buf;
    level2_lens_s = (int8_t *)(buf + MAX_PREFIX_ALPHABET_SIZE * sizeof(int8_t));
    level2_syms = (int16_t *)(buf + MAX_PREFIX_ALPHABET_SIZE * (2 * sizeof(int8_t)));
    level2_codecounts = (uint32_t *)(buf + MAX_PREFIX_ALPHABET_SIZE * (2 * sizeof(int8_t) + sizeof(int16_t)));

    total_code = 0;
    for (int i = 0; i < dist->alphabet_size; i++) {
        len = get_vlc2(gb, level1_vlc.table, 5, 1);
        if (len < 0) {
            ret = AVERROR_INVALIDDATA;
            goto end;
        }
        if (get_bits_left(gb) < 0) {
            ret = AVERROR_BUFFER_TOO_SMALL;
            goto end;
        }
        if (len == 16) {
            int extra = 3 + get_bits(gb, 2);
            if (repeat_count_prev)
                extra += 4 * (repeat_count_prev - 2) - repeat_count_prev;
            extra = FFMIN(extra, dist->alphabet_size - i);
            for (int j = 0; j < extra; j++)
                level2_lens[i + j] = prev;
            total_code += (32768 >> prev) * extra;
            i += extra - 1;
            repeat_count_prev += extra;
            repeat_count_zero = 0;
            level2_codecounts[prev] += extra;
        } else if (len == 17) {
            int extra = 3 + get_bits(gb, 3);
            if (repeat_count_zero > 0)
                extra += 8 * (repeat_count_zero - 2) - repeat_count_zero;
            extra = FFMIN(extra, dist->alphabet_size - i);
            i += extra - 1;
            repeat_count_prev = 0;
            repeat_count_zero += extra;
            level2_codecounts[0] += extra;
        } else {
            level2_lens[i] = len;
            repeat_count_prev = repeat_count_zero = 0;
            if (len) {
                total_code += (32768 >> len);
                prev = len;
            }
            level2_codecounts[len]++;
        }
        if (total_code >= 32768) {
            level2_codecounts[0] += dist->alphabet_size - i - 1;
            break;
        }
    }

    if (total_code != 32768 && level2_codecounts[0] < dist->alphabet_size - 1) {
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

    for (int i = 1; i < dist->alphabet_size + 1; i++)
        level2_codecounts[i] += level2_codecounts[i - 1];

    for (int i = dist->alphabet_size - 1; i >= 0; i--) {
        int idx = --level2_codecounts[level2_lens[i]];
        level2_lens_s[idx] = level2_lens[i];
        level2_syms[idx] = i;
    }

    ret = ff_vlc_init_from_lengths(&dist->vlc, 15, dist->alphabet_size, level2_lens_s,
                                    1, level2_syms, 2, 2, 0, VLC_INIT_LE, dec->logctx);

end:
    av_freep(&buf);
    ff_vlc_free(&level1_vlc);

    return ret;
}

static int read_distribution_bundle(GetBitContext *gb, JXLEntropyDecoder *dec,
                                    JXLDistributionBundle *bundle, int num_dist, int disallow_lz77)
{
    int ret;

    if (num_dist <= 0)
        return AVERROR(EINVAL);

    bundle->num_dist = num_dist;
    bundle->lz77_enabled = get_bits1(gb);
    if (bundle->lz77_enabled) {
        if (disallow_lz77)
            return AVERROR_INVALIDDATA;
        bundle->lz77_min_symbol = jxl_u32(gb, 224, 512, 4096, 8, 0, 0, 0, 15);
        bundle->lz77_min_length = jxl_u32(gb, 3, 4, 5, 9, 0, 0, 2, 8);
        bundle->num_dist++;
        ret = read_hybrid_uint_conf(gb, &bundle->lz_len_conf, 8);
        if (ret < 0)
            return ret;
    }

    if (bundle->lz77_enabled && !dec->window) {
        dec->window = av_malloc_array(1 << 20, sizeof(uint32_t));
        if (!dec->window)
            return AVERROR(ENOMEM);
    }

    ret = read_dist_clustering(gb, dec, bundle);
    if (ret < 0)
        return ret;
    if (get_bits_left(gb) < 0)
        return AVERROR_BUFFER_TOO_SMALL;

    bundle->dists = av_calloc(bundle->num_clusters, sizeof(JXLSymbolDistribution));
    if (!bundle->dists)
        return AVERROR(ENOMEM);

    bundle->use_prefix_code = get_bits1(gb);
    bundle->log_alphabet_size = bundle->use_prefix_code ? 15 : 5 + get_bits(gb, 2);

    for (int i = 0; i < bundle->num_clusters; i++) {
        ret = read_hybrid_uint_conf(gb, &bundle->dists[i].config, bundle->log_alphabet_size);
        if (ret < 0)
            return ret;
        if (get_bits_left(gb) < 0)
            return AVERROR_BUFFER_TOO_SMALL;
    }

    if (bundle->use_prefix_code) {
        for (int i = 0; i < bundle->num_clusters; i++) {
            JXLSymbolDistribution *dist = &bundle->dists[i];
            if (get_bits1(gb)) {
                int n = get_bits(gb, 4);
                dist->alphabet_size = 1 + (1 << n) + get_bitsz(gb, n);
                if (dist->alphabet_size > MAX_PREFIX_ALPHABET_SIZE)
                    return AVERROR_INVALIDDATA;
            } else {
                dist->alphabet_size = 1;
            }
            dist->log_alphabet_size = clog1p(dist->alphabet_size - 1);
        }
        for (int i = 0; i < bundle->num_clusters; i++) {
            ret = read_vlc_prefix(gb, dec, &bundle->dists[i]);
            if (ret < 0)
                return ret;
            if (get_bits_left(gb) < 0)
                return AVERROR_BUFFER_TOO_SMALL;
        }
    } else {
        for (int i = 0; i < bundle->num_clusters; i++) {
            ret = populate_distribution(gb, &bundle->dists[i], bundle->log_alphabet_size);
            if (ret < 0)
                return ret;
            if (get_bits_left(gb) < 0)
                return AVERROR_BUFFER_TOO_SMALL;
        }
        for (int i = 0; i < bundle->num_clusters; i++) {
            ret = gen_alias_map(dec, &bundle->dists[i], bundle->log_alphabet_size);
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

static void entropy_decoder_close(JXLEntropyDecoder *dec)
{
    if (!dec)
        return;
    av_freep(&dec->window);
    dist_bundle_close(&dec->bundle);
}

static int entropy_decoder_init(void *avctx, GetBitContext *gb, JXLEntropyDecoder *dec, int num_dist)
{
    int ret;

    memset(dec, 0, sizeof(*dec));
    dec->logctx = avctx;
    dec->state = -1;

    ret = read_distribution_bundle(gb, dec, &dec->bundle, num_dist, 0);
    if (ret < 0) {
        entropy_decoder_close(dec);
        return ret;
    }

    return 0;
}

static int64_t entropy_decoder_read_symbol(GetBitContext *gb, JXLEntropyDecoder *dec, uint32_t context)
{
    int ret;
    uint32_t hybrid_uint;

    ret = decode_hybrid_varlen_uint(gb, dec, &dec->bundle, context, &hybrid_uint);
    if (ret < 0)
        return ret;

    return hybrid_uint;
}

static inline uint32_t icc_context(uint64_t i, uint32_t b1, uint32_t b2)
{
    uint32_t p1, p2;
    if (i <= 128)
        return 0;
    if (b1 >= 'a' && b1 <= 'z' || b1 >= 'A' && b1 <= 'Z')
        p1 = 0;
    else if (b1 >= '0' && b1 <= '9' || b1 == '.' || b1 == ',')
        p1 = 1;
    else if (b1 <= 1)
        p1 = b1 + 2;
    else if (b1 > 1 && b1 < 16)
        p1 = 4;
    else if (b1 > 240 && b1 < 255)
        p1 = 5;
    else if (b1 == 255)
        p1 = 6;
    else
        p1 = 7;

    if (b2 >= 'a' && b2 <= 'z' || b2 >= 'A' && b2 <= 'Z')
        p2 = 0;
    else if (b2 >= '0' && b2 <= '9' || b2 == '.' || b2 == ',')
        p2 = 1;
    else if (b2 < 16)
        p2 = 2;
    else if (b2 > 240)
        p2 = 3;
    else
        p2 = 4;

    return 1 + p1 + p2 * 8;
}

static inline uint32_t toc_context(uint32_t x)
{
    return FFMIN(7, clog1p(x));
}

static void populate_fields(AVCodecParserContext *s, AVCodecContext *avctx, const FFJXLMetadata *meta)
{
    s->width = meta->width;
    s->height = meta->height;

    switch (meta->csp) {
    case JPEGXL_CS_RGB:
    case JPEGXL_CS_XYB:
        avctx->colorspace = AVCOL_SPC_RGB;
        break;
    default:
        avctx->colorspace = AVCOL_SPC_UNSPECIFIED;
    }

    if (meta->wp == JPEGXL_WP_D65) {
        switch (meta->primaries) {
        case JPEGXL_PR_SRGB:
            avctx->color_primaries = AVCOL_PRI_BT709;
            break;
        case JPEGXL_PR_P3:
            avctx->color_primaries = AVCOL_PRI_SMPTE432;
            break;
        case JPEGXL_PR_2100:
            avctx->color_primaries = AVCOL_PRI_BT2020;
            break;
        default:
            avctx->color_primaries = AVCOL_PRI_UNSPECIFIED;
        }
    } else if (meta->wp == JPEGXL_WP_DCI && meta->primaries == JPEGXL_PR_P3) {
        avctx->color_primaries = AVCOL_PRI_SMPTE431;
    } else {
        avctx->color_primaries = AVCOL_PRI_UNSPECIFIED;
    }

    if (meta->trc > JPEGXL_TR_GAMMA) {
        FFJXLTransferCharacteristic trc = meta->trc - JPEGXL_TR_GAMMA;
        switch (trc) {
        case JPEGXL_TR_BT709:
            avctx->color_trc = AVCOL_TRC_BT709;
            break;
        case JPEGXL_TR_LINEAR:
            avctx->color_trc = AVCOL_TRC_LINEAR;
            break;
        case JPEGXL_TR_SRGB:
            avctx->color_trc = AVCOL_TRC_IEC61966_2_1;
            break;
        case JPEGXL_TR_PQ:
            avctx->color_trc = AVCOL_TRC_SMPTEST2084;
            break;
        case JPEGXL_TR_DCI:
            avctx->color_trc = AVCOL_TRC_SMPTE428;
            break;
        case JPEGXL_TR_HLG:
            avctx->color_trc = AVCOL_TRC_ARIB_STD_B67;
            break;
        default:
            avctx->color_trc = AVCOL_TRC_UNSPECIFIED;
        }
    } else if (meta->trc > 0) {
        if (meta->trc > 45355 && meta->trc < 45555)
            avctx->color_trc = AVCOL_TRC_GAMMA22;
        else if (meta->trc > 35614 && meta->trc < 35814)
            avctx->color_trc = AVCOL_TRC_GAMMA28;
        else
            avctx->color_trc = AVCOL_TRC_UNSPECIFIED;
    } else {
        avctx->color_trc = AVCOL_TRC_UNSPECIFIED;
    }

    if (meta->csp == JPEGXL_CS_GRAY) {
        if (meta->bit_depth <= 8)
            s->format = meta->have_alpha ? AV_PIX_FMT_YA8 : AV_PIX_FMT_GRAY8;
        else if (meta->bit_depth <= 16)
            s->format = meta->have_alpha ? AV_PIX_FMT_YA16 : AV_PIX_FMT_GRAY16;
        else
            s->format = meta->have_alpha ? AV_PIX_FMT_NONE : AV_PIX_FMT_GRAYF32;
    } else {
        if (meta->bit_depth <= 8)
            s->format = meta->have_alpha ? AV_PIX_FMT_RGBA : AV_PIX_FMT_RGB24;
        else if (meta->bit_depth <= 16)
            s->format = meta->have_alpha ? AV_PIX_FMT_RGBA64 : AV_PIX_FMT_RGB48;
        else
            s->format = meta->have_alpha ? AV_PIX_FMT_RGBAF32 : AV_PIX_FMT_RGBF32;
    }
}

static int skip_icc_profile(void *avctx, JXLParseContext *ctx, GetBitContext *gb)
{
    int64_t ret;
    uint32_t last = 0, last2 = 0;
    JXLEntropyDecoder dec = { 0 };
    uint64_t enc_size = jxl_u64(gb);
    uint64_t output_size = 0;
    int out_size_shift = 0;

    if (!enc_size || enc_size > (1 << 22))
        return AVERROR_INVALIDDATA;

    ret = entropy_decoder_init(avctx, gb, &dec, 41);
    if (ret < 0)
        goto end;

    if (get_bits_left(gb) < 0) {
        ret = AVERROR_BUFFER_TOO_SMALL;
        goto end;
    }

    for (uint64_t read = 0; read < enc_size; read++) {
        ret = entropy_decoder_read_symbol(gb, &dec, icc_context(read, last, last2));
        if (ret < 0)
            goto end;
        if (ret > 255) {
            ret = AVERROR_INVALIDDATA;
            goto end;
        }
        if (get_bits_left(gb) < 0) {
            ret = AVERROR_BUFFER_TOO_SMALL;
            goto end;
        }
        last2 = last;
        last = ret;
        if (out_size_shift < 63) {
            output_size += (ret & UINT64_C(0x7F)) << out_size_shift;
            if (!(ret & 0x80)) {
                out_size_shift = 63;
            } else {
                out_size_shift += 7;
                if (out_size_shift > 56) {
                    ret = AVERROR_INVALIDDATA;
                    goto end;
                }
            }
        } else if (output_size < 132) {
            ret = AVERROR_INVALIDDATA;
            goto end;
        }
    }

    ret = 0;

end:
    entropy_decoder_close(&dec);

    return ret;
}

static int skip_extensions(GetBitContext *gb)
{
    uint64_t extensions = jxl_u64(gb), extensions_len = 0;

    if (get_bits_left(gb) < 0)
        return AVERROR_BUFFER_TOO_SMALL;

    if (!extensions)
        return 0;

    for (int i = 0; i < 64; i++) {
        if (extensions & (UINT64_C(1) << i))
            extensions_len += jxl_u64(gb);
        if (get_bits_left(gb) < 0)
            return AVERROR_BUFFER_TOO_SMALL;
    }

    if (extensions_len > INT_MAX || get_bits_left(gb) < extensions_len)
        return AVERROR_BUFFER_TOO_SMALL;

    skip_bits_long(gb, extensions_len);

    return 0;
}

static int parse_frame_header(void *avctx, JXLParseContext *ctx, GetBitContext *gb)
{
    int all_default, do_yCbCr = 0, num_passes = 1, ret;
    int group_size_shift = 1, lf_level = 0, save_as_ref = 0;
    int have_crop = 0, full_frame = 1, resets_canvas = 1, upsampling = 1;
    JXLFrame *frame = &ctx->codestream.frame;
    const FFJXLMetadata *meta = &ctx->codestream.meta;
    int32_t x0 = 0, y0 = 0;
    uint32_t duration = 0, width = meta->coded_width, height = meta->coded_height;
    uint32_t name_len, num_groups, num_lf_groups, group_dim, lf_group_dim, toc_count;
    uint64_t flags = 0;
    int start_len = get_bits_count(gb);

    memset(frame, 0, sizeof(*frame));
    frame->is_last = 1;

    all_default = get_bits1(gb);
    if (!all_default) {
        frame->type = get_bits(gb, 2);
        frame->encoding = get_bits1(gb);
        flags = jxl_u64(gb);
        if (!meta->xyb_encoded)
            do_yCbCr = get_bits1(gb);
        if (!(flags & JXL_FLAG_USE_LF_FRAME)) {
            if (do_yCbCr)
                skip_bits(gb, 6); // jpeg upsampling
            upsampling = jxl_u32(gb, 1, 2, 4, 8, 0, 0, 0, 0);
            skip_bits_long(gb, 2 * meta->num_extra_channels);
            if (get_bits_left(gb) < 0)
                return AVERROR_BUFFER_TOO_SMALL;
        }
        if (frame->encoding == JPEGXL_ENC_MODULAR)
            group_size_shift = get_bits(gb, 2);
        else if (meta->xyb_encoded)
            skip_bits(gb, 6); // xqm and bqm scales
        if (frame->type != JPEGXL_FRAME_REFERENCE_ONLY) {
            num_passes = jxl_u32(gb, 1, 2, 3, 4, 0, 0, 0, 3);
            if (num_passes != 1) {
                int num_ds = jxl_u32(gb, 0, 1, 2, 3, 0, 0, 0, 1);
                skip_bits(gb, 2 * (num_passes - 1)); // shift
                skip_bits(gb, 2 * num_ds); // downsample
                for (int i = 0; i < num_ds; i++)
                    jxl_u32(gb, 0, 1, 2, 0, 0, 0, 0, 3);
            }
        }
        if (frame->type == JPEGXL_FRAME_LF)
            lf_level = 1 + get_bits(gb, 2);
        else
            have_crop = get_bits1(gb);
        if (have_crop) {
            if (frame->type != JPEGXL_FRAME_REFERENCE_ONLY) {
                uint32_t ux0 = jxl_u32(gb, 0, 256, 2304, 18688, 8, 11, 14, 30);
                uint32_t uy0 = jxl_u32(gb, 0, 256, 2304, 18688, 8, 11, 14, 30);
                x0 = unpack_signed(ux0);
                y0 = unpack_signed(uy0);
            }
            width = jxl_u32(gb, 0, 256, 2304, 18688, 8, 11, 14, 30);
            height = jxl_u32(gb, 0, 256, 2304, 18688, 8, 11, 14, 30);
            full_frame = x0 <= 0 && y0 <= 0 && width + x0 >= meta->coded_width
                                            && height + y0 >= meta->coded_height;
        }
        if (get_bits_left(gb) < 0)
            return AVERROR_BUFFER_TOO_SMALL;
        if (frame->type == JPEGXL_FRAME_REGULAR || frame->type == JPEGXL_FRAME_SKIP_PROGRESSIVE) {
            for (int i = 0; i <= meta->num_extra_channels; i++) {
                int mode = jxl_u32(gb, 0, 1, 2, 3, 0, 0, 0, 2);
                if (meta->num_extra_channels && (mode == JPEGXL_BM_BLEND || mode == JPEGXL_BM_MULADD))
                    jxl_u32(gb, 0, 1, 2, 3, 0, 0, 0, 2);
                if (meta->num_extra_channels && (mode == JPEGXL_BM_BLEND || mode == JPEGXL_BM_MULADD
                                              || mode == JPEGXL_BM_MUL))
                    skip_bits1(gb);
                if (!i)
                    resets_canvas = mode == JPEGXL_BM_REPLACE && full_frame;
                if (!resets_canvas)
                    skip_bits(gb, 2);
                if (get_bits_left(gb) < 0)
                    return AVERROR_BUFFER_TOO_SMALL;
            }
            if (meta->animation_offset)
                duration = jxl_u32(gb, 0, 1, 0, 0, 0, 0, 8, 32);
            if (meta->have_timecodes)
                skip_bits_long(gb, 32);
            frame->is_last = get_bits1(gb);
        } else {
            frame->is_last = 0;
        }
        if (frame->type != JPEGXL_FRAME_LF && !frame->is_last)
            save_as_ref = get_bits(gb, 2);
        if (frame->type == JPEGXL_FRAME_REFERENCE_ONLY ||
                (resets_canvas && !frame->is_last && (!duration || save_as_ref)
                && frame->type != JPEGXL_FRAME_LF))
            skip_bits1(gb); // save before color transform
        name_len = 8 * jxl_u32(gb, 0, 0, 16, 48, 0, 4, 5, 10);
        if (get_bits_left(gb) < name_len)
            return AVERROR_BUFFER_TOO_SMALL;
        skip_bits_long(gb, name_len);
    }

    if (!all_default) {
        int restd = get_bits1(gb), gab = 1;
        if (!restd)
            gab = get_bits1(gb);
        if (gab && !restd && get_bits1(gb))
            // gab custom
            skip_bits_long(gb, 16 * 6);
        if (get_bits_left(gb) < 0)
            return AVERROR_BUFFER_TOO_SMALL;
        if (!restd) {
            int epf = get_bits(gb, 2);
            if (epf) {
                if (frame->encoding == JPEGXL_ENC_VARDCT && get_bits1(gb)) {
                    skip_bits_long(gb, 16 * 8); // custom epf sharpness
                    if (get_bits_left(gb) < 0)
                        return AVERROR_BUFFER_TOO_SMALL;
                }
                if (get_bits1(gb)) {
                    skip_bits_long(gb, 3 * 16 + 32); // custom epf weight
                    if (get_bits_left(gb) < 0)
                        return AVERROR_BUFFER_TOO_SMALL;
                }
                if (get_bits1(gb)) { // custom epf sigma
                    if (frame->encoding == JPEGXL_ENC_VARDCT)
                        skip_bits(gb, 16);
                    skip_bits_long(gb, 16 * 3);
                    if (get_bits_left(gb) < 0)
                        return AVERROR_BUFFER_TOO_SMALL;
                }
                if (frame->encoding == JPEGXL_ENC_MODULAR)
                    skip_bits(gb, 16);
            }
            ret = skip_extensions(gb);
            if (ret < 0)
                return ret;
        }
        ret = skip_extensions(gb);
        if (ret < 0)
            return ret;
    }

    width = div_ceil(div_ceil(width, upsampling), 1 << (3 * lf_level));
    height = div_ceil(div_ceil(height, upsampling), 1 << (3 * lf_level));
    group_dim = 128 << group_size_shift;
    lf_group_dim = group_dim << 3;
    num_groups = div_ceil(width, group_dim) * div_ceil(height, group_dim);
    num_lf_groups = div_ceil(width, lf_group_dim) * div_ceil(height, lf_group_dim);
    if (num_groups == 1 && num_passes == 1)
        toc_count = 1;
    else
        toc_count = 2 + num_lf_groups + num_groups * num_passes;

    // permuted toc
    if (get_bits1(gb)) {
        JXLEntropyDecoder dec;
        int64_t end, lehmer = 0;
        ret = entropy_decoder_init(avctx, gb, &dec, 8);
        if (ret < 0)
            return ret;
        if (get_bits_left(gb) < 0) {
            entropy_decoder_close(&dec);
            return AVERROR_BUFFER_TOO_SMALL;
        }
        end = entropy_decoder_read_symbol(gb, &dec, toc_context(toc_count));
        if (end < 0 || end > toc_count) {
            entropy_decoder_close(&dec);
            return AVERROR_INVALIDDATA;
        }
        for (uint32_t i = 0; i < end; i++) {
            lehmer = entropy_decoder_read_symbol(gb, &dec, toc_context(lehmer));
            if (lehmer < 0 || get_bits_left(gb) < 0) {
                entropy_decoder_close(&dec);
                return AVERROR_BUFFER_TOO_SMALL;
            }
        }
        entropy_decoder_close(&dec);
    }
    align_get_bits(gb);

    for (uint32_t i = 0; i < toc_count; i++) {
        frame->body_length += 8 * jxl_u32(gb, 0, 1024, 17408, 4211712, 10, 14, 22, 30);
        if (get_bits_left(gb) < 0)
            return AVERROR_BUFFER_TOO_SMALL;
    }
    align_get_bits(gb);

    frame->total_length = frame->body_length + get_bits_count(gb) - start_len;

    return 0;
}

static int skip_boxes(JXLParseContext *ctx, const uint8_t *buf, int buf_size)
{
    GetByteContext gb;

    if (ctx->skip > buf_size)
        return AVERROR_BUFFER_TOO_SMALL;

    buf += ctx->skip;
    buf_size -= ctx->skip;
    bytestream2_init(&gb, buf, buf_size);

    while (1) {
        uint64_t size;
        int head_size = 8;

        if (bytestream2_peek_le16(&gb) == FF_JPEGXL_CODESTREAM_SIGNATURE_LE)
            break;
        if (bytestream2_peek_le64(&gb) == FF_JPEGXL_CONTAINER_SIGNATURE_LE)
            break;

        if (bytestream2_get_bytes_left(&gb) < 8)
            return AVERROR_BUFFER_TOO_SMALL;

        size = bytestream2_get_be32(&gb);
        bytestream2_skip(&gb, 4); // tag
        if (size == 1) {
            if (bytestream2_get_bytes_left(&gb) < 8)
                return AVERROR_BUFFER_TOO_SMALL;
            size = bytestream2_get_be64(&gb);
            head_size = 16;
        }
        if (!size)
            return AVERROR_INVALIDDATA;
        /* invalid ISOBMFF size */
        if (size <= head_size || size > INT_MAX - ctx->skip)
            return AVERROR_INVALIDDATA;

        ctx->skip += size;
        bytestream2_skip(&gb, size - head_size);
        if (bytestream2_get_bytes_left(&gb) <= 0)
            return AVERROR_BUFFER_TOO_SMALL;
    }

    return 0;
}

static int try_parse(AVCodecParserContext *s, AVCodecContext *avctx, JXLParseContext *ctx,
                     const uint8_t *buf, int buf_size)
{
    int ret, cs_buflen, header_skip;
    const uint8_t *cs_buffer;
    GetBitContext gb;

    if (ctx->skip > buf_size)
        return AVERROR_BUFFER_TOO_SMALL;

    buf += ctx->skip;
    buf_size -= ctx->skip;

    if (ctx->container || AV_RL64(buf) == FF_JPEGXL_CONTAINER_SIGNATURE_LE) {
        ctx->container = 1;
        ret = ff_jpegxl_collect_codestream_header(buf, buf_size, ctx->cs_buffer,
                                                  sizeof(ctx->cs_buffer) - AV_INPUT_BUFFER_PADDING_SIZE, &ctx->copied);
        if (ret < 0)
            return ret;
        ctx->collected_size = ret;
        if (!ctx->copied) {
            ctx->skip += ret;
            return AVERROR_BUFFER_TOO_SMALL;
        }
        cs_buffer = ctx->cs_buffer;
        cs_buflen = FFMIN(sizeof(ctx->cs_buffer) - AV_INPUT_BUFFER_PADDING_SIZE, ctx->copied);
    } else {
        cs_buffer = buf;
        cs_buflen = buf_size;
    }

    if (!ctx->codestream_length) {
        header_skip = ff_jpegxl_parse_codestream_header(cs_buffer, cs_buflen, &ctx->codestream.meta, 0);
        if (header_skip < 0)
            return header_skip;
        ctx->codestream_length = header_skip;
        populate_fields(s, avctx, &ctx->codestream.meta);
    }

    if (ctx->container)
        return ctx->collected_size;

    ret = init_get_bits8(&gb, cs_buffer, cs_buflen);
    if (ret < 0)
        return ret;

    skip_bits_long(&gb, ctx->codestream_length);

    if (!ctx->skipped_icc && ctx->codestream.meta.have_icc_profile) {
        ret = skip_icc_profile(avctx, ctx, &gb);
        if (ret < 0)
            return ret;
        ctx->skipped_icc = 1;
        align_get_bits(&gb);
        ctx->codestream_length = get_bits_count(&gb);
    }

    if (get_bits_left(&gb) <= 0)
        return AVERROR_BUFFER_TOO_SMALL;

    while (1) {
        ret = parse_frame_header(avctx, ctx, &gb);
        if (ret < 0)
            return ret;
        ctx->codestream_length += ctx->codestream.frame.total_length;
        if (ctx->codestream.frame.is_last)
            return ctx->codestream_length / 8;
        if (get_bits_left(&gb) <= ctx->codestream.frame.body_length)
            return AVERROR_BUFFER_TOO_SMALL;
        skip_bits_long(&gb, ctx->codestream.frame.body_length);
    }
}

static int jpegxl_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                        const uint8_t **poutbuf, int *poutbuf_size,
                        const uint8_t *buf, int buf_size)
{
    JXLParseContext *ctx = s->priv_data;
    int next = END_NOT_FOUND, ret;
    const uint8_t *pbuf = ctx->pc.buffer;
    int pindex = ctx->pc.index;

    *poutbuf_size = 0;
    *poutbuf = NULL;

    if (!ctx->pc.index) {
        if (ctx->pc.overread)
            goto flush;
        pbuf = buf;
        pindex = buf_size;
    }

    if ((!ctx->container || !ctx->codestream_length) && !ctx->next) {
        ret = try_parse(s, avctx, ctx, pbuf, pindex);
        if (ret < 0)
            goto flush;
        ctx->next = ret;
        if (ctx->container)
            ctx->skip += ctx->next;
    }

    if (ctx->container && ctx->next >= 0) {
        ret = skip_boxes(ctx, pbuf, pindex);
        if (ret < 0) {
            if (ret == AVERROR_INVALIDDATA)
                ctx->next = -1;
            goto flush;
        }
        ctx->next = ret + ctx->skip;
    }

    if (ctx->next >= 0)
        next = ctx->next - ctx->pc.index;

flush:
    if (next > buf_size)
        next = END_NOT_FOUND;

    ret = ff_combine_frame(&ctx->pc, next, &buf, &buf_size);
    if (ret < 0)
        return buf_size;

    *poutbuf      = buf;
    *poutbuf_size = buf_size;

    ctx->codestream_length = 0;
    ctx->collected_size = 0;
    ctx->container = 0;
    ctx->copied = 0;
    ctx->skip = 0;
    ctx->skipped_icc = 0;
    ctx->next = 0;
    memset(&ctx->codestream, 0, sizeof(ctx->codestream));

    return next;
}

const AVCodecParser ff_jpegxl_parser = {
    .codec_ids      = { AV_CODEC_ID_JPEGXL },
    .priv_data_size = sizeof(JXLParseContext),
    .parser_parse   = jpegxl_parse,
    .parser_close   = ff_parse_close,
};
