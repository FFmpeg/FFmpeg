/*
 * Indeo Video v3 compatible decoder
 * Copyright (c) 2009 - 2011 Maxim Poliakovski
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
 * This is a decoder for Intel Indeo Video v3.
 * It is based on vector quantization, run-length coding and motion compensation.
 * Known container formats: .avi and .mov
 * Known FOURCCs: 'IV31', 'IV32'
 *
 * @see http://wiki.multimedia.cx/index.php?title=Indeo_3
 */

#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "copy_block.h"
#include "dsputil.h"
#include "bytestream.h"
#include "get_bits.h"
#include "internal.h"

#include "indeo3data.h"

/* RLE opcodes. */
enum {
    RLE_ESC_F9    = 249, ///< same as RLE_ESC_FA + do the same with next block
    RLE_ESC_FA    = 250, ///< INTRA: skip block, INTER: copy data from reference
    RLE_ESC_FB    = 251, ///< apply null delta to N blocks / skip N blocks
    RLE_ESC_FC    = 252, ///< same as RLE_ESC_FD + do the same with next block
    RLE_ESC_FD    = 253, ///< apply null delta to all remaining lines of this block
    RLE_ESC_FE    = 254, ///< apply null delta to all lines up to the 3rd line
    RLE_ESC_FF    = 255  ///< apply null delta to all lines up to the 2nd line
};


/* Some constants for parsing frame bitstream flags. */
#define BS_8BIT_PEL     (1 << 1) ///< 8bit pixel bitdepth indicator
#define BS_KEYFRAME     (1 << 2) ///< intra frame indicator
#define BS_MV_Y_HALF    (1 << 4) ///< vertical mv halfpel resolution indicator
#define BS_MV_X_HALF    (1 << 5) ///< horizontal mv halfpel resolution indicator
#define BS_NONREF       (1 << 8) ///< nonref (discardable) frame indicator
#define BS_BUFFER        9       ///< indicates which of two frame buffers should be used


typedef struct Plane {
    uint8_t         *buffers[2];
    uint8_t         *pixels[2]; ///< pointer to the actual pixel data of the buffers above
    uint32_t        width;
    uint32_t        height;
    uint32_t        pitch;
} Plane;

#define CELL_STACK_MAX  20

typedef struct Cell {
    int16_t         xpos;       ///< cell coordinates in 4x4 blocks
    int16_t         ypos;
    int16_t         width;      ///< cell width  in 4x4 blocks
    int16_t         height;     ///< cell height in 4x4 blocks
    uint8_t         tree;       ///< tree id: 0- MC tree, 1 - VQ tree
    const int8_t    *mv_ptr;    ///< ptr to the motion vector if any
} Cell;

typedef struct Indeo3DecodeContext {
    AVCodecContext *avctx;
    AVFrame         frame;
    DSPContext      dsp;

    GetBitContext   gb;
    int             need_resync;
    int             skip_bits;
    const uint8_t   *next_cell_data;
    const uint8_t   *last_byte;
    const int8_t    *mc_vectors;
    unsigned        num_vectors;    ///< number of motion vectors in mc_vectors

    int16_t         width, height;
    uint32_t        frame_num;      ///< current frame number (zero-based)
    uint32_t        data_size;      ///< size of the frame data in bytes
    uint16_t        frame_flags;    ///< frame properties
    uint8_t         cb_offset;      ///< needed for selecting VQ tables
    uint8_t         buf_sel;        ///< active frame buffer: 0 - primary, 1 -secondary
    const uint8_t   *y_data_ptr;
    const uint8_t   *v_data_ptr;
    const uint8_t   *u_data_ptr;
    int32_t         y_data_size;
    int32_t         v_data_size;
    int32_t         u_data_size;
    const uint8_t   *alt_quant;     ///< secondary VQ table set for the modes 1 and 4
    Plane           planes[3];
} Indeo3DecodeContext;


static uint8_t requant_tab[8][128];

/*
 *  Build the static requantization table.
 *  This table is used to remap pixel values according to a specific
 *  quant index and thus avoid overflows while adding deltas.
 */
static av_cold void build_requant_tab(void)
{
    static int8_t offsets[8] = { 1, 1, 2, -3, -3, 3, 4, 4 };
    static int8_t deltas [8] = { 0, 1, 0,  4,  4, 1, 0, 1 };

    int i, j, step;

    for (i = 0; i < 8; i++) {
        step = i + 2;
        for (j = 0; j < 128; j++)
                requant_tab[i][j] = (j + offsets[i]) / step * step + deltas[i];
    }

    /* some last elements calculated above will have values >= 128 */
    /* pixel values shall never exceed 127 so set them to non-overflowing values */
    /* according with the quantization step of the respective section */
    requant_tab[0][127] = 126;
    requant_tab[1][119] = 118;
    requant_tab[1][120] = 118;
    requant_tab[2][126] = 124;
    requant_tab[2][127] = 124;
    requant_tab[6][124] = 120;
    requant_tab[6][125] = 120;
    requant_tab[6][126] = 120;
    requant_tab[6][127] = 120;

    /* Patch for compatibility with the Intel's binary decoders */
    requant_tab[1][7] = 10;
    requant_tab[4][8] = 10;
}


