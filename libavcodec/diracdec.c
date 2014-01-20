/*
 * Copyright (C) 2007 Marco Gerards <marco@gnu.org>
 * Copyright (C) 2009 David Conrad
 * Copyright (C) 2011 Jordi Ortiz
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
 * Dirac Decoder
 * @author Marco Gerards <marco@gnu.org>, David Conrad, Jordi Ortiz <nenjordi@gmail.com>
 */

#include "avcodec.h"
#include "dsputil.h"
#include "get_bits.h"
#include "bytestream.h"
#include "internal.h"
#include "golomb.h"
#include "dirac_arith.h"
#include "mpeg12data.h"
#include "dirac_dwt.h"
#include "dirac.h"
#include "diracdsp.h"
#include "videodsp.h" // for ff_emulated_edge_mc_8

/**
 * The spec limits the number of wavelet decompositions to 4 for both
 * level 1 (VC-2) and 128 (long-gop default).
 * 5 decompositions is the maximum before >16-bit buffers are needed.
 * Schroedinger allows this for DD 9,7 and 13,7 wavelets only, limiting
 * the others to 4 decompositions (or 3 for the fidelity filter).
 *
 * We use this instead of MAX_DECOMPOSITIONS to save some memory.
 */
#define MAX_DWT_LEVELS 5

/**
 * The spec limits this to 3 for frame coding, but in practice can be as high as 6
 */
#define MAX_REFERENCE_FRAMES 8
#define MAX_DELAY 5         /* limit for main profile for frame coding (TODO: field coding) */
#define MAX_FRAMES (MAX_REFERENCE_FRAMES + MAX_DELAY + 1)
#define MAX_QUANT 68        /* max quant for VC-2 */
#define MAX_BLOCKSIZE 32    /* maximum xblen/yblen we support */

/**
 * DiracBlock->ref flags, if set then the block does MC from the given ref
 */
#define DIRAC_REF_MASK_REF1   1
#define DIRAC_REF_MASK_REF2   2
#define DIRAC_REF_MASK_GLOBAL 4

/**
 * Value of Picture.reference when Picture is not a reference picture, but
 * is held for delayed output.
 */
#define DELAYED_PIC_REF 4

#define ff_emulated_edge_mc ff_emulated_edge_mc_8 /* Fix: change the calls to this function regarding bit depth */

#define CALC_PADDING(size, depth)                       \
    (((size + (1 << depth) - 1) >> depth) << depth)

#define DIVRNDUP(a, b) (((a) + (b) - 1) / (b))

typedef struct {
    AVFrame *avframe;
    int interpolated[3];    /* 1 if hpel[] is valid */
    uint8_t *hpel[3][4];
    uint8_t *hpel_base[3][4];
} DiracFrame;

typedef struct {
    union {
        int16_t mv[2][2];
        int16_t dc[3];
    } u; /* anonymous unions aren't in C99 :( */
    uint8_t ref;
} DiracBlock;

typedef struct SubBand {
    int level;
    int orientation;
    int stride;
    int width;
    int height;
    int quant;
    IDWTELEM *ibuf;
    struct SubBand *parent;

    /* for low delay */
    unsigned length;
    const uint8_t *coeff_data;
} SubBand;

typedef struct Plane {
    int width;
    int height;
    ptrdiff_t stride;

    int idwt_width;
    int idwt_height;
    int idwt_stride;
    IDWTELEM *idwt_buf;
    IDWTELEM *idwt_buf_base;
    IDWTELEM *idwt_tmp;

    /* block length */
    uint8_t xblen;
    uint8_t yblen;
    /* block separation (block n+1 starts after this many pixels in block n) */
    uint8_t xbsep;
    uint8_t ybsep;
    /* amount of overspill on each edge (half of the overlap between blocks) */
    uint8_t xoffset;
    uint8_t yoffset;

    SubBand band[MAX_DWT_LEVELS][4];
} Plane;

typedef struct DiracContext {
    AVCodecContext *avctx;
    DSPContext dsp;
    DiracDSPContext diracdsp;
    GetBitContext gb;
    dirac_source_params source;
    int seen_sequence_header;
    int frame_number;           /* number of the next frame to display       */
    Plane plane[3];
    int chroma_x_shift;
    int chroma_y_shift;

    int zero_res;               /* zero residue flag                         */
    int is_arith;               /* whether coeffs use arith or golomb coding */
    int low_delay;              /* use the low delay syntax                  */
    int globalmc_flag;          /* use global motion compensation            */
    int num_refs;               /* number of reference pictures              */

    /* wavelet decoding */
    unsigned wavelet_depth;     /* depth of the IDWT                         */
    unsigned wavelet_idx;

    /**
     * schroedinger older than 1.0.8 doesn't store
     * quant delta if only one codebook exists in a band
     */
    unsigned old_delta_quant;
    unsigned codeblock_mode;

    struct {
        unsigned width;
        unsigned height;
    } codeblock[MAX_DWT_LEVELS+1];

    struct {
        unsigned num_x;         /* number of horizontal slices               */
        unsigned num_y;         /* number of vertical slices                 */
        AVRational bytes;       /* average bytes per slice                   */
        uint8_t quant[MAX_DWT_LEVELS][4]; /* [DIRAC_STD] E.1 */
    } lowdelay;

    struct {
        int pan_tilt[2];        /* pan/tilt vector                           */
        int zrs[2][2];          /* zoom/rotate/shear matrix                  */
        int perspective[2];     /* perspective vector                        */
        unsigned zrs_exp;
        unsigned perspective_exp;
    } globalmc[2];

    /* motion compensation */
    uint8_t mv_precision;       /* [DIRAC_STD] REFS_WT_PRECISION             */
    int16_t weight[2];          /* [DIRAC_STD] REF1_WT and REF2_WT           */
    unsigned weight_log2denom;  /* [DIRAC_STD] REFS_WT_PRECISION             */

    int blwidth;                /* number of blocks (horizontally)           */
    int blheight;               /* number of blocks (vertically)             */
    int sbwidth;                /* number of superblocks (horizontally)      */
    int sbheight;               /* number of superblocks (vertically)        */

    uint8_t *sbsplit;
    DiracBlock *blmotion;

    uint8_t *edge_emu_buffer[4];
    uint8_t *edge_emu_buffer_base;

    uint16_t *mctmp;            /* buffer holding the MC data multipled by OBMC weights */
    uint8_t *mcscratch;

    DECLARE_ALIGNED(16, uint8_t, obmc_weight)[3][MAX_BLOCKSIZE*MAX_BLOCKSIZE];

    void (*put_pixels_tab[4])(uint8_t *dst, const uint8_t *src[5], int stride, int h);
    void (*avg_pixels_tab[4])(uint8_t *dst, const uint8_t *src[5], int stride, int h);
    void (*add_obmc)(uint16_t *dst, const uint8_t *src, int stride, const uint8_t *obmc_weight, int yblen);
    dirac_weight_func weight_func;
    dirac_biweight_func biweight_func;

    DiracFrame *current_picture;
    DiracFrame *ref_pics[2];

    DiracFrame *ref_frames[MAX_REFERENCE_FRAMES+1];
    DiracFrame *delay_frames[MAX_DELAY+1];
    DiracFrame all_frames[MAX_FRAMES];
} DiracContext;

/**
 * Dirac Specification ->
 * Parse code values. 9.6.1 Table 9.1
 */
enum dirac_parse_code {
    pc_seq_header         = 0x00,
    pc_eos                = 0x10,
    pc_aux_data           = 0x20,
    pc_padding            = 0x30,
};

enum dirac_subband {
    subband_ll = 0,
    subband_hl = 1,
    subband_lh = 2,
    subband_hh = 3
};

static const uint8_t default_qmat[][4][4] = {
    { { 5,  3,  3,  0}, { 0,  4,  4,  1}, { 0,  5,  5,  2}, { 0,  6,  6,  3} },
    { { 4,  2,  2,  0}, { 0,  4,  4,  2}, { 0,  5,  5,  3}, { 0,  7,  7,  5} },
    { { 5,  3,  3,  0}, { 0,  4,  4,  1}, { 0,  5,  5,  2}, { 0,  6,  6,  3} },
    { { 8,  4,  4,  0}, { 0,  4,  4,  0}, { 0,  4,  4,  0}, { 0,  4,  4,  0} },
    { { 8,  4,  4,  0}, { 0,  4,  4,  0}, { 0,  4,  4,  0}, { 0,  4,  4,  0} },
    { { 0,  4,  4,  8}, { 0,  8,  8, 12}, { 0, 13, 13, 17}, { 0, 17, 17, 21} },
    { { 3,  1,  1,  0}, { 0,  4,  4,  2}, { 0,  6,  6,  5}, { 0,  9,  9,  7} },
};

static const int qscale_tab[MAX_QUANT+1] = {
    4,     5,     6,     7,     8,    10,    11,    13,
    16,    19,    23,    27,    32,    38,    45,    54,
    64,    76,    91,   108,   128,   152,   181,   215,
    256,   304,   362,   431,   512,   609,   724,   861,
    1024,  1218,  1448,  1722,  2048,  2435,  2896,  3444,
    4096,  4871,  5793,  6889,  8192,  9742, 11585, 13777,
    16384, 19484, 23170, 27554, 32768, 38968, 46341, 55109,
    65536, 77936
};

static const int qoffset_intra_tab[MAX_QUANT+1] = {
    1,     2,     3,     4,     4,     5,     6,     7,
    8,    10,    12,    14,    16,    19,    23,    27,
    32,    38,    46,    54,    64,    76,    91,   108,
    128,   152,   181,   216,   256,   305,   362,   431,
    512,   609,   724,   861,  1024,  1218,  1448,  1722,
    2048,  2436,  2897,  3445,  4096,  4871,  5793,  6889,
    8192,  9742, 11585, 13777, 16384, 19484, 23171, 27555,
    32768, 38968
};