static av_cold int allocate_frame_buffers(Indeo3DecodeContext *ctx,
                                          AVCodecContext *avctx, int luma_width, int luma_height)
{
    int p, chroma_width, chroma_height;
    int luma_pitch, chroma_pitch, luma_size, chroma_size;

    if (luma_width  < 16 || luma_width  > 640 ||
        luma_height < 16 || luma_height > 480 ||
        luma_width  &  3 || luma_height &   3) {
        av_log(avctx, AV_LOG_ERROR, "Invalid picture dimensions: %d x %d!\n",
               luma_width, luma_height);
        return AVERROR_INVALIDDATA;
    }

    ctx->width  = luma_width ;
    ctx->height = luma_height;

    chroma_width  = FFALIGN(luma_width  >> 2, 4);
    chroma_height = FFALIGN(luma_height >> 2, 4);

    luma_pitch   = FFALIGN(luma_width,   16);
    chroma_pitch = FFALIGN(chroma_width, 16);

    /* Calculate size of the luminance plane.  */
    /* Add one line more for INTRA prediction. */
    luma_size = luma_pitch * (luma_height + 1);

    /* Calculate size of a chrominance planes. */
    /* Add one line more for INTRA prediction. */
    chroma_size = chroma_pitch * (chroma_height + 1);

    /* allocate frame buffers */
    for (p = 0; p < 3; p++) {
        ctx->planes[p].pitch  = !p ? luma_pitch  : chroma_pitch;
        ctx->planes[p].width  = !p ? luma_width  : chroma_width;
        ctx->planes[p].height = !p ? luma_height : chroma_height;

        ctx->planes[p].buffers[0] = av_malloc(!p ? luma_size : chroma_size);
        ctx->planes[p].buffers[1] = av_malloc(!p ? luma_size : chroma_size);

        /* fill the INTRA prediction lines with the middle pixel value = 64 */
        memset(ctx->planes[p].buffers[0], 0x40, ctx->planes[p].pitch);
        memset(ctx->planes[p].buffers[1], 0x40, ctx->planes[p].pitch);

        /* set buffer pointers = buf_ptr + pitch and thus skip the INTRA prediction line */
        ctx->planes[p].pixels[0] = ctx->planes[p].buffers[0] + ctx->planes[p].pitch;
        ctx->planes[p].pixels[1] = ctx->planes[p].buffers[1] + ctx->planes[p].pitch;
        memset(ctx->planes[p].pixels[0], 0, ctx->planes[p].pitch * ctx->planes[p].height);
        memset(ctx->planes[p].pixels[1], 0, ctx->planes[p].pitch * ctx->planes[p].height);
    }

    return 0;
}


static av_cold void free_frame_buffers(Indeo3DecodeContext *ctx)
{
    int p;

    ctx->width=
    ctx->height= 0;

    for (p = 0; p < 3; p++) {
        av_freep(&ctx->planes[p].buffers[0]);
        av_freep(&ctx->planes[p].buffers[1]);
        ctx->planes[p].pixels[0] = ctx->planes[p].pixels[1] = 0;
    }
}


/**
 *  Copy pixels of the cell(x + mv_x, y + mv_y) from the previous frame into
 *  the cell(x, y) in the current frame.
 *
 *  @param ctx      pointer to the decoder context
 *  @param plane    pointer to the plane descriptor
 *  @param cell     pointer to the cell  descriptor
 */
static void copy_cell(Indeo3DecodeContext *ctx, Plane *plane, Cell *cell)
{
    int     h, w, mv_x, mv_y, offset, offset_dst;
    uint8_t *src, *dst;

    /* setup output and reference pointers */
    offset_dst  = (cell->ypos << 2) * plane->pitch + (cell->xpos << 2);
    dst         = plane->pixels[ctx->buf_sel] + offset_dst;
    if(cell->mv_ptr){
    mv_y        = cell->mv_ptr[0];
    mv_x        = cell->mv_ptr[1];
    }else
        mv_x= mv_y= 0;
    offset      = offset_dst + mv_y * plane->pitch + mv_x;
    src         = plane->pixels[ctx->buf_sel ^ 1] + offset;

    h = cell->height << 2;

    for (w = cell->width; w > 0;) {
        /* copy using 16xH blocks */
        if (!((cell->xpos << 2) & 15) && w >= 4) {
            for (; w >= 4; src += 16, dst += 16, w -= 4)
                ctx->dsp.put_no_rnd_pixels_tab[0][0](dst, src, plane->pitch, h);
        }

        /* copy using 8xH blocks */
        if (!((cell->xpos << 2) & 7) && w >= 2) {
            ctx->dsp.put_no_rnd_pixels_tab[1][0](dst, src, plane->pitch, h);
            w -= 2;
            src += 8;
            dst += 8;
        }

        if (w >= 1) {
            copy_block4(dst, src, plane->pitch, plane->pitch, h);
            w--;
            src += 4;
            dst += 4;
        }
    }
}


/* Average 4/8 pixels at once without rounding using SWAR */
#define AVG_32(dst, src, ref) \
    AV_WN32A(dst, ((AV_RN32A(src) + AV_RN32A(ref)) >> 1) & 0x7F7F7F7FUL)

#define AVG_64(dst, src, ref) \
    AV_WN64A(dst, ((AV_RN64A(src) + AV_RN64A(ref)) >> 1) & 0x7F7F7F7F7F7F7F7FULL)


/*
 *  Replicate each even pixel as follows:
 *  ABCDEFGH -> AACCEEGG
 */
static inline uint64_t replicate64(uint64_t a) {
#if HAVE_BIGENDIAN
    a &= 0xFF00FF00FF00FF00ULL;
    a |= a >> 8;
#else
    a &= 0x00FF00FF00FF00FFULL;
    a |= a << 8;
#endif
    return a;
}

static inline uint32_t replicate32(uint32_t a) {
#if HAVE_BIGENDIAN
    a &= 0xFF00FF00UL;
    a |= a >> 8;
#else
    a &= 0x00FF00FFUL;
    a |= a << 8;
#endif
    return a;
}


/* Fill n lines with 64bit pixel value pix */
static inline void fill_64(uint8_t *dst, const uint64_t pix, int32_t n,
                           int32_t row_offset)
{
    for (; n > 0; dst += row_offset, n--)
        AV_WN64A(dst, pix);
}


/* Error codes for cell decoding. */
enum {
    IV3_NOERR       = 0,
    IV3_BAD_RLE     = 1,
    IV3_BAD_DATA    = 2,
    IV3_BAD_COUNTER = 3,
    IV3_UNSUPPORTED = 4,
    IV3_OUT_OF_DATA = 5
};


#define BUFFER_PRECHECK \
if (*data_ptr >= last_ptr) \
    return IV3_OUT_OF_DATA; \

#define RLE_BLOCK_COPY \
    if (cell->mv_ptr || !skip_flag) \
        copy_block4(dst, ref, row_offset, row_offset, 4 << v_zoom)

#define RLE_BLOCK_COPY_8 \
    pix64 = AV_RN64A(ref);\
    if (is_first_row) {/* special prediction case: top line of a cell */\
        pix64 = replicate64(pix64);\
        fill_64(dst + row_offset, pix64, 7, row_offset);\
        AVG_64(dst, ref, dst + row_offset);\
    } else \
        fill_64(dst, pix64, 8, row_offset)

#define RLE_LINES_COPY \
    copy_block4(dst, ref, row_offset, row_offset, num_lines << v_zoom)

#define RLE_LINES_COPY_M10 \
    pix64 = AV_RN64A(ref);\
    if (is_top_of_cell) {\
        pix64 = replicate64(pix64);\
        fill_64(dst + row_offset, pix64, (num_lines << 1) - 1, row_offset);\
        AVG_64(dst, ref, dst + row_offset);\
    } else \
        fill_64(dst, pix64, num_lines << 1, row_offset)

#define APPLY_DELTA_4 \
    AV_WN16A(dst + line_offset    ,\
             (AV_RN16A(ref    ) + delta_tab->deltas[dyad1]) & 0x7F7F);\
    AV_WN16A(dst + line_offset + 2,\
             (AV_RN16A(ref + 2) + delta_tab->deltas[dyad2]) & 0x7F7F);\
    if (mode >= 3) {\
        if (is_top_of_cell && !cell->ypos) {\
            AV_COPY32(dst, dst + row_offset);\
        } else {\
            AVG_32(dst, ref, dst + row_offset);\
        }\
    }

#define APPLY_DELTA_8 \
    /* apply two 32-bit VQ deltas to next even line */\
    if (is_top_of_cell) { \
        AV_WN32A(dst + row_offset    , \
                 (replicate32(AV_RN32A(ref    )) + delta_tab->deltas_m10[dyad1]) & 0x7F7F7F7F);\
        AV_WN32A(dst + row_offset + 4, \
                 (replicate32(AV_RN32A(ref + 4)) + delta_tab->deltas_m10[dyad2]) & 0x7F7F7F7F);\
    } else { \
        AV_WN32A(dst + row_offset    , \
                 (AV_RN32A(ref    ) + delta_tab->deltas_m10[dyad1]) & 0x7F7F7F7F);\
        AV_WN32A(dst + row_offset + 4, \
                 (AV_RN32A(ref + 4) + delta_tab->deltas_m10[dyad2]) & 0x7F7F7F7F);\
    } \
    /* odd lines are not coded but rather interpolated/replicated */\
    /* first line of the cell on the top of image? - replicate */\
    /* otherwise - interpolate */\
    if (is_top_of_cell && !cell->ypos) {\
        AV_COPY64(dst, dst + row_offset);\
    } else \
        AVG_64(dst, ref, dst + row_offset);


#define APPLY_DELTA_1011_INTER \
    if (mode == 10) { \
        AV_WN32A(dst                 , \
                 (AV_RN32A(dst                 ) + delta_tab->deltas_m10[dyad1]) & 0x7F7F7F7F);\
        AV_WN32A(dst + 4             , \
                 (AV_RN32A(dst + 4             ) + delta_tab->deltas_m10[dyad2]) & 0x7F7F7F7F);\
        AV_WN32A(dst + row_offset    , \
                 (AV_RN32A(dst + row_offset    ) + delta_tab->deltas_m10[dyad1]) & 0x7F7F7F7F);\
        AV_WN32A(dst + row_offset + 4, \
                 (AV_RN32A(dst + row_offset + 4) + delta_tab->deltas_m10[dyad2]) & 0x7F7F7F7F);\
    } else { \
        AV_WN16A(dst                 , \
                 (AV_RN16A(dst                 ) + delta_tab->deltas[dyad1]) & 0x7F7F);\
        AV_WN16A(dst + 2             , \
                 (AV_RN16A(dst + 2             ) + delta_tab->deltas[dyad2]) & 0x7F7F);\
        AV_WN16A(dst + row_offset    , \
                 (AV_RN16A(dst + row_offset    ) + delta_tab->deltas[dyad1]) & 0x7F7F);\
        AV_WN16A(dst + row_offset + 2, \
                 (AV_RN16A(dst + row_offset + 2) + delta_tab->deltas[dyad2]) & 0x7F7F);\
    }