static const int qoffset_inter_tab[MAX_QUANT+1] = {
    1,     2,     2,     3,     3,     4,     4,     5,
    6,     7,     9,    10,    12,    14,    17,    20,
    24,    29,    34,    41,    48,    57,    68,    81,
    96,   114,   136,   162,   192,   228,   272,   323,
    384,   457,   543,   646,   768,   913,  1086,  1292,
    1536,  1827,  2172,  2583,  3072,  3653,  4344,  5166,
    6144,  7307,  8689, 10333, 12288, 14613, 17378, 20666,
    24576, 29226
};

/* magic number division by 3 from schroedinger */
static inline int divide3(int x)
{
    return ((x+1)*21845 + 10922) >> 16;
}

static DiracFrame *remove_frame(DiracFrame *framelist[], int picnum)
{
    DiracFrame *remove_pic = NULL;
    int i, remove_idx = -1;

    for (i = 0; framelist[i]; i++)
        if (framelist[i]->avframe->display_picture_number == picnum) {
            remove_pic = framelist[i];
            remove_idx = i;
        }

    if (remove_pic)
        for (i = remove_idx; framelist[i]; i++)
            framelist[i] = framelist[i+1];

    return remove_pic;
}

static int add_frame(DiracFrame *framelist[], int maxframes, DiracFrame *frame)
{
    int i;
    for (i = 0; i < maxframes; i++)
        if (!framelist[i]) {
            framelist[i] = frame;
            return 0;
        }
    return -1;
}

static int alloc_sequence_buffers(DiracContext *s)
{
    int sbwidth  = DIVRNDUP(s->source.width,  4);
    int sbheight = DIVRNDUP(s->source.height, 4);
    int i, w, h, top_padding;

    /* todo: think more about this / use or set Plane here */
    for (i = 0; i < 3; i++) {
        int max_xblen = MAX_BLOCKSIZE >> (i ? s->chroma_x_shift : 0);
        int max_yblen = MAX_BLOCKSIZE >> (i ? s->chroma_y_shift : 0);
        w = s->source.width  >> (i ? s->chroma_x_shift : 0);
        h = s->source.height >> (i ? s->chroma_y_shift : 0);

        /* we allocate the max we support here since num decompositions can
         * change from frame to frame. Stride is aligned to 16 for SIMD, and
         * 1<<MAX_DWT_LEVELS top padding to avoid if(y>0) in arith decoding
         * MAX_BLOCKSIZE padding for MC: blocks can spill up to half of that
         * on each side */
        top_padding = FFMAX(1<<MAX_DWT_LEVELS, max_yblen/2);
        w = FFALIGN(CALC_PADDING(w, MAX_DWT_LEVELS), 8); /* FIXME: Should this be 16 for SSE??? */
        h = top_padding + CALC_PADDING(h, MAX_DWT_LEVELS) + max_yblen/2;

        s->plane[i].idwt_buf_base = av_mallocz((w+max_xblen)*h * sizeof(IDWTELEM));
        s->plane[i].idwt_tmp      = av_malloc((w+16) * sizeof(IDWTELEM));
        s->plane[i].idwt_buf      = s->plane[i].idwt_buf_base + top_padding*w;
        if (!s->plane[i].idwt_buf_base || !s->plane[i].idwt_tmp)
            return AVERROR(ENOMEM);
    }

    w = s->source.width;
    h = s->source.height;

    /* fixme: allocate using real stride here */
    s->sbsplit  = av_malloc(sbwidth * sbheight);
    s->blmotion = av_malloc(sbwidth * sbheight * 16 * sizeof(*s->blmotion));
    s->edge_emu_buffer_base = av_malloc((w+64)*MAX_BLOCKSIZE);

    s->mctmp     = av_malloc((w+64+MAX_BLOCKSIZE) * (h+MAX_BLOCKSIZE) * sizeof(*s->mctmp));
    s->mcscratch = av_malloc((w+64)*MAX_BLOCKSIZE);

    if (!s->sbsplit || !s->blmotion || !s->mctmp || !s->mcscratch)
        return AVERROR(ENOMEM);
    return 0;
}

static void free_sequence_buffers(DiracContext *s)
{
    int i, j, k;

    for (i = 0; i < MAX_FRAMES; i++) {
        if (s->all_frames[i].avframe->data[0]) {
            av_frame_unref(s->all_frames[i].avframe);
            memset(s->all_frames[i].interpolated, 0, sizeof(s->all_frames[i].interpolated));
        }

        for (j = 0; j < 3; j++)
            for (k = 1; k < 4; k++)
                av_freep(&s->all_frames[i].hpel_base[j][k]);
    }

    memset(s->ref_frames, 0, sizeof(s->ref_frames));
    memset(s->delay_frames, 0, sizeof(s->delay_frames));

    for (i = 0; i < 3; i++) {
        av_freep(&s->plane[i].idwt_buf_base);
        av_freep(&s->plane[i].idwt_tmp);
    }

    av_freep(&s->sbsplit);
    av_freep(&s->blmotion);
    av_freep(&s->edge_emu_buffer_base);

    av_freep(&s->mctmp);
    av_freep(&s->mcscratch);
}

static av_cold int dirac_decode_init(AVCodecContext *avctx)
{
    DiracContext *s = avctx->priv_data;
    int i;

    s->avctx = avctx;
    s->frame_number = -1;

    ff_dsputil_init(&s->dsp, avctx);
    ff_diracdsp_init(&s->diracdsp);

    for (i = 0; i < MAX_FRAMES; i++) {
        s->all_frames[i].avframe = av_frame_alloc();
        if (!s->all_frames[i].avframe) {
            while (i > 0)
                av_frame_free(&s->all_frames[--i].avframe);
            return AVERROR(ENOMEM);
        }
    }

    return 0;
}

static void dirac_decode_flush(AVCodecContext *avctx)
{
    DiracContext *s = avctx->priv_data;
    free_sequence_buffers(s);
    s->seen_sequence_header = 0;
    s->frame_number = -1;
}

static av_cold int dirac_decode_end(AVCodecContext *avctx)
{
    DiracContext *s = avctx->priv_data;
    int i;

    dirac_decode_flush(avctx);
    for (i = 0; i < MAX_FRAMES; i++)
        av_frame_free(&s->all_frames[i].avframe);

    return 0;
}

#define SIGN_CTX(x) (CTX_SIGN_ZERO + ((x) > 0) - ((x) < 0))

static inline void coeff_unpack_arith(DiracArith *c, int qfactor, int qoffset,
                                      SubBand *b, IDWTELEM *buf, int x, int y)
{
    int coeff, sign;
    int sign_pred = 0;
    int pred_ctx = CTX_ZPZN_F1;

    /* Check if the parent subband has a 0 in the corresponding position */
    if (b->parent)
        pred_ctx += !!b->parent->ibuf[b->parent->stride * (y>>1) + (x>>1)] << 1;

    if (b->orientation == subband_hl)
        sign_pred = buf[-b->stride];

    /* Determine if the pixel has only zeros in its neighbourhood */
    if (x) {
        pred_ctx += !(buf[-1] | buf[-b->stride] | buf[-1-b->stride]);
        if (b->orientation == subband_lh)
            sign_pred = buf[-1];
    } else {
        pred_ctx += !buf[-b->stride];
    }

    coeff = dirac_get_arith_uint(c, pred_ctx, CTX_COEFF_DATA);
    if (coeff) {
        coeff = (coeff * qfactor + qoffset + 2) >> 2;
        sign  = dirac_get_arith_bit(c, SIGN_CTX(sign_pred));
        coeff = (coeff ^ -sign) + sign;
    }
    *buf = coeff;
}

static inline int coeff_unpack_golomb(GetBitContext *gb, int qfactor, int qoffset)
{
    int sign, coeff;

    coeff = svq3_get_ue_golomb(gb);
    if (coeff) {
        coeff = (coeff * qfactor + qoffset + 2) >> 2;
        sign  = get_bits1(gb);
        coeff = (coeff ^ -sign) + sign;
    }
    return coeff;
}

/**
 * Decode the coeffs in the rectangle defined by left, right, top, bottom
 * [DIRAC_STD] 13.4.3.2 Codeblock unpacking loop. codeblock()
 */
static inline void codeblock(DiracContext *s, SubBand *b,
                             GetBitContext *gb, DiracArith *c,
                             int left, int right, int top, int bottom,
                             int blockcnt_one, int is_arith)
{
    int x, y, zero_block;
    int qoffset, qfactor;
    IDWTELEM *buf;

    /* check for any coded coefficients in this codeblock */
    if (!blockcnt_one) {
        if (is_arith)
            zero_block = dirac_get_arith_bit(c, CTX_ZERO_BLOCK);
        else
            zero_block = get_bits1(gb);

        if (zero_block)
            return;
    }

    if (s->codeblock_mode && !(s->old_delta_quant && blockcnt_one)) {
        int quant = b->quant;
        if (is_arith)
            quant += dirac_get_arith_int(c, CTX_DELTA_Q_F, CTX_DELTA_Q_DATA);
        else
            quant += dirac_get_se_golomb(gb);
        if (quant < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid quant\n");
            return;
        }
        b->quant = quant;
    }

    b->quant = FFMIN(b->quant, MAX_QUANT);

    qfactor = qscale_tab[b->quant];
    /* TODO: context pointer? */
    if (!s->num_refs)
        qoffset = qoffset_intra_tab[b->quant];
    else
        qoffset = qoffset_inter_tab[b->quant];

    buf = b->ibuf + top * b->stride;
    for (y = top; y < bottom; y++) {
        for (x = left; x < right; x++) {
            /* [DIRAC_STD] 13.4.4 Subband coefficients. coeff_unpack() */
            if (is_arith)
                coeff_unpack_arith(c, qfactor, qoffset, b, buf+x, x, y);
            else
                buf[x] = coeff_unpack_golomb(gb, qfactor, qoffset);
        }
        buf += b->stride;
    }
}