static int decode_cell_data(Indeo3DecodeContext *ctx, Cell *cell,
                            uint8_t *block, uint8_t *ref_block,
                            int pitch, int h_zoom, int v_zoom, int mode,
                            const vqEntry *delta[2], int swap_quads[2],
                            const uint8_t **data_ptr, const uint8_t *last_ptr)
{
    int           x, y, line, num_lines;
    int           rle_blocks = 0;
    uint8_t       code, *dst, *ref;
    const vqEntry *delta_tab;
    unsigned int  dyad1, dyad2;
    uint64_t      pix64;
    int           skip_flag = 0, is_top_of_cell, is_first_row = 1;
    int           row_offset, blk_row_offset, line_offset;

    row_offset     =  pitch;
    blk_row_offset = (row_offset << (2 + v_zoom)) - (cell->width << 2);
    line_offset    = v_zoom ? row_offset : 0;

    if (cell->height & v_zoom || cell->width & h_zoom)
        return IV3_BAD_DATA;

    for (y = 0; y < cell->height; is_first_row = 0, y += 1 + v_zoom) {
        for (x = 0; x < cell->width; x += 1 + h_zoom) {
            ref = ref_block;
            dst = block;

            if (rle_blocks > 0) {
                if (mode <= 4) {
                    RLE_BLOCK_COPY;
                } else if (mode == 10 && !cell->mv_ptr) {
                    RLE_BLOCK_COPY_8;
                }
                rle_blocks--;
            } else {
                for (line = 0; line < 4;) {
                    num_lines = 1;
                    is_top_of_cell = is_first_row && !line;

                    /* select primary VQ table for odd, secondary for even lines */
                    if (mode <= 4)
                        delta_tab = delta[line & 1];
                    else
                        delta_tab = delta[1];
                    BUFFER_PRECHECK;
                    code = bytestream_get_byte(data_ptr);
                    if (code < 248) {
                        if (code < delta_tab->num_dyads) {
                            BUFFER_PRECHECK;
                            dyad1 = bytestream_get_byte(data_ptr);
                            dyad2 = code;
                            if (dyad1 >= delta_tab->num_dyads || dyad1 >= 248)
                                return IV3_BAD_DATA;
                        } else {
                            /* process QUADS */
                            code -= delta_tab->num_dyads;
                            dyad1 = code / delta_tab->quad_exp;
                            dyad2 = code % delta_tab->quad_exp;
                            if (swap_quads[line & 1])
                                FFSWAP(unsigned int, dyad1, dyad2);
                        }
                        if (mode <= 4) {
                            APPLY_DELTA_4;
                        } else if (mode == 10 && !cell->mv_ptr) {
                            APPLY_DELTA_8;
                        } else {
                            APPLY_DELTA_1011_INTER;
                        }
                    } else {
                        /* process RLE codes */
                        switch (code) {
                        case RLE_ESC_FC:
                            skip_flag  = 0;
                            rle_blocks = 1;
                            code       = 253;
                            /* FALLTHROUGH */
                        case RLE_ESC_FF:
                        case RLE_ESC_FE:
                        case RLE_ESC_FD:
                            num_lines = 257 - code - line;
                            if (num_lines <= 0)
                                return IV3_BAD_RLE;
                            if (mode <= 4) {
                                RLE_LINES_COPY;
                            } else if (mode == 10 && !cell->mv_ptr) {
                                RLE_LINES_COPY_M10;
                            }
                            break;
                        case RLE_ESC_FB:
                            BUFFER_PRECHECK;
                            code = bytestream_get_byte(data_ptr);
                            rle_blocks = (code & 0x1F) - 1; /* set block counter */
                            if (code >= 64 || rle_blocks < 0)
                                return IV3_BAD_COUNTER;
                            skip_flag = code & 0x20;
                            num_lines = 4 - line; /* enforce next block processing */
                            if (mode >= 10 || (cell->mv_ptr || !skip_flag)) {
                                if (mode <= 4) {
                                    RLE_LINES_COPY;
                                } else if (mode == 10 && !cell->mv_ptr) {
                                    RLE_LINES_COPY_M10;
                                }
                            }
                            break;
                        case RLE_ESC_F9:
                            skip_flag  = 1;
                            rle_blocks = 1;
                            /* FALLTHROUGH */
                        case RLE_ESC_FA:
                            if (line)
                                return IV3_BAD_RLE;
                            num_lines = 4; /* enforce next block processing */
                            if (cell->mv_ptr) {
                                if (mode <= 4) {
                                    RLE_LINES_COPY;
                                } else if (mode == 10 && !cell->mv_ptr) {
                                    RLE_LINES_COPY_M10;
                                }
                            }
                            break;
                        default:
                            return IV3_UNSUPPORTED;
                        }
                    }

                    line += num_lines;
                    ref  += row_offset * (num_lines << v_zoom);
                    dst  += row_offset * (num_lines << v_zoom);
                }
            }

            /* move to next horizontal block */
            block     += 4 << h_zoom;
            ref_block += 4 << h_zoom;
        }

        /* move to next line of blocks */
        ref_block += blk_row_offset;
        block     += blk_row_offset;
    }
    return IV3_NOERR;
}


/**
 *  Decode a vector-quantized cell.
 *  It consists of several routines, each of which handles one or more "modes"
 *  with which a cell can be encoded.
 *
 *  @param ctx      pointer to the decoder context
 *  @param avctx    ptr to the AVCodecContext
 *  @param plane    pointer to the plane descriptor
 *  @param cell     pointer to the cell  descriptor
 *  @param data_ptr pointer to the compressed data
 *  @param last_ptr pointer to the last byte to catch reads past end of buffer
 *  @return         number of consumed bytes or negative number in case of error
 */