/**
 * Dirac Specification ->
 * 13.3 intra_dc_prediction(band)
 */
static inline void intra_dc_prediction(SubBand *b)
{
    IDWTELEM *buf = b->ibuf;
    int x, y;

    for (x = 1; x < b->width; x++)
        buf[x] += buf[x-1];
    buf += b->stride;

    for (y = 1; y < b->height; y++) {
        buf[0] += buf[-b->stride];

        for (x = 1; x < b->width; x++) {
            int pred = buf[x - 1] + buf[x - b->stride] + buf[x - b->stride-1];
            buf[x]  += divide3(pred);
        }
        buf += b->stride;
    }
}

/**
 * Dirac Specification ->
 * 13.4.2 Non-skipped subbands.  subband_coeffs()
 */
static av_always_inline void decode_subband_internal(DiracContext *s, SubBand *b, int is_arith)
{
    int cb_x, cb_y, left, right, top, bottom;
    DiracArith c;
    GetBitContext gb;
    int cb_width  = s->codeblock[b->level + (b->orientation != subband_ll)].width;
    int cb_height = s->codeblock[b->level + (b->orientation != subband_ll)].height;
    int blockcnt_one = (cb_width + cb_height) == 2;

    if (!b->length)
        return;

    init_get_bits8(&gb, b->coeff_data, b->length);

    if (is_arith)
        ff_dirac_init_arith_decoder(&c, &gb, b->length);

    top = 0;
    for (cb_y = 0; cb_y < cb_height; cb_y++) {
        bottom = (b->height * (cb_y+1)) / cb_height;
        left = 0;
        for (cb_x = 0; cb_x < cb_width; cb_x++) {
            right = (b->width * (cb_x+1)) / cb_width;
            codeblock(s, b, &gb, &c, left, right, top, bottom, blockcnt_one, is_arith);
            left = right;
        }
        top = bottom;
    }

    if (b->orientation == subband_ll && s->num_refs == 0)
        intra_dc_prediction(b);
}

static int decode_subband_arith(AVCodecContext *avctx, void *b)
{
    DiracContext *s = avctx->priv_data;
    decode_subband_internal(s, b, 1);
    return 0;
}

static int decode_subband_golomb(AVCodecContext *avctx, void *arg)
{
    DiracContext *s = avctx->priv_data;
    SubBand **b     = arg;
    decode_subband_internal(s, *b, 0);
    return 0;
}

/**
 * Dirac Specification ->
 * [DIRAC_STD] 13.4.1 core_transform_data()
 */
static void decode_component(DiracContext *s, int comp)
{
    AVCodecContext *avctx = s->avctx;
    SubBand *bands[3*MAX_DWT_LEVELS+1];
    enum dirac_subband orientation;
    int level, num_bands = 0;

    /* Unpack all subbands at all levels. */
    for (level = 0; level < s->wavelet_depth; level++) {
        for (orientation = !!level; orientation < 4; orientation++) {
            SubBand *b = &s->plane[comp].band[level][orientation];
            bands[num_bands++] = b;

            align_get_bits(&s->gb);
            /* [DIRAC_STD] 13.4.2 subband() */
            b->length = svq3_get_ue_golomb(&s->gb);
            if (b->length) {
                b->quant = svq3_get_ue_golomb(&s->gb);
                align_get_bits(&s->gb);
                b->coeff_data = s->gb.buffer + get_bits_count(&s->gb)/8;
                b->length = FFMIN(b->length, FFMAX(get_bits_left(&s->gb)/8, 0));
                skip_bits_long(&s->gb, b->length*8);
            }
        }
        /* arithmetic coding has inter-level dependencies, so we can only execute one level at a time */
        if (s->is_arith)
            avctx->execute(avctx, decode_subband_arith, &s->plane[comp].band[level][!!level],
                           NULL, 4-!!level, sizeof(SubBand));
    }
    /* golomb coding has no inter-level dependencies, so we can execute all subbands in parallel */
    if (!s->is_arith)
        avctx->execute(avctx, decode_subband_golomb, bands, NULL, num_bands, sizeof(SubBand*));
}

/* [DIRAC_STD] 13.5.5.2 Luma slice subband data. luma_slice_band(level,orient,sx,sy) --> if b2 == NULL */
/* [DIRAC_STD] 13.5.5.3 Chroma slice subband data. chroma_slice_band(level,orient,sx,sy) --> if b2 != NULL */
static void lowdelay_subband(DiracContext *s, GetBitContext *gb, int quant,
                             int slice_x, int slice_y, int bits_end,
                             SubBand *b1, SubBand *b2)
{
    int left   = b1->width  * slice_x    / s->lowdelay.num_x;
    int right  = b1->width  *(slice_x+1) / s->lowdelay.num_x;
    int top    = b1->height * slice_y    / s->lowdelay.num_y;
    int bottom = b1->height *(slice_y+1) / s->lowdelay.num_y;

    int qfactor = qscale_tab[FFMIN(quant, MAX_QUANT)];
    int qoffset = qoffset_intra_tab[FFMIN(quant, MAX_QUANT)];

    IDWTELEM *buf1 =      b1->ibuf + top * b1->stride;
    IDWTELEM *buf2 = b2 ? b2->ibuf + top * b2->stride : NULL;
    int x, y;
    /* we have to constantly check for overread since the spec explictly
       requires this, with the meaning that all remaining coeffs are set to 0 */
    if (get_bits_count(gb) >= bits_end)
        return;

    for (y = top; y < bottom; y++) {
        for (x = left; x < right; x++) {
            buf1[x] = coeff_unpack_golomb(gb, qfactor, qoffset);
            if (get_bits_count(gb) >= bits_end)
                return;
            if (buf2) {
                buf2[x] = coeff_unpack_golomb(gb, qfactor, qoffset);
                if (get_bits_count(gb) >= bits_end)
                    return;
            }
        }
        buf1 += b1->stride;
        if (buf2)
            buf2 += b2->stride;
    }
}

struct lowdelay_slice {
    GetBitContext gb;
    int slice_x;
    int slice_y;
    int bytes;
};


/**
 * Dirac Specification ->
 * 13.5.2 Slices. slice(sx,sy)
 */
static int decode_lowdelay_slice(AVCodecContext *avctx, void *arg)
{
    DiracContext *s = avctx->priv_data;
    struct lowdelay_slice *slice = arg;
    GetBitContext *gb = &slice->gb;
    enum dirac_subband orientation;
    int level, quant, chroma_bits, chroma_end;

    int quant_base  = get_bits(gb, 7); /*[DIRAC_STD] qindex */
    int length_bits = av_log2(8 * slice->bytes)+1;
    int luma_bits   = get_bits_long(gb, length_bits);
    int luma_end    = get_bits_count(gb) + FFMIN(luma_bits, get_bits_left(gb));

    /* [DIRAC_STD] 13.5.5.2 luma_slice_band */
    for (level = 0; level < s->wavelet_depth; level++)
        for (orientation = !!level; orientation < 4; orientation++) {
            quant = FFMAX(quant_base - s->lowdelay.quant[level][orientation], 0);
            lowdelay_subband(s, gb, quant, slice->slice_x, slice->slice_y, luma_end,
                             &s->plane[0].band[level][orientation], NULL);
        }

    /* consume any unused bits from luma */
    skip_bits_long(gb, get_bits_count(gb) - luma_end);

    chroma_bits = 8*slice->bytes - 7 - length_bits - luma_bits;
    chroma_end  = get_bits_count(gb) + FFMIN(chroma_bits, get_bits_left(gb));
    /* [DIRAC_STD] 13.5.5.3 chroma_slice_band */
    for (level = 0; level < s->wavelet_depth; level++)
        for (orientation = !!level; orientation < 4; orientation++) {
            quant = FFMAX(quant_base - s->lowdelay.quant[level][orientation], 0);
            lowdelay_subband(s, gb, quant, slice->slice_x, slice->slice_y, chroma_end,
                             &s->plane[1].band[level][orientation],
                             &s->plane[2].band[level][orientation]);
        }

    return 0;
}

/**
 * Dirac Specification ->
 * 13.5.1 low_delay_transform_data()
 */
static void decode_lowdelay(DiracContext *s)
{
    AVCodecContext *avctx = s->avctx;
    int slice_x, slice_y, bytes, bufsize;
    const uint8_t *buf;
    struct lowdelay_slice *slices;
    int slice_num = 0;

    slices = av_mallocz(s->lowdelay.num_x * s->lowdelay.num_y * sizeof(struct lowdelay_slice));

    align_get_bits(&s->gb);
    /*[DIRAC_STD] 13.5.2 Slices. slice(sx,sy) */
    buf = s->gb.buffer + get_bits_count(&s->gb)/8;
    bufsize = get_bits_left(&s->gb);

    for (slice_y = 0; bufsize > 0 && slice_y < s->lowdelay.num_y; slice_y++)
        for (slice_x = 0; bufsize > 0 && slice_x < s->lowdelay.num_x; slice_x++) {
            bytes = (slice_num+1) * s->lowdelay.bytes.num / s->lowdelay.bytes.den
                - slice_num    * s->lowdelay.bytes.num / s->lowdelay.bytes.den;

            slices[slice_num].bytes   = bytes;
            slices[slice_num].slice_x = slice_x;
            slices[slice_num].slice_y = slice_y;
            init_get_bits(&slices[slice_num].gb, buf, bufsize);
            slice_num++;

            buf     += bytes;
            bufsize -= bytes*8;
        }

    avctx->execute(avctx, decode_lowdelay_slice, slices, NULL, slice_num,
                   sizeof(struct lowdelay_slice)); /* [DIRAC_STD] 13.5.2 Slices */
    intra_dc_prediction(&s->plane[0].band[0][0]);  /* [DIRAC_STD] 13.3 intra_dc_prediction() */
    intra_dc_prediction(&s->plane[1].band[0][0]);  /* [DIRAC_STD] 13.3 intra_dc_prediction() */
    intra_dc_prediction(&s->plane[2].band[0][0]);  /* [DIRAC_STD] 13.3 intra_dc_prediction() */
    av_free(slices);
}

static void init_planes(DiracContext *s)
{
    int i, w, h, level, orientation;

    for (i = 0; i < 3; i++) {
        Plane *p = &s->plane[i];

        p->width       = s->source.width  >> (i ? s->chroma_x_shift : 0);
        p->height      = s->source.height >> (i ? s->chroma_y_shift : 0);
        p->idwt_width  = w = CALC_PADDING(p->width , s->wavelet_depth);
        p->idwt_height = h = CALC_PADDING(p->height, s->wavelet_depth);
        p->idwt_stride = FFALIGN(p->idwt_width, 8);

        for (level = s->wavelet_depth-1; level >= 0; level--) {
            w = w>>1;
            h = h>>1;
            for (orientation = !!level; orientation < 4; orientation++) {
                SubBand *b = &p->band[level][orientation];

                b->ibuf   = p->idwt_buf;
                b->level  = level;
                b->stride = p->idwt_stride << (s->wavelet_depth - level);
                b->width  = w;
                b->height = h;
                b->orientation = orientation;

                if (orientation & 1)
                    b->ibuf += w;
                if (orientation > 1)
                    b->ibuf += b->stride>>1;

                if (level)
                    b->parent = &p->band[level-1][orientation];
            }
        }

        if (i > 0) {
            p->xblen = s->plane[0].xblen >> s->chroma_x_shift;
            p->yblen = s->plane[0].yblen >> s->chroma_y_shift;
            p->xbsep = s->plane[0].xbsep >> s->chroma_x_shift;
            p->ybsep = s->plane[0].ybsep >> s->chroma_y_shift;
        }

        p->xoffset = (p->xblen - p->xbsep)/2;
        p->yoffset = (p->yblen - p->ybsep)/2;
    }
}

/**
 * Unpack the motion compensation parameters
 * Dirac Specification ->
 * 11.2 Picture prediction data. picture_prediction()
 */
static int dirac_unpack_prediction_parameters(DiracContext *s)
{
    static const uint8_t default_blen[] = { 4, 12, 16, 24 };
    static const uint8_t default_bsep[] = { 4,  8, 12, 16 };

    GetBitContext *gb = &s->gb;
    unsigned idx, ref;

    align_get_bits(gb);
    /* [DIRAC_STD] 11.2.2 Block parameters. block_parameters() */
    /* Luma and Chroma are equal. 11.2.3 */
    idx = svq3_get_ue_golomb(gb); /* [DIRAC_STD] index */

    if (idx > 4) {
        av_log(s->avctx, AV_LOG_ERROR, "Block prediction index too high\n");
        return -1;
    }

    if (idx == 0) {
        s->plane[0].xblen = svq3_get_ue_golomb(gb);
        s->plane[0].yblen = svq3_get_ue_golomb(gb);
        s->plane[0].xbsep = svq3_get_ue_golomb(gb);
        s->plane[0].ybsep = svq3_get_ue_golomb(gb);
    } else {
        /*[DIRAC_STD] preset_block_params(index). Table 11.1 */
        s->plane[0].xblen = default_blen[idx-1];
        s->plane[0].yblen = default_blen[idx-1];
        s->plane[0].xbsep = default_bsep[idx-1];
        s->plane[0].ybsep = default_bsep[idx-1];
    }
    /*[DIRAC_STD] 11.2.4 motion_data_dimensions()
      Calculated in function dirac_unpack_block_motion_data */

    if (!s->plane[0].xbsep || !s->plane[0].ybsep || s->plane[0].xbsep < s->plane[0].xblen/2 || s->plane[0].ybsep < s->plane[0].yblen/2) {
        av_log(s->avctx, AV_LOG_ERROR, "Block separation too small\n");
        return -1;
    }
    if (s->plane[0].xbsep > s->plane[0].xblen || s->plane[0].ybsep > s->plane[0].yblen) {
        av_log(s->avctx, AV_LOG_ERROR, "Block separation greater than size\n");
        return -1;
    }
    if (FFMAX(s->plane[0].xblen, s->plane[0].yblen) > MAX_BLOCKSIZE) {
        av_log(s->avctx, AV_LOG_ERROR, "Unsupported large block size\n");
        return -1;
    }

    /*[DIRAC_STD] 11.2.5 Motion vector precision. motion_vector_precision()
      Read motion vector precision */
    s->mv_precision = svq3_get_ue_golomb(gb);
    if (s->mv_precision > 3) {
        av_log(s->avctx, AV_LOG_ERROR, "MV precision finer than eighth-pel\n");
        return -1;
    }

    /*[DIRAC_STD] 11.2.6 Global motion. global_motion()
      Read the global motion compensation parameters */
    s->globalmc_flag = get_bits1(gb);
    if (s->globalmc_flag) {
        memset(s->globalmc, 0, sizeof(s->globalmc));
        /* [DIRAC_STD] pan_tilt(gparams) */
        for (ref = 0; ref < s->num_refs; ref++) {
            if (get_bits1(gb)) {
                s->globalmc[ref].pan_tilt[0] = dirac_get_se_golomb(gb);
                s->globalmc[ref].pan_tilt[1] = dirac_get_se_golomb(gb);
            }
            /* [DIRAC_STD] zoom_rotate_shear(gparams)
               zoom/rotation/shear parameters */
            if (get_bits1(gb)) {
                s->globalmc[ref].zrs_exp   = svq3_get_ue_golomb(gb);
                s->globalmc[ref].zrs[0][0] = dirac_get_se_golomb(gb);
                s->globalmc[ref].zrs[0][1] = dirac_get_se_golomb(gb);
                s->globalmc[ref].zrs[1][0] = dirac_get_se_golomb(gb);
                s->globalmc[ref].zrs[1][1] = dirac_get_se_golomb(gb);
            } else {
                s->globalmc[ref].zrs[0][0] = 1;
                s->globalmc[ref].zrs[1][1] = 1;
            }
            /* [DIRAC_STD] perspective(gparams) */
            if (get_bits1(gb)) {
                s->globalmc[ref].perspective_exp = svq3_get_ue_golomb(gb);
                s->globalmc[ref].perspective[0]  = dirac_get_se_golomb(gb);
                s->globalmc[ref].perspective[1]  = dirac_get_se_golomb(gb);
            }
        }
    }

    /*[DIRAC_STD] 11.2.7 Picture prediction mode. prediction_mode()
      Picture prediction mode, not currently used. */
    if (svq3_get_ue_golomb(gb)) {
        av_log(s->avctx, AV_LOG_ERROR, "Unknown picture prediction mode\n");
        return -1;
    }

    /* [DIRAC_STD] 11.2.8 Reference picture weight. reference_picture_weights()
       just data read, weight calculation will be done later on. */
    s->weight_log2denom = 1;
    s->weight[0]        = 1;
    s->weight[1]        = 1;

    if (get_bits1(gb)) {
        s->weight_log2denom = svq3_get_ue_golomb(gb);
        s->weight[0] = dirac_get_se_golomb(gb);
        if (s->num_refs == 2)
            s->weight[1] = dirac_get_se_golomb(gb);
    }
    return 0;
}

/**
 * Dirac Specification ->
 * 11.3 Wavelet transform data. wavelet_transform()
 */
static int dirac_unpack_idwt_params(DiracContext *s)
{
    GetBitContext *gb = &s->gb;
    int i, level;
    unsigned tmp;

#define CHECKEDREAD(dst, cond, errmsg) \
    tmp = svq3_get_ue_golomb(gb); \
    if (cond) { \
        av_log(s->avctx, AV_LOG_ERROR, errmsg); \
        return -1; \
    }\
    dst = tmp;

    align_get_bits(gb);

    s->zero_res = s->num_refs ? get_bits1(gb) : 0;
    if (s->zero_res)
        return 0;

    /*[DIRAC_STD] 11.3.1 Transform parameters. transform_parameters() */
    CHECKEDREAD(s->wavelet_idx, tmp > 6, "wavelet_idx is too big\n")

    CHECKEDREAD(s->wavelet_depth, tmp > MAX_DWT_LEVELS || tmp < 1, "invalid number of DWT decompositions\n")

    if (!s->low_delay) {
        /* Codeblock parameters (core syntax only) */
        if (get_bits1(gb)) {
            for (i = 0; i <= s->wavelet_depth; i++) {
                CHECKEDREAD(s->codeblock[i].width , tmp < 1, "codeblock width invalid\n")
                CHECKEDREAD(s->codeblock[i].height, tmp < 1, "codeblock height invalid\n")
            }

            CHECKEDREAD(s->codeblock_mode, tmp > 1, "unknown codeblock mode\n")
        } else
            for (i = 0; i <= s->wavelet_depth; i++)
                s->codeblock[i].width = s->codeblock[i].height = 1;
    } else {
        /* Slice parameters + quantization matrix*/
        /*[DIRAC_STD] 11.3.4 Slice coding Parameters (low delay syntax only). slice_parameters() */
        s->lowdelay.num_x     = svq3_get_ue_golomb(gb);
        s->lowdelay.num_y     = svq3_get_ue_golomb(gb);
        s->lowdelay.bytes.num = svq3_get_ue_golomb(gb);
        s->lowdelay.bytes.den = svq3_get_ue_golomb(gb);

        if (s->lowdelay.bytes.den <= 0) {
            av_log(s->avctx,AV_LOG_ERROR,"Invalid lowdelay.bytes.den\n");
            return AVERROR_INVALIDDATA;
        }

        /* [DIRAC_STD] 11.3.5 Quantisation matrices (low-delay syntax). quant_matrix() */
        if (get_bits1(gb)) {
            av_log(s->avctx,AV_LOG_DEBUG,"Low Delay: Has Custom Quantization Matrix!\n");
            /* custom quantization matrix */
            s->lowdelay.quant[0][0] = svq3_get_ue_golomb(gb);
            for (level = 0; level < s->wavelet_depth; level++) {
                s->lowdelay.quant[level][1] = svq3_get_ue_golomb(gb);
                s->lowdelay.quant[level][2] = svq3_get_ue_golomb(gb);
                s->lowdelay.quant[level][3] = svq3_get_ue_golomb(gb);
            }
        } else {
            if (s->wavelet_depth > 4) {
                av_log(s->avctx,AV_LOG_ERROR,"Mandatory custom low delay matrix missing for depth %d\n", s->wavelet_depth);
                return AVERROR_INVALIDDATA;
            }
            /* default quantization matrix */
            for (level = 0; level < s->wavelet_depth; level++)
                for (i = 0; i < 4; i++) {
                    s->lowdelay.quant[level][i] = default_qmat[s->wavelet_idx][level][i];
                    /* haar with no shift differs for different depths */
                    if (s->wavelet_idx == 3)
                        s->lowdelay.quant[level][i] += 4*(s->wavelet_depth-1 - level);
                }
        }
    }
    return 0;
}