static int decode_cell(Indeo3DecodeContext *ctx, AVCodecContext *avctx,
                       Plane *plane, Cell *cell, const uint8_t *data_ptr,
                       const uint8_t *last_ptr)
{
    int           x, mv_x, mv_y, mode, vq_index, prim_indx, second_indx;
    int           zoom_fac;
    int           offset, error = 0, swap_quads[2];
    uint8_t       code, *block, *ref_block = 0;
    const vqEntry *delta[2];
    const uint8_t *data_start = data_ptr;

    /* get coding mode and VQ table index from the VQ descriptor byte */
    code     = *data_ptr++;
    mode     = code >> 4;
    vq_index = code & 0xF;

    /* setup output and reference pointers */
    offset = (cell->ypos << 2) * plane->pitch + (cell->xpos << 2);
    block  =  plane->pixels[ctx->buf_sel] + offset;

    if (cell->mv_ptr) {
        mv_y      = cell->mv_ptr[0];
        mv_x      = cell->mv_ptr[1];
        if (   mv_x + 4*cell->xpos < 0
            || mv_y + 4*cell->ypos < 0
            || mv_x + 4*cell->xpos + 4*cell->width  > plane->width
            || mv_y + 4*cell->ypos + 4*cell->height > plane->height) {
            av_log(avctx, AV_LOG_ERROR, "motion vector %d %d outside reference\n", mv_x + 4*cell->xpos, mv_y + 4*cell->ypos);
            return AVERROR_INVALIDDATA;
        }
    }

    if (!cell->mv_ptr) {
        /* use previous line as reference for INTRA cells */
        ref_block = block - plane->pitch;
    } else if (mode >= 10) {
        /* for mode 10 and 11 INTER first copy the predicted cell into the current one */
        /* so we don't need to do data copying for each RLE code later */
        copy_cell(ctx, plane, cell);
    } else {
        /* set the pointer to the reference pixels for modes 0-4 INTER */
        mv_y      = cell->mv_ptr[0];
        mv_x      = cell->mv_ptr[1];
        offset   += mv_y * plane->pitch + mv_x;
        ref_block = plane->pixels[ctx->buf_sel ^ 1] + offset;
    }

    /* select VQ tables as follows: */
    /* modes 0 and 3 use only the primary table for all lines in a block */
    /* while modes 1 and 4 switch between primary and secondary tables on alternate lines */
    if (mode == 1 || mode == 4) {
        code        = ctx->alt_quant[vq_index];
        prim_indx   = (code >> 4)  + ctx->cb_offset;
        second_indx = (code & 0xF) + ctx->cb_offset;
    } else {
        vq_index += ctx->cb_offset;
        prim_indx = second_indx = vq_index;
    }

    if (prim_indx >= 24 || second_indx >= 24) {
        av_log(avctx, AV_LOG_ERROR, "Invalid VQ table indexes! Primary: %d, secondary: %d!\n",
               prim_indx, second_indx);
        return AVERROR_INVALIDDATA;
    }

    delta[0] = &vq_tab[second_indx];
    delta[1] = &vq_tab[prim_indx];
    swap_quads[0] = second_indx >= 16;
    swap_quads[1] = prim_indx   >= 16;

    /* requantize the prediction if VQ index of this cell differs from VQ index */
    /* of the predicted cell in order to avoid overflows. */
    if (vq_index >= 8 && ref_block) {
        for (x = 0; x < cell->width << 2; x++)
            ref_block[x] = requant_tab[vq_index & 7][ref_block[x] & 127];
    }

    error = IV3_NOERR;

    switch (mode) {
    case 0: /*------------------ MODES 0 & 1 (4x4 block processing) --------------------*/
    case 1:
    case 3: /*------------------ MODES 3 & 4 (4x8 block processing) --------------------*/
    case 4:
        if (mode >= 3 && cell->mv_ptr) {
            av_log(avctx, AV_LOG_ERROR, "Attempt to apply Mode 3/4 to an INTER cell!\n");
            return AVERROR_INVALIDDATA;
        }

        zoom_fac = mode >= 3;
        error = decode_cell_data(ctx, cell, block, ref_block, plane->pitch,
                                 0, zoom_fac, mode, delta, swap_quads,
                                 &data_ptr, last_ptr);
        break;
    case 10: /*-------------------- MODE 10 (8x8 block processing) ---------------------*/
    case 11: /*----------------- MODE 11 (4x8 INTER block processing) ------------------*/
        if (mode == 10 && !cell->mv_ptr) { /* MODE 10 INTRA processing */
            error = decode_cell_data(ctx, cell, block, ref_block, plane->pitch,
                                     1, 1, mode, delta, swap_quads,
                                     &data_ptr, last_ptr);
        } else { /* mode 10 and 11 INTER processing */
            if (mode == 11 && !cell->mv_ptr) {
               av_log(avctx, AV_LOG_ERROR, "Attempt to use Mode 11 for an INTRA cell!\n");
               return AVERROR_INVALIDDATA;
            }

            zoom_fac = mode == 10;
            error = decode_cell_data(ctx, cell, block, ref_block, plane->pitch,
                                     zoom_fac, 1, mode, delta, swap_quads,
                                     &data_ptr, last_ptr);
        }
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported coding mode: %d\n", mode);
        return AVERROR_INVALIDDATA;
    }//switch mode

    switch (error) {
    case IV3_BAD_RLE:
        av_log(avctx, AV_LOG_ERROR, "Mode %d: RLE code %X is not allowed at the current line\n",
               mode, data_ptr[-1]);
        return AVERROR_INVALIDDATA;
    case IV3_BAD_DATA:
        av_log(avctx, AV_LOG_ERROR, "Mode %d: invalid VQ data\n", mode);
        return AVERROR_INVALIDDATA;
    case IV3_BAD_COUNTER:
        av_log(avctx, AV_LOG_ERROR, "Mode %d: RLE-FB invalid counter: %d\n", mode, code);
        return AVERROR_INVALIDDATA;
    case IV3_UNSUPPORTED:
        av_log(avctx, AV_LOG_ERROR, "Mode %d: unsupported RLE code: %X\n", mode, data_ptr[-1]);
        return AVERROR_INVALIDDATA;
    case IV3_OUT_OF_DATA:
        av_log(avctx, AV_LOG_ERROR, "Mode %d: attempt to read past end of buffer\n", mode);
        return AVERROR_INVALIDDATA;
    }

    return data_ptr - data_start; /* report number of bytes consumed from the input buffer */
}


/* Binary tree codes. */
enum {
    H_SPLIT    = 0,
    V_SPLIT    = 1,
    INTRA_NULL = 2,
    INTER_DATA = 3
};


#define SPLIT_CELL(size, new_size) (new_size) = ((size) > 2) ? ((((size) + 2) >> 2) << 1) : 1

#define UPDATE_BITPOS(n) \
    ctx->skip_bits  += (n); \
    ctx->need_resync = 1

#define RESYNC_BITSTREAM \
    if (ctx->need_resync && !(get_bits_count(&ctx->gb) & 7)) { \
        skip_bits_long(&ctx->gb, ctx->skip_bits);              \
        ctx->skip_bits   = 0;                                  \
        ctx->need_resync = 0;                                  \
    }

#define CHECK_CELL \
    if (curr_cell.xpos + curr_cell.width > (plane->width >> 2) ||               \
        curr_cell.ypos + curr_cell.height > (plane->height >> 2)) {             \
        av_log(avctx, AV_LOG_ERROR, "Invalid cell: x=%d, y=%d, w=%d, h=%d\n",   \
               curr_cell.xpos, curr_cell.ypos, curr_cell.width, curr_cell.height); \
        return AVERROR_INVALIDDATA;                                                              \
    }


static int parse_bintree(Indeo3DecodeContext *ctx, AVCodecContext *avctx,
                         Plane *plane, int code, Cell *ref_cell,
                         const int depth, const int strip_width)
{
    Cell    curr_cell;
    int     bytes_used;
    int mv_x, mv_y;

    if (depth <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Stack overflow (corrupted binary tree)!\n");
        return AVERROR_INVALIDDATA; // unwind recursion
    }

    curr_cell = *ref_cell; // clone parent cell
    if (code == H_SPLIT) {
        SPLIT_CELL(ref_cell->height, curr_cell.height);
        ref_cell->ypos   += curr_cell.height;
        ref_cell->height -= curr_cell.height;
        if (ref_cell->height <= 0 || curr_cell.height <= 0)
            return AVERROR_INVALIDDATA;
    } else if (code == V_SPLIT) {
        if (curr_cell.width > strip_width) {
            /* split strip */
            curr_cell.width = (curr_cell.width <= (strip_width << 1) ? 1 : 2) * strip_width;
        } else
            SPLIT_CELL(ref_cell->width, curr_cell.width);
        ref_cell->xpos  += curr_cell.width;
        ref_cell->width -= curr_cell.width;
        if (ref_cell->width <= 0 || curr_cell.width <= 0)
            return AVERROR_INVALIDDATA;
    }

    while (get_bits_left(&ctx->gb) >= 2) { /* loop until return */
        RESYNC_BITSTREAM;
        switch (code = get_bits(&ctx->gb, 2)) {
        case H_SPLIT:
        case V_SPLIT:
            if (parse_bintree(ctx, avctx, plane, code, &curr_cell, depth - 1, strip_width))
                return AVERROR_INVALIDDATA;
            break;
        case INTRA_NULL:
            if (!curr_cell.tree) { /* MC tree INTRA code */
                curr_cell.mv_ptr = 0; /* mark the current strip as INTRA */
                curr_cell.tree   = 1; /* enter the VQ tree */
            } else { /* VQ tree NULL code */
                RESYNC_BITSTREAM;
                code = get_bits(&ctx->gb, 2);
                if (code >= 2) {
                    av_log(avctx, AV_LOG_ERROR, "Invalid VQ_NULL code: %d\n", code);
                    return AVERROR_INVALIDDATA;
                }
                if (code == 1)
                    av_log(avctx, AV_LOG_ERROR, "SkipCell procedure not implemented yet!\n");

                CHECK_CELL
                if (!curr_cell.mv_ptr)
                    return AVERROR_INVALIDDATA;

                mv_y = curr_cell.mv_ptr[0];
                mv_x = curr_cell.mv_ptr[1];
                if (   mv_x + 4*curr_cell.xpos < 0
                    || mv_y + 4*curr_cell.ypos < 0
                    || mv_x + 4*curr_cell.xpos + 4*curr_cell.width  > plane->width
                    || mv_y + 4*curr_cell.ypos + 4*curr_cell.height > plane->height) {
                    av_log(avctx, AV_LOG_ERROR, "motion vector %d %d outside reference\n", mv_x + 4*curr_cell.xpos, mv_y + 4*curr_cell.ypos);
                    return AVERROR_INVALIDDATA;
                }

                copy_cell(ctx, plane, &curr_cell);
                return 0;
            }
            break;
        case INTER_DATA:
            if (!curr_cell.tree) { /* MC tree INTER code */
                unsigned mv_idx;
                /* get motion vector index and setup the pointer to the mv set */
                if (!ctx->need_resync)
                    ctx->next_cell_data = &ctx->gb.buffer[(get_bits_count(&ctx->gb) + 7) >> 3];
                if (ctx->next_cell_data >= ctx->last_byte) {
                    av_log(avctx, AV_LOG_ERROR, "motion vector out of array\n");
                    return AVERROR_INVALIDDATA;
                }
                mv_idx = *(ctx->next_cell_data++);
                if (mv_idx >= ctx->num_vectors) {
                    av_log(avctx, AV_LOG_ERROR, "motion vector index out of range\n");
                    return AVERROR_INVALIDDATA;
                }
                curr_cell.mv_ptr = &ctx->mc_vectors[mv_idx << 1];
                curr_cell.tree   = 1; /* enter the VQ tree */
                UPDATE_BITPOS(8);
            } else { /* VQ tree DATA code */
                if (!ctx->need_resync)
                    ctx->next_cell_data = &ctx->gb.buffer[(get_bits_count(&ctx->gb) + 7) >> 3];

                CHECK_CELL
                bytes_used = decode_cell(ctx, avctx, plane, &curr_cell,
                                         ctx->next_cell_data, ctx->last_byte);
                if (bytes_used < 0)
                    return AVERROR_INVALIDDATA;

                UPDATE_BITPOS(bytes_used << 3);
                ctx->next_cell_data += bytes_used;
                return 0;
            }
            break;
        }
    }//while

    return AVERROR_INVALIDDATA;
}