static inline int pred_sbsplit(uint8_t *sbsplit, int stride, int x, int y)
{
    static const uint8_t avgsplit[7] = { 0, 0, 1, 1, 1, 2, 2 };

    if (!(x|y))
        return 0;
    else if (!y)
        return sbsplit[-1];
    else if (!x)
        return sbsplit[-stride];

    return avgsplit[sbsplit[-1] + sbsplit[-stride] + sbsplit[-stride-1]];
}

static inline int pred_block_mode(DiracBlock *block, int stride, int x, int y, int refmask)
{
    int pred;

    if (!(x|y))
        return 0;
    else if (!y)
        return block[-1].ref & refmask;
    else if (!x)
        return block[-stride].ref & refmask;

    /* return the majority */
    pred = (block[-1].ref & refmask) + (block[-stride].ref & refmask) + (block[-stride-1].ref & refmask);
    return (pred >> 1) & refmask;
}

static inline void pred_block_dc(DiracBlock *block, int stride, int x, int y)
{
    int i, n = 0;

    memset(block->u.dc, 0, sizeof(block->u.dc));

    if (x && !(block[-1].ref & 3)) {
        for (i = 0; i < 3; i++)
            block->u.dc[i] += block[-1].u.dc[i];
        n++;
    }

    if (y && !(block[-stride].ref & 3)) {
        for (i = 0; i < 3; i++)
            block->u.dc[i] += block[-stride].u.dc[i];
        n++;
    }

    if (x && y && !(block[-1-stride].ref & 3)) {
        for (i = 0; i < 3; i++)
            block->u.dc[i] += block[-1-stride].u.dc[i];
        n++;
    }

    if (n == 2) {
        for (i = 0; i < 3; i++)
            block->u.dc[i] = (block->u.dc[i]+1)>>1;
    } else if (n == 3) {
        for (i = 0; i < 3; i++)
            block->u.dc[i] = divide3(block->u.dc[i]);
    }
}

static inline void pred_mv(DiracBlock *block, int stride, int x, int y, int ref)
{
    int16_t *pred[3];
    int refmask = ref+1;
    int mask = refmask | DIRAC_REF_MASK_GLOBAL; /*  exclude gmc blocks */
    int n = 0;

    if (x && (block[-1].ref & mask) == refmask)
        pred[n++] = block[-1].u.mv[ref];

    if (y && (block[-stride].ref & mask) == refmask)
        pred[n++] = block[-stride].u.mv[ref];

    if (x && y && (block[-stride-1].ref & mask) == refmask)
        pred[n++] = block[-stride-1].u.mv[ref];

    switch (n) {
    case 0:
        block->u.mv[ref][0] = 0;
        block->u.mv[ref][1] = 0;
        break;
    case 1:
        block->u.mv[ref][0] = pred[0][0];
        block->u.mv[ref][1] = pred[0][1];
        break;
    case 2:
        block->u.mv[ref][0] = (pred[0][0] + pred[1][0] + 1) >> 1;
        block->u.mv[ref][1] = (pred[0][1] + pred[1][1] + 1) >> 1;
        break;
    case 3:
        block->u.mv[ref][0] = mid_pred(pred[0][0], pred[1][0], pred[2][0]);
        block->u.mv[ref][1] = mid_pred(pred[0][1], pred[1][1], pred[2][1]);
        break;
    }
}

static void global_mv(DiracContext *s, DiracBlock *block, int x, int y, int ref)
{
    int ez      = s->globalmc[ref].zrs_exp;
    int ep      = s->globalmc[ref].perspective_exp;
    int (*A)[2] = s->globalmc[ref].zrs;
    int *b      = s->globalmc[ref].pan_tilt;
    int *c      = s->globalmc[ref].perspective;

    int m       = (1<<ep) - (c[0]*x + c[1]*y);
    int mx      = m * ((A[0][0] * x + A[0][1]*y) + (1<<ez) * b[0]);
    int my      = m * ((A[1][0] * x + A[1][1]*y) + (1<<ez) * b[1]);

    block->u.mv[ref][0] = (mx + (1<<(ez+ep))) >> (ez+ep);
    block->u.mv[ref][1] = (my + (1<<(ez+ep))) >> (ez+ep);
}

static void decode_block_params(DiracContext *s, DiracArith arith[8], DiracBlock *block,
                                int stride, int x, int y)
{
    int i;

    block->ref  = pred_block_mode(block, stride, x, y, DIRAC_REF_MASK_REF1);
    block->ref ^= dirac_get_arith_bit(arith, CTX_PMODE_REF1);

    if (s->num_refs == 2) {
        block->ref |= pred_block_mode(block, stride, x, y, DIRAC_REF_MASK_REF2);
        block->ref ^= dirac_get_arith_bit(arith, CTX_PMODE_REF2) << 1;
    }

    if (!block->ref) {
        pred_block_dc(block, stride, x, y);
        for (i = 0; i < 3; i++)
            block->u.dc[i] += dirac_get_arith_int(arith+1+i, CTX_DC_F1, CTX_DC_DATA);
        return;
    }

    if (s->globalmc_flag) {
        block->ref |= pred_block_mode(block, stride, x, y, DIRAC_REF_MASK_GLOBAL);
        block->ref ^= dirac_get_arith_bit(arith, CTX_GLOBAL_BLOCK) << 2;
    }

    for (i = 0; i < s->num_refs; i++)
        if (block->ref & (i+1)) {
            if (block->ref & DIRAC_REF_MASK_GLOBAL) {
                global_mv(s, block, x, y, i);
            } else {
                pred_mv(block, stride, x, y, i);
                block->u.mv[i][0] += dirac_get_arith_int(arith + 4 + 2 * i, CTX_MV_F1, CTX_MV_DATA);
                block->u.mv[i][1] += dirac_get_arith_int(arith + 5 + 2 * i, CTX_MV_F1, CTX_MV_DATA);
            }
        }
}

/**
 * Copies the current block to the other blocks covered by the current superblock split mode
 */
static void propagate_block_data(DiracBlock *block, int stride, int size)
{
    int x, y;
    DiracBlock *dst = block;

    for (x = 1; x < size; x++)
        dst[x] = *block;

    for (y = 1; y < size; y++) {
        dst += stride;
        for (x = 0; x < size; x++)
            dst[x] = *block;
    }
}

/**
 * Dirac Specification ->
 * 12. Block motion data syntax
 */
static int dirac_unpack_block_motion_data(DiracContext *s)
{
    GetBitContext *gb = &s->gb;
    uint8_t *sbsplit = s->sbsplit;
    int i, x, y, q, p;
    DiracArith arith[8];

    align_get_bits(gb);

    /* [DIRAC_STD] 11.2.4 and 12.2.1 Number of blocks and superblocks */
    s->sbwidth  = DIVRNDUP(s->source.width,  4*s->plane[0].xbsep);
    s->sbheight = DIVRNDUP(s->source.height, 4*s->plane[0].ybsep);
    s->blwidth  = 4 * s->sbwidth;
    s->blheight = 4 * s->sbheight;

    /* [DIRAC_STD] 12.3.1 Superblock splitting modes. superblock_split_modes()
       decode superblock split modes */
    ff_dirac_init_arith_decoder(arith, gb, svq3_get_ue_golomb(gb));     /* svq3_get_ue_golomb(gb) is the length */
    for (y = 0; y < s->sbheight; y++) {
        for (x = 0; x < s->sbwidth; x++) {
            unsigned int split  = dirac_get_arith_uint(arith, CTX_SB_F1, CTX_SB_DATA);
            if (split > 2)
                return -1;
            sbsplit[x] = (split + pred_sbsplit(sbsplit+x, s->sbwidth, x, y)) % 3;
        }
        sbsplit += s->sbwidth;
    }

    /* setup arith decoding */
    ff_dirac_init_arith_decoder(arith, gb, svq3_get_ue_golomb(gb));
    for (i = 0; i < s->num_refs; i++) {
        ff_dirac_init_arith_decoder(arith + 4 + 2 * i, gb, svq3_get_ue_golomb(gb));
        ff_dirac_init_arith_decoder(arith + 5 + 2 * i, gb, svq3_get_ue_golomb(gb));
    }
    for (i = 0; i < 3; i++)
        ff_dirac_init_arith_decoder(arith+1+i, gb, svq3_get_ue_golomb(gb));

    for (y = 0; y < s->sbheight; y++)
        for (x = 0; x < s->sbwidth; x++) {
            int blkcnt = 1 << s->sbsplit[y * s->sbwidth + x];
            int step   = 4 >> s->sbsplit[y * s->sbwidth + x];

            for (q = 0; q < blkcnt; q++)
                for (p = 0; p < blkcnt; p++) {
                    int bx = 4 * x + p*step;
                    int by = 4 * y + q*step;
                    DiracBlock *block = &s->blmotion[by*s->blwidth + bx];
                    decode_block_params(s, arith, block, s->blwidth, bx, by);
                    propagate_block_data(block, s->blwidth, step);
                }
        }

    return 0;
}