static int decode_plane(Indeo3DecodeContext *ctx, AVCodecContext *avctx,
                        Plane *plane, const uint8_t *data, int32_t data_size,
                        int32_t strip_width)
{
    Cell            curr_cell;
    unsigned        num_vectors;

    /* each plane data starts with mc_vector_count field, */
    /* an optional array of motion vectors followed by the vq data */
    num_vectors = bytestream_get_le32(&data); data_size -= 4;
    if (num_vectors > 256) {
        av_log(ctx->avctx, AV_LOG_ERROR,
               "Read invalid number of motion vectors %d\n", num_vectors);
        return AVERROR_INVALIDDATA;
    }
    if (num_vectors * 2 > data_size)
        return AVERROR_INVALIDDATA;

    ctx->num_vectors = num_vectors;
    ctx->mc_vectors  = num_vectors ? data : 0;

    /* init the bitreader */
    init_get_bits(&ctx->gb, &data[num_vectors * 2], (data_size - num_vectors * 2) << 3);
    ctx->skip_bits   = 0;
    ctx->need_resync = 0;

    ctx->last_byte = data + data_size;

    /* initialize the 1st cell and set its dimensions to whole plane */
    curr_cell.xpos   = curr_cell.ypos = 0;
    curr_cell.width  = plane->width  >> 2;
    curr_cell.height = plane->height >> 2;
    curr_cell.tree   = 0; // we are in the MC tree now
    curr_cell.mv_ptr = 0; // no motion vector = INTRA cell

    return parse_bintree(ctx, avctx, plane, INTRA_NULL, &curr_cell, CELL_STACK_MAX, strip_width);
}


#define OS_HDR_ID   MKBETAG('F', 'R', 'M', 'H')

static int decode_frame_headers(Indeo3DecodeContext *ctx, AVCodecContext *avctx,
                                const uint8_t *buf, int buf_size)
{
    const uint8_t   *buf_ptr = buf, *bs_hdr;
    uint32_t        frame_num, word2, check_sum, data_size;
    uint32_t        y_offset, u_offset, v_offset, starts[3], ends[3];
    uint16_t        height, width;
    int             i, j;

    /* parse and check the OS header */
    frame_num = bytestream_get_le32(&buf_ptr);
    word2     = bytestream_get_le32(&buf_ptr);
    check_sum = bytestream_get_le32(&buf_ptr);
    data_size = bytestream_get_le32(&buf_ptr);

    if ((frame_num ^ word2 ^ data_size ^ OS_HDR_ID) != check_sum) {
        av_log(avctx, AV_LOG_ERROR, "OS header checksum mismatch!\n");
        return AVERROR_INVALIDDATA;
    }

    /* parse the bitstream header */
    bs_hdr = buf_ptr;
    buf_size -= 16;

    if (bytestream_get_le16(&buf_ptr) != 32) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported codec version!\n");
        return AVERROR_INVALIDDATA;
    }

    ctx->frame_num   =  frame_num;
    ctx->frame_flags =  bytestream_get_le16(&buf_ptr);
    ctx->data_size   = (bytestream_get_le32(&buf_ptr) + 7) >> 3;
    ctx->cb_offset   = *buf_ptr++;

    if (ctx->data_size == 16)
        return 4;
    if (ctx->data_size > buf_size)
        ctx->data_size = buf_size;

    buf_ptr += 3; // skip reserved byte and checksum

    /* check frame dimensions */
    height = bytestream_get_le16(&buf_ptr);
    width  = bytestream_get_le16(&buf_ptr);
    if (av_image_check_size(width, height, 0, avctx))
        return AVERROR_INVALIDDATA;

    if (width != ctx->width || height != ctx->height) {
        int res;

        av_dlog(avctx, "Frame dimensions changed!\n");

        if (width  < 16 || width  > 640 ||
            height < 16 || height > 480 ||
            width  &  3 || height &   3) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid picture dimensions: %d x %d!\n", width, height);
            return AVERROR_INVALIDDATA;
        }
        free_frame_buffers(ctx);
        if ((res = allocate_frame_buffers(ctx, avctx, width, height)) < 0)
             return res;
        avcodec_set_dimensions(avctx, width, height);
    }

    y_offset = bytestream_get_le32(&buf_ptr);
    v_offset = bytestream_get_le32(&buf_ptr);
    u_offset = bytestream_get_le32(&buf_ptr);

    /* unfortunately there is no common order of planes in the buffer */
    /* so we use that sorting algo for determining planes data sizes  */
    starts[0] = y_offset;
    starts[1] = v_offset;
    starts[2] = u_offset;

    for (j = 0; j < 3; j++) {
        ends[j] = ctx->data_size;
        for (i = 2; i >= 0; i--)
            if (starts[i] < ends[j] && starts[i] > starts[j])
                ends[j] = starts[i];
    }

    ctx->y_data_size = ends[0] - starts[0];
    ctx->v_data_size = ends[1] - starts[1];
    ctx->u_data_size = ends[2] - starts[2];
    if (FFMAX3(y_offset, v_offset, u_offset) >= ctx->data_size - 16 ||
        FFMIN3(ctx->y_data_size, ctx->v_data_size, ctx->u_data_size) <= 0) {
        av_log(avctx, AV_LOG_ERROR, "One of the y/u/v offsets is invalid\n");
        return AVERROR_INVALIDDATA;
    }

    ctx->y_data_ptr = bs_hdr + y_offset;
    ctx->v_data_ptr = bs_hdr + v_offset;
    ctx->u_data_ptr = bs_hdr + u_offset;
    ctx->alt_quant  = buf_ptr + sizeof(uint32_t);

    if (ctx->data_size == 16) {
        av_log(avctx, AV_LOG_DEBUG, "Sync frame encountered!\n");
        return 16;
    }

    if (ctx->frame_flags & BS_8BIT_PEL) {
        av_log_ask_for_sample(avctx, "8-bit pixel format\n");
        return AVERROR_PATCHWELCOME;
    }

    if (ctx->frame_flags & BS_MV_X_HALF || ctx->frame_flags & BS_MV_Y_HALF) {
        av_log_ask_for_sample(avctx, "halfpel motion vectors\n");
        return AVERROR_PATCHWELCOME;
    }

    return 0;
}


/**
 *  Convert and output the current plane.
 *  All pixel values will be upsampled by shifting right by one bit.
 *
 *  @param[in]  plane        pointer to the descriptor of the plane being processed
 *  @param[in]  buf_sel      indicates which frame buffer the input data stored in
 *  @param[out] dst          pointer to the buffer receiving converted pixels
 *  @param[in]  dst_pitch    pitch for moving to the next y line
 *  @param[in]  dst_height   output plane height
 */
static void output_plane(const Plane *plane, int buf_sel, uint8_t *dst,
                         int dst_pitch, int dst_height)
{
    int             x,y;
    const uint8_t   *src  = plane->pixels[buf_sel];
    uint32_t        pitch = plane->pitch;

    dst_height = FFMIN(dst_height, plane->height);
    for (y = 0; y < dst_height; y++) {
        /* convert four pixels at once using SWAR */
        for (x = 0; x < plane->width >> 2; x++) {
            AV_WN32A(dst, (AV_RN32A(src) & 0x7F7F7F7F) << 1);
            src += 4;
            dst += 4;
        }

        for (x <<= 2; x < plane->width; x++)
            *dst++ = *src++ << 1;

        src += pitch     - plane->width;
        dst += dst_pitch - plane->width;
    }
}


static av_cold int decode_init(AVCodecContext *avctx)
{
    Indeo3DecodeContext *ctx = avctx->priv_data;

    ctx->avctx     = avctx;
    avctx->pix_fmt = AV_PIX_FMT_YUV410P;
    avcodec_get_frame_defaults(&ctx->frame);

    build_requant_tab();

    ff_dsputil_init(&ctx->dsp, avctx);

    return allocate_frame_buffers(ctx, avctx, avctx->width, avctx->height);
}


static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                        AVPacket *avpkt)
{
    Indeo3DecodeContext *ctx = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    int res;

    res = decode_frame_headers(ctx, avctx, buf, buf_size);
    if (res < 0)
        return res;

    /* skip sync(null) frames */
    if (res) {
        // we have processed 16 bytes but no data was decoded
        *got_frame = 0;
        return buf_size;
    }

    /* skip droppable INTER frames if requested */
    if (ctx->frame_flags & BS_NONREF &&
       (avctx->skip_frame >= AVDISCARD_NONREF))
        return 0;

    /* skip INTER frames if requested */
    if (!(ctx->frame_flags & BS_KEYFRAME) && avctx->skip_frame >= AVDISCARD_NONKEY)
        return 0;

    /* use BS_BUFFER flag for buffer switching */
    ctx->buf_sel = (ctx->frame_flags >> BS_BUFFER) & 1;

    if (ctx->frame.data[0])
        avctx->release_buffer(avctx, &ctx->frame);

    ctx->frame.reference = 0;
    if ((res = ff_get_buffer(avctx, &ctx->frame)) < 0) {
        av_log(ctx->avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return res;
    }

    /* decode luma plane */
    if ((res = decode_plane(ctx, avctx, ctx->planes, ctx->y_data_ptr, ctx->y_data_size, 40)))
        return res;

    /* decode chroma planes */
    if ((res = decode_plane(ctx, avctx, &ctx->planes[1], ctx->u_data_ptr, ctx->u_data_size, 10)))
        return res;

    if ((res = decode_plane(ctx, avctx, &ctx->planes[2], ctx->v_data_ptr, ctx->v_data_size, 10)))
        return res;

    output_plane(&ctx->planes[0], ctx->buf_sel,
                 ctx->frame.data[0], ctx->frame.linesize[0],
                 avctx->height);
    output_plane(&ctx->planes[1], ctx->buf_sel,
                 ctx->frame.data[1], ctx->frame.linesize[1],
                 (avctx->height + 3) >> 2);
    output_plane(&ctx->planes[2], ctx->buf_sel,
                 ctx->frame.data[2], ctx->frame.linesize[2],
                 (avctx->height + 3) >> 2);

    *got_frame = 1;
    *(AVFrame*)data = ctx->frame;

    return buf_size;
}


static av_cold int decode_close(AVCodecContext *avctx)
{
    Indeo3DecodeContext *ctx = avctx->priv_data;

    free_frame_buffers(avctx->priv_data);

    if (ctx->frame.data[0])
        avctx->release_buffer(avctx, &ctx->frame);

    return 0;
}

AVCodec ff_indeo3_decoder = {
    .name           = "indeo3",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_INDEO3,
    .priv_data_size = sizeof(Indeo3DecodeContext),
    .init           = decode_init,
    .close          = decode_close,
    .decode         = decode_frame,
    .long_name      = NULL_IF_CONFIG_SMALL("Intel Indeo 3"),
    .capabilities   = CODEC_CAP_DR1,
};