static int weight(int i, int blen, int offset)
{
#define ROLLOFF(i) offset == 1 ? ((i) ? 5 : 3) :        \
    (1 + (6*(i) + offset - 1) / (2*offset - 1))

    if (i < 2*offset)
        return ROLLOFF(i);
    else if (i > blen-1 - 2*offset)
        return ROLLOFF(blen-1 - i);
    return 8;
}

static void init_obmc_weight_row(Plane *p, uint8_t *obmc_weight, int stride,
                                 int left, int right, int wy)
{
    int x;
    for (x = 0; left && x < p->xblen >> 1; x++)
        obmc_weight[x] = wy*8;
    for (; x < p->xblen >> right; x++)
        obmc_weight[x] = wy*weight(x, p->xblen, p->xoffset);
    for (; x < p->xblen; x++)
        obmc_weight[x] = wy*8;
    for (; x < stride; x++)
        obmc_weight[x] = 0;
}

static void init_obmc_weight(Plane *p, uint8_t *obmc_weight, int stride,
                             int left, int right, int top, int bottom)
{
    int y;
    for (y = 0; top && y < p->yblen >> 1; y++) {
        init_obmc_weight_row(p, obmc_weight, stride, left, right, 8);
        obmc_weight += stride;
    }
    for (; y < p->yblen >> bottom; y++) {
        int wy = weight(y, p->yblen, p->yoffset);
        init_obmc_weight_row(p, obmc_weight, stride, left, right, wy);
        obmc_weight += stride;
    }
    for (; y < p->yblen; y++) {
        init_obmc_weight_row(p, obmc_weight, stride, left, right, 8);
        obmc_weight += stride;
    }
}

static void init_obmc_weights(DiracContext *s, Plane *p, int by)
{
    int top = !by;
    int bottom = by == s->blheight-1;

    /* don't bother re-initing for rows 2 to blheight-2, the weights don't change */
    if (top || bottom || by == 1) {
        init_obmc_weight(p, s->obmc_weight[0], MAX_BLOCKSIZE, 1, 0, top, bottom);
        init_obmc_weight(p, s->obmc_weight[1], MAX_BLOCKSIZE, 0, 0, top, bottom);
        init_obmc_weight(p, s->obmc_weight[2], MAX_BLOCKSIZE, 0, 1, top, bottom);
    }
}

static const uint8_t epel_weights[4][4][4] = {
    {{ 16,  0,  0,  0 },
     { 12,  4,  0,  0 },
     {  8,  8,  0,  0 },
     {  4, 12,  0,  0 }},
    {{ 12,  0,  4,  0 },
     {  9,  3,  3,  1 },
     {  6,  6,  2,  2 },
     {  3,  9,  1,  3 }},
    {{  8,  0,  8,  0 },
     {  6,  2,  6,  2 },
     {  4,  4,  4,  4 },
     {  2,  6,  2,  6 }},
    {{  4,  0, 12,  0 },
     {  3,  1,  9,  3 },
     {  2,  2,  6,  6 },
     {  1,  3,  3,  9 }}
};

/**
 * For block x,y, determine which of the hpel planes to do bilinear
 * interpolation from and set src[] to the location in each hpel plane
 * to MC from.
 *
 * @return the index of the put_dirac_pixels_tab function to use
 *  0 for 1 plane (fpel,hpel), 1 for 2 planes (qpel), 2 for 4 planes (qpel), and 3 for epel
 */
static int mc_subpel(DiracContext *s, DiracBlock *block, const uint8_t *src[5],
                     int x, int y, int ref, int plane)
{
    Plane *p = &s->plane[plane];
    uint8_t **ref_hpel = s->ref_pics[ref]->hpel[plane];
    int motion_x = block->u.mv[ref][0];
    int motion_y = block->u.mv[ref][1];
    int mx, my, i, epel, nplanes = 0;

    if (plane) {
        motion_x >>= s->chroma_x_shift;
        motion_y >>= s->chroma_y_shift;
    }

    mx         = motion_x & ~(-1 << s->mv_precision);
    my         = motion_y & ~(-1 << s->mv_precision);
    motion_x >>= s->mv_precision;
    motion_y >>= s->mv_precision;
    /* normalize subpel coordinates to epel */
    /* TODO: template this function? */
    mx      <<= 3 - s->mv_precision;
    my      <<= 3 - s->mv_precision;

    x += motion_x;
    y += motion_y;
    epel = (mx|my)&1;

    /* hpel position */
    if (!((mx|my)&3)) {
        nplanes = 1;
        src[0] = ref_hpel[(my>>1)+(mx>>2)] + y*p->stride + x;
    } else {
        /* qpel or epel */
        nplanes = 4;
        for (i = 0; i < 4; i++)
            src[i] = ref_hpel[i] + y*p->stride + x;

        /* if we're interpolating in the right/bottom halves, adjust the planes as needed
           we increment x/y because the edge changes for half of the pixels */
        if (mx > 4) {
            src[0] += 1;
            src[2] += 1;
            x++;
        }
        if (my > 4) {
            src[0] += p->stride;
            src[1] += p->stride;
            y++;
        }

        /* hpel planes are:
           [0]: F  [1]: H
           [2]: V  [3]: C */
        if (!epel) {
            /* check if we really only need 2 planes since either mx or my is
               a hpel position. (epel weights of 0 handle this there) */
            if (!(mx&3)) {
                /* mx == 0: average [0] and [2]
                   mx == 4: average [1] and [3] */
                src[!mx] = src[2 + !!mx];
                nplanes = 2;
            } else if (!(my&3)) {
                src[0] = src[(my>>1)  ];
                src[1] = src[(my>>1)+1];
                nplanes = 2;
            }
        } else {
            /* adjust the ordering if needed so the weights work */
            if (mx > 4) {
                FFSWAP(const uint8_t *, src[0], src[1]);
                FFSWAP(const uint8_t *, src[2], src[3]);
            }
            if (my > 4) {
                FFSWAP(const uint8_t *, src[0], src[2]);
                FFSWAP(const uint8_t *, src[1], src[3]);
            }
            src[4] = epel_weights[my&3][mx&3];
        }
    }

    /* fixme: v/h _edge_pos */
    if (x + p->xblen > p->width +EDGE_WIDTH/2 ||
        y + p->yblen > p->height+EDGE_WIDTH/2 ||
        x < 0 || y < 0) {
        for (i = 0; i < nplanes; i++) {
            ff_emulated_edge_mc(s->edge_emu_buffer[i], src[i],
                                p->stride, p->stride,
                                p->xblen, p->yblen, x, y,
                                p->width+EDGE_WIDTH/2, p->height+EDGE_WIDTH/2);
            src[i] = s->edge_emu_buffer[i];
        }
    }
    return (nplanes>>1) + epel;
}

static void add_dc(uint16_t *dst, int dc, int stride,
                   uint8_t *obmc_weight, int xblen, int yblen)
{
    int x, y;
    dc += 128;

    for (y = 0; y < yblen; y++) {
        for (x = 0; x < xblen; x += 2) {
            dst[x  ] += dc * obmc_weight[x  ];
            dst[x+1] += dc * obmc_weight[x+1];
        }
        dst          += stride;
        obmc_weight  += MAX_BLOCKSIZE;
    }
}

static void block_mc(DiracContext *s, DiracBlock *block,
                     uint16_t *mctmp, uint8_t *obmc_weight,
                     int plane, int dstx, int dsty)
{
    Plane *p = &s->plane[plane];
    const uint8_t *src[5];
    int idx;

    switch (block->ref&3) {
    case 0: /* DC */
        add_dc(mctmp, block->u.dc[plane], p->stride, obmc_weight, p->xblen, p->yblen);
        return;
    case 1:
    case 2:
        idx = mc_subpel(s, block, src, dstx, dsty, (block->ref&3)-1, plane);
        s->put_pixels_tab[idx](s->mcscratch, src, p->stride, p->yblen);
        if (s->weight_func)
            s->weight_func(s->mcscratch, p->stride, s->weight_log2denom,
                           s->weight[0] + s->weight[1], p->yblen);
        break;
    case 3:
        idx = mc_subpel(s, block, src, dstx, dsty, 0, plane);
        s->put_pixels_tab[idx](s->mcscratch, src, p->stride, p->yblen);
        idx = mc_subpel(s, block, src, dstx, dsty, 1, plane);
        if (s->biweight_func) {
            /* fixme: +32 is a quick hack */
            s->put_pixels_tab[idx](s->mcscratch + 32, src, p->stride, p->yblen);
            s->biweight_func(s->mcscratch, s->mcscratch+32, p->stride, s->weight_log2denom,
                             s->weight[0], s->weight[1], p->yblen);
        } else
            s->avg_pixels_tab[idx](s->mcscratch, src, p->stride, p->yblen);
        break;
    }
    s->add_obmc(mctmp, s->mcscratch, p->stride, obmc_weight, p->yblen);
}

static void mc_row(DiracContext *s, DiracBlock *block, uint16_t *mctmp, int plane, int dsty)
{
    Plane *p = &s->plane[plane];
    int x, dstx = p->xbsep - p->xoffset;

    block_mc(s, block, mctmp, s->obmc_weight[0], plane, -p->xoffset, dsty);
    mctmp += p->xbsep;

    for (x = 1; x < s->blwidth-1; x++) {
        block_mc(s, block+x, mctmp, s->obmc_weight[1], plane, dstx, dsty);
        dstx  += p->xbsep;
        mctmp += p->xbsep;
    }
    block_mc(s, block+x, mctmp, s->obmc_weight[2], plane, dstx, dsty);
}

static void select_dsp_funcs(DiracContext *s, int width, int height, int xblen, int yblen)
{
    int idx = 0;
    if (xblen > 8)
        idx = 1;
    if (xblen > 16)
        idx = 2;

    memcpy(s->put_pixels_tab, s->diracdsp.put_dirac_pixels_tab[idx], sizeof(s->put_pixels_tab));
    memcpy(s->avg_pixels_tab, s->diracdsp.avg_dirac_pixels_tab[idx], sizeof(s->avg_pixels_tab));
    s->add_obmc = s->diracdsp.add_dirac_obmc[idx];
    if (s->weight_log2denom > 1 || s->weight[0] != 1 || s->weight[1] != 1) {
        s->weight_func   = s->diracdsp.weight_dirac_pixels_tab[idx];
        s->biweight_func = s->diracdsp.biweight_dirac_pixels_tab[idx];
    } else {
        s->weight_func   = NULL;
        s->biweight_func = NULL;
    }
}

static void interpolate_refplane(DiracContext *s, DiracFrame *ref, int plane, int width, int height)
{
    /* chroma allocates an edge of 8 when subsampled
       which for 4:2:2 means an h edge of 16 and v edge of 8
       just use 8 for everything for the moment */
    int i, edge = EDGE_WIDTH/2;

    ref->hpel[plane][0] = ref->avframe->data[plane];
    s->dsp.draw_edges(ref->hpel[plane][0], ref->avframe->linesize[plane], width, height, edge, edge, EDGE_TOP | EDGE_BOTTOM); /* EDGE_TOP | EDGE_BOTTOM values just copied to make it build, this needs to be ensured */

    /* no need for hpel if we only have fpel vectors */
    if (!s->mv_precision)
        return;

    for (i = 1; i < 4; i++) {
        if (!ref->hpel_base[plane][i])
            ref->hpel_base[plane][i] = av_malloc((height+2*edge) * ref->avframe->linesize[plane] + 32);
        /* we need to be 16-byte aligned even for chroma */
        ref->hpel[plane][i] = ref->hpel_base[plane][i] + edge*ref->avframe->linesize[plane] + 16;
    }

    if (!ref->interpolated[plane]) {
        s->diracdsp.dirac_hpel_filter(ref->hpel[plane][1], ref->hpel[plane][2],
                                      ref->hpel[plane][3], ref->hpel[plane][0],
                                      ref->avframe->linesize[plane], width, height);
        s->dsp.draw_edges(ref->hpel[plane][1], ref->avframe->linesize[plane], width, height, edge, edge, EDGE_TOP | EDGE_BOTTOM);
        s->dsp.draw_edges(ref->hpel[plane][2], ref->avframe->linesize[plane], width, height, edge, edge, EDGE_TOP | EDGE_BOTTOM);
        s->dsp.draw_edges(ref->hpel[plane][3], ref->avframe->linesize[plane], width, height, edge, edge, EDGE_TOP | EDGE_BOTTOM);
    }
    ref->interpolated[plane] = 1;
}

/**
 * Dirac Specification ->
 * 13.0 Transform data syntax. transform_data()
 */
static int dirac_decode_frame_internal(DiracContext *s)
{
    DWTContext d;
    int y, i, comp, dsty;

    if (s->low_delay) {
        /* [DIRAC_STD] 13.5.1 low_delay_transform_data() */
        for (comp = 0; comp < 3; comp++) {
            Plane *p = &s->plane[comp];
            memset(p->idwt_buf, 0, p->idwt_stride * p->idwt_height * sizeof(IDWTELEM));
        }
        if (!s->zero_res)
            decode_lowdelay(s);
    }

    for (comp = 0; comp < 3; comp++) {
        Plane *p       = &s->plane[comp];
        uint8_t *frame = s->current_picture->avframe->data[comp];

        /* FIXME: small resolutions */
        for (i = 0; i < 4; i++)
            s->edge_emu_buffer[i] = s->edge_emu_buffer_base + i*FFALIGN(p->width, 16);

        if (!s->zero_res && !s->low_delay)
        {
            memset(p->idwt_buf, 0, p->idwt_stride * p->idwt_height * sizeof(IDWTELEM));
            decode_component(s, comp); /* [DIRAC_STD] 13.4.1 core_transform_data() */
        }
        if (ff_spatial_idwt_init2(&d, p->idwt_buf, p->idwt_width, p->idwt_height, p->idwt_stride,
                                  s->wavelet_idx+2, s->wavelet_depth, p->idwt_tmp))
            return -1;

        if (!s->num_refs) { /* intra */
            for (y = 0; y < p->height; y += 16) {
                ff_spatial_idwt_slice2(&d, y+16); /* decode */
                s->diracdsp.put_signed_rect_clamped(frame + y*p->stride, p->stride,
                                                    p->idwt_buf + y*p->idwt_stride, p->idwt_stride, p->width, 16);
            }
        } else { /* inter */
            int rowheight = p->ybsep*p->stride;

            select_dsp_funcs(s, p->width, p->height, p->xblen, p->yblen);

            for (i = 0; i < s->num_refs; i++)
                interpolate_refplane(s, s->ref_pics[i], comp, p->width, p->height);

            memset(s->mctmp, 0, 4*p->yoffset*p->stride);

            dsty = -p->yoffset;
            for (y = 0; y < s->blheight; y++) {
                int h     = 0,
                    start = FFMAX(dsty, 0);
                uint16_t *mctmp    = s->mctmp + y*rowheight;
                DiracBlock *blocks = s->blmotion + y*s->blwidth;

                init_obmc_weights(s, p, y);

                if (y == s->blheight-1 || start+p->ybsep > p->height)
                    h = p->height - start;
                else
                    h = p->ybsep - (start - dsty);
                if (h < 0)
                    break;

                memset(mctmp+2*p->yoffset*p->stride, 0, 2*rowheight);
                mc_row(s, blocks, mctmp, comp, dsty);

                mctmp += (start - dsty)*p->stride + p->xoffset;
                ff_spatial_idwt_slice2(&d, start + h); /* decode */
                s->diracdsp.add_rect_clamped(frame + start*p->stride, mctmp, p->stride,
                                             p->idwt_buf + start*p->idwt_stride, p->idwt_stride, p->width, h);

                dsty += p->ybsep;
            }
        }
    }


    return 0;
}

static int get_buffer_with_edge(AVCodecContext *avctx, AVFrame *f, int flags)
{
    int ret, i;
    int chroma_x_shift, chroma_y_shift;
    avcodec_get_chroma_sub_sample(avctx->pix_fmt, &chroma_x_shift, &chroma_y_shift);

    f->width  = avctx->width  + 2 * EDGE_WIDTH;
    f->height = avctx->height + 2 * EDGE_WIDTH + 2;
    ret = ff_get_buffer(avctx, f, flags);
    if (ret < 0)
        return ret;

    for (i = 0; f->data[i]; i++) {
        int offset = (EDGE_WIDTH >> (i && i<3 ? chroma_y_shift : 0)) *
                     f->linesize[i] + 32;
        f->data[i] += offset;
    }
    f->width  = avctx->width;
    f->height = avctx->height;

    return 0;
}

/**
 * Dirac Specification ->
 * 11.1.1 Picture Header. picture_header()
 */
static int dirac_decode_picture_header(DiracContext *s)
{
    int retire, picnum;
    int i, j, refnum, refdist;
    GetBitContext *gb = &s->gb;

    /* [DIRAC_STD] 11.1.1 Picture Header. picture_header() PICTURE_NUM */
    picnum = s->current_picture->avframe->display_picture_number = get_bits_long(gb, 32);


    av_log(s->avctx,AV_LOG_DEBUG,"PICTURE_NUM: %d\n",picnum);

    /* if this is the first keyframe after a sequence header, start our
       reordering from here */
    if (s->frame_number < 0)
        s->frame_number = picnum;

    s->ref_pics[0] = s->ref_pics[1] = NULL;
    for (i = 0; i < s->num_refs; i++) {
        refnum = picnum + dirac_get_se_golomb(gb);
        refdist = INT_MAX;

        /* find the closest reference to the one we want */
        /* Jordi: this is needed if the referenced picture hasn't yet arrived */
        for (j = 0; j < MAX_REFERENCE_FRAMES && refdist; j++)
            if (s->ref_frames[j]
                && FFABS(s->ref_frames[j]->avframe->display_picture_number - refnum) < refdist) {
                s->ref_pics[i] = s->ref_frames[j];
                refdist = FFABS(s->ref_frames[j]->avframe->display_picture_number - refnum);
            }

        if (!s->ref_pics[i] || refdist)
            av_log(s->avctx, AV_LOG_DEBUG, "Reference not found\n");

        /* if there were no references at all, allocate one */
        if (!s->ref_pics[i])
            for (j = 0; j < MAX_FRAMES; j++)
                if (!s->all_frames[j].avframe->data[0]) {
                    s->ref_pics[i] = &s->all_frames[j];
                    get_buffer_with_edge(s->avctx, s->ref_pics[i]->avframe, AV_GET_BUFFER_FLAG_REF);
                    break;
                }
    }

    /* retire the reference frames that are not used anymore */
    if (s->current_picture->avframe->reference) {
        retire = picnum + dirac_get_se_golomb(gb);
        if (retire != picnum) {
            DiracFrame *retire_pic = remove_frame(s->ref_frames, retire);

            if (retire_pic)
                retire_pic->avframe->reference &= DELAYED_PIC_REF;
            else
                av_log(s->avctx, AV_LOG_DEBUG, "Frame to retire not found\n");
        }

        /* if reference array is full, remove the oldest as per the spec */
        while (add_frame(s->ref_frames, MAX_REFERENCE_FRAMES, s->current_picture)) {
            av_log(s->avctx, AV_LOG_ERROR, "Reference frame overflow\n");
            remove_frame(s->ref_frames, s->ref_frames[0]->avframe->display_picture_number)->avframe->reference &= DELAYED_PIC_REF;
        }
    }

    if (s->num_refs) {
        if (dirac_unpack_prediction_parameters(s))  /* [DIRAC_STD] 11.2 Picture Prediction Data. picture_prediction() */
            return -1;
        if (dirac_unpack_block_motion_data(s))      /* [DIRAC_STD] 12. Block motion data syntax                       */
            return -1;
    }
    if (dirac_unpack_idwt_params(s))                /* [DIRAC_STD] 11.3 Wavelet transform data                        */
        return -1;

    init_planes(s);
    return 0;
}

static int get_delayed_pic(DiracContext *s, AVFrame *picture, int *got_frame)
{
    DiracFrame *out = s->delay_frames[0];
    int i, out_idx  = 0;
    int ret;

    /* find frame with lowest picture number */
    for (i = 1; s->delay_frames[i]; i++)
        if (s->delay_frames[i]->avframe->display_picture_number < out->avframe->display_picture_number) {
            out     = s->delay_frames[i];
            out_idx = i;
        }

    for (i = out_idx; s->delay_frames[i]; i++)
        s->delay_frames[i] = s->delay_frames[i+1];

    if (out) {
        out->avframe->reference ^= DELAYED_PIC_REF;
        *got_frame = 1;
        if((ret = av_frame_ref(picture, out->avframe)) < 0)
            return ret;
    }

    return 0;
}

/**
 * Dirac Specification ->
 * 9.6 Parse Info Header Syntax. parse_info()
 * 4 byte start code + byte parse code + 4 byte size + 4 byte previous size
 */
#define DATA_UNIT_HEADER_SIZE 13

/* [DIRAC_STD] dirac_decode_data_unit makes reference to the while defined in 9.3
   inside the function parse_sequence() */
static int dirac_decode_data_unit(AVCodecContext *avctx, const uint8_t *buf, int size)
{
    DiracContext *s   = avctx->priv_data;
    DiracFrame *pic   = NULL;
    int ret, i, parse_code = buf[4];
    unsigned tmp;

    if (size < DATA_UNIT_HEADER_SIZE)
        return -1;

    init_get_bits(&s->gb, &buf[13], 8*(size - DATA_UNIT_HEADER_SIZE));

    if (parse_code == pc_seq_header) {
        if (s->seen_sequence_header)
            return 0;

        /* [DIRAC_STD] 10. Sequence header */
        if (avpriv_dirac_parse_sequence_header(avctx, &s->gb, &s->source))
            return -1;

        avcodec_get_chroma_sub_sample(avctx->pix_fmt, &s->chroma_x_shift, &s->chroma_y_shift);

        if (alloc_sequence_buffers(s))
            return -1;

        s->seen_sequence_header = 1;
    } else if (parse_code == pc_eos) { /* [DIRAC_STD] End of Sequence */
        free_sequence_buffers(s);
        s->seen_sequence_header = 0;
    } else if (parse_code == pc_aux_data) {
        if (buf[13] == 1) {     /* encoder implementation/version */
            int ver[3];
            /* versions older than 1.0.8 don't store quant delta for
               subbands with only one codeblock */
            if (sscanf(buf+14, "Schroedinger %d.%d.%d", ver, ver+1, ver+2) == 3)
                if (ver[0] == 1 && ver[1] == 0 && ver[2] <= 7)
                    s->old_delta_quant = 1;
        }
    } else if (parse_code & 0x8) {  /* picture data unit */
        if (!s->seen_sequence_header) {
            av_log(avctx, AV_LOG_DEBUG, "Dropping frame without sequence header\n");
            return -1;
        }

        /* find an unused frame */
        for (i = 0; i < MAX_FRAMES; i++)
            if (s->all_frames[i].avframe->data[0] == NULL)
                pic = &s->all_frames[i];
        if (!pic) {
            av_log(avctx, AV_LOG_ERROR, "framelist full\n");
            return -1;
        }

        av_frame_unref(pic->avframe);

        /* [DIRAC_STD] Defined in 9.6.1 ... */
        tmp            =  parse_code & 0x03;                   /* [DIRAC_STD] num_refs()      */
        if (tmp > 2) {
            av_log(avctx, AV_LOG_ERROR, "num_refs of 3\n");
            return -1;
        }
        s->num_refs    = tmp;
        s->is_arith    = (parse_code & 0x48) == 0x08;          /* [DIRAC_STD] using_ac()      */
        s->low_delay   = (parse_code & 0x88) == 0x88;          /* [DIRAC_STD] is_low_delay()  */
        pic->avframe->reference = (parse_code & 0x0C) == 0x0C;  /* [DIRAC_STD]  is_reference() */
        pic->avframe->key_frame = s->num_refs == 0;             /* [DIRAC_STD] is_intra()      */
        pic->avframe->pict_type = s->num_refs + 1;              /* Definition of AVPictureType in avutil.h */

        if ((ret = get_buffer_with_edge(avctx, pic->avframe, (parse_code & 0x0C) == 0x0C ? AV_GET_BUFFER_FLAG_REF : 0)) < 0)
            return ret;
        s->current_picture = pic;
        s->plane[0].stride = pic->avframe->linesize[0];
        s->plane[1].stride = pic->avframe->linesize[1];
        s->plane[2].stride = pic->avframe->linesize[2];

        /* [DIRAC_STD] 11.1 Picture parse. picture_parse() */
        if (dirac_decode_picture_header(s))
            return -1;

        /* [DIRAC_STD] 13.0 Transform data syntax. transform_data() */
        if (dirac_decode_frame_internal(s))
            return -1;
    }
    return 0;
}

static int dirac_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *pkt)
{
    DiracContext *s     = avctx->priv_data;
    AVFrame *picture    = data;
    uint8_t *buf        = pkt->data;
    int buf_size        = pkt->size;
    int i, data_unit_size, buf_idx = 0;
    int ret;

    /* release unused frames */
    for (i = 0; i < MAX_FRAMES; i++)
        if (s->all_frames[i].avframe->data[0] && !s->all_frames[i].avframe->reference) {
            av_frame_unref(s->all_frames[i].avframe);
            memset(s->all_frames[i].interpolated, 0, sizeof(s->all_frames[i].interpolated));
        }

    s->current_picture = NULL;
    *got_frame = 0;

    /* end of stream, so flush delayed pics */
    if (buf_size == 0)
        return get_delayed_pic(s, (AVFrame *)data, got_frame);

    for (;;) {
        /*[DIRAC_STD] Here starts the code from parse_info() defined in 9.6
          [DIRAC_STD] PARSE_INFO_PREFIX = "BBCD" as defined in ISO/IEC 646
          BBCD start code search */
        for (; buf_idx + DATA_UNIT_HEADER_SIZE < buf_size; buf_idx++) {
            if (buf[buf_idx  ] == 'B' && buf[buf_idx+1] == 'B' &&
                buf[buf_idx+2] == 'C' && buf[buf_idx+3] == 'D')
                break;
        }
        /* BBCD found or end of data */
        if (buf_idx + DATA_UNIT_HEADER_SIZE >= buf_size)
            break;

        data_unit_size = AV_RB32(buf+buf_idx+5);
        if (buf_idx + data_unit_size > buf_size || !data_unit_size) {
            if(buf_idx + data_unit_size > buf_size)
            av_log(s->avctx, AV_LOG_ERROR,
                   "Data unit with size %d is larger than input buffer, discarding\n",
                   data_unit_size);
            buf_idx += 4;
            continue;
        }
        /* [DIRAC_STD] dirac_decode_data_unit makes reference to the while defined in 9.3 inside the function parse_sequence() */
        if (dirac_decode_data_unit(avctx, buf+buf_idx, data_unit_size))
        {
            av_log(s->avctx, AV_LOG_ERROR,"Error in dirac_decode_data_unit\n");
            return -1;
        }
        buf_idx += data_unit_size;
    }

    if (!s->current_picture)
        return buf_size;

    if (s->current_picture->avframe->display_picture_number > s->frame_number) {
        DiracFrame *delayed_frame = remove_frame(s->delay_frames, s->frame_number);

        s->current_picture->avframe->reference |= DELAYED_PIC_REF;

        if (add_frame(s->delay_frames, MAX_DELAY, s->current_picture)) {
            int min_num = s->delay_frames[0]->avframe->display_picture_number;
            /* Too many delayed frames, so we display the frame with the lowest pts */
            av_log(avctx, AV_LOG_ERROR, "Delay frame overflow\n");
            delayed_frame = s->delay_frames[0];

            for (i = 1; s->delay_frames[i]; i++)
                if (s->delay_frames[i]->avframe->display_picture_number < min_num)
                    min_num = s->delay_frames[i]->avframe->display_picture_number;

            delayed_frame = remove_frame(s->delay_frames, min_num);
            add_frame(s->delay_frames, MAX_DELAY, s->current_picture);
        }

        if (delayed_frame) {
            delayed_frame->avframe->reference ^= DELAYED_PIC_REF;
            if((ret=av_frame_ref(data, delayed_frame->avframe)) < 0)
                return ret;
            *got_frame = 1;
        }
    } else if (s->current_picture->avframe->display_picture_number == s->frame_number) {
        /* The right frame at the right time :-) */
        if((ret=av_frame_ref(data, s->current_picture->avframe)) < 0)
            return ret;
        *got_frame = 1;
    }

    if (*got_frame)
        s->frame_number = picture->display_picture_number + 1;

    return buf_idx;
}

AVCodec ff_dirac_decoder = {
    .name           = "dirac",
    .long_name      = NULL_IF_CONFIG_SMALL("BBC Dirac VC-2"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DIRAC,
    .priv_data_size = sizeof(DiracContext),
    .init           = dirac_decode_init,
    .close          = dirac_decode_end,
    .decode         = dirac_decode_frame,
    .capabilities   = CODEC_CAP_DELAY,
    .flush          = dirac_decode_flush,
};
