/**
 * Copyright (C) 2025 Niklas Haas
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

#ifndef SWSCALE_UOPS_H
#define SWSCALE_UOPS_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

/***************************************************************************
 * Note: This header must be usable at build time, to generate asm sources *
 ***************************************************************************/

#include "libavutil/attributes.h"

typedef struct SwsContext       SwsContext;
typedef struct SwsFilterWeights SwsFilterWeights;
typedef struct SwsLut3D         SwsLut3D;
typedef struct SwsOpList        SwsOpList;

typedef enum SwsPixelType {
    SWS_PIXEL_NONE = 0,
    SWS_PIXEL_U8,
    SWS_PIXEL_U16,
    SWS_PIXEL_U32,
    SWS_PIXEL_F32,
    SWS_PIXEL_TYPE_NB
} SwsPixelType;

const char *ff_sws_pixel_type_name(SwsPixelType type);

static inline av_const int ff_sws_pixel_type_size(SwsPixelType type)
{
    switch (type) {
    case SWS_PIXEL_U8:  return sizeof(uint8_t);
    case SWS_PIXEL_U16: return sizeof(uint16_t);
    case SWS_PIXEL_U32: return sizeof(uint32_t);
    case SWS_PIXEL_F32: return sizeof(float);
    case SWS_PIXEL_NONE: break;
    case SWS_PIXEL_TYPE_NB: break;
    }
    return 0;
}

static inline av_const bool ff_sws_pixel_type_is_int(SwsPixelType type)
{
    switch (type) {
    case SWS_PIXEL_U8:
    case SWS_PIXEL_U16:
    case SWS_PIXEL_U32:
        return true;
    case SWS_PIXEL_F32:
        return false;
    case SWS_PIXEL_NONE:
    case SWS_PIXEL_TYPE_NB: break;
    }
    return false;
}

typedef union SwsPixel {
    char data[4];

    uint8_t  u8;
    uint16_t u16;
    uint32_t u32;
    float    f32;
} SwsPixel;

/* Ensures (SwsPixel) {0} is properly initialized to all zeros */
static_assert(sizeof(SwsPixel) == sizeof(char[4]), "SwsPixel size mismatch");

/**
 * Bit-mask of components. Exact meaning depends on the usage context.
 */
typedef uint8_t SwsCompMask;
enum {
    SWS_COMP_NONE = 0,
    SWS_COMP_ALL  = 0xF,
#define SWS_COMP(X) (1 << (X))
#define SWS_COMP_TEST(mask, X) (!!((mask) & SWS_COMP(X)))
#define SWS_COMP_INV(mask) ((mask) ^ SWS_COMP_ALL)
#define SWS_COMP_ELEMS(N) ((1 << (N)) - 1)
#define SWS_COMP_COUNT(mask) (av_popcount((mask) & SWS_COMP_ALL))
#define SWS_COMP_MASK(X, Y, Z, W)   \
    (((X) ? SWS_COMP(0) : 0) |      \
     ((Y) ? SWS_COMP(1) : 0) |      \
     ((Z) ? SWS_COMP(2) : 0) |      \
     ((W) ? SWS_COMP(3) : 0))
};


#define ff_sws_comp_mask_str(mask) ff_sws_comp_mask_print(mask, (char[5]){0})
static inline char *ff_sws_comp_mask_print(SwsCompMask mask, char buf[5])
{
    char *ptr = buf;
    for (int c = 0; c < 4; c++) {
        if (SWS_COMP_TEST(mask, c))
            *ptr++ = "xyzw"[c];
    }
    *ptr = '\0';
    return buf;
}

typedef uint32_t SwsUOpFlags;
typedef enum SwsUOpFlagBits {
    SWS_UOP_FLAG_NONE   = 0,
    SWS_UOP_FLAG_FMA    = (1 << 0), /* platform supports FMA ops */
    SWS_UOP_FLAG_PSHUFB = (1 << 1), /* platform supports pshufb equivalent */
} SwsUOpFlagBits;

typedef enum SwsUOpType {
    SWS_UOP_INVALID = 0,

    /* Read/write uops; mask = components to read/write */
    SWS_UOP_READ_PLANAR,     /* simple planar byte-aligned read */
    SWS_UOP_READ_PLANAR_FH,  /* planar read with horizontal filter */
    SWS_UOP_READ_PLANAR_FV,  /* planar read with vertical filter */
    SWS_UOP_READ_PLANAR_FV_FMA,
    SWS_UOP_READ_PACKED,     /* simple packed byte-aligned read */
    SWS_UOP_READ_NIBBLE,     /* fractional read (4 bits) from single plane */
    SWS_UOP_READ_BIT,        /* fractional read (1 bit) from single plane */
    SWS_UOP_READ_PALETTE,    /* indexed read from palette in plane 1 */

    SWS_UOP_WRITE_PLANAR,    /* simple planar byte-aligned write */
    SWS_UOP_WRITE_PACKED,    /* simple packed byte-aligned write */
    SWS_UOP_WRITE_NIBBLE,    /* fractional write (4 bits) to single plane */
    SWS_UOP_WRITE_BIT,       /* fractional write (1 bit) to single plane */

    /* Packed shuffle / gather uops */
    SWS_UOP_RW_SHUFFLE,      /* in-place (packed) indexed shuffle/gather */

    /* Data rearrangement uops; mask = needed or trivial components */
    SWS_UOP_PERMUTE,         /* permute pointers (no duplicates) */
    SWS_UOP_COPY,            /* permute data (may contain duplicates) */

    /* Data conversion / manipulation uops; mask = affected components */
    SWS_UOP_SWAP_BYTES,      /* swap byte order in components */
    SWS_UOP_EXPAND_BIT,      /* expand low-order bit to all bits in type */
    SWS_UOP_EXPAND_PAIR,     /* expand bytes in pairs (16 bit) */
    SWS_UOP_EXPAND_QUAD,     /* expand bytes in quads (32 bit) */
    SWS_UOP_TO_U8,           /* cast pixel values to SWS_PIXEL_U8  */
    SWS_UOP_TO_U16,          /* cast pixel values to SWS_PIXEL_U16 */
    SWS_UOP_TO_U32,          /* cast pixel values to SWS_PIXEL_U32 */
    SWS_UOP_TO_F32,          /* cast pixel values to SWS_PIXEL_F32 */

    /* Arithmetic uops */
    SWS_UOP_SCALE,           /* multiply masked components by scalar */
    SWS_UOP_ADD,             /* add vec4 to masked components */
    SWS_UOP_MIN,             /* min(x, vec4) on masked components */
    SWS_UOP_MAX,             /* max(x, vec4) on masked components */

    /* Identical to corresponding SwsOpType */
    SWS_UOP_UNPACK,          /* mask = nonzero components in pack pattern */
    SWS_UOP_PACK,            /* mask = nonzero components in pack pattern */
    SWS_UOP_LSHIFT,          /* mask = components to shift */
    SWS_UOP_RSHIFT,          /* mask = components to shift */
    SWS_UOP_CLEAR,           /* mask = components to clear */
    SWS_UOP_LINEAR,          /* mask = non-trivial output rows */
    SWS_UOP_LINEAR_FMA,      /* with SWS_UOP_FLAG_FMA */
    SWS_UOP_DITHER,          /* mask = components to dither */
    SWS_UOP_LUT_3D,          /* mask = needed output components */

    /* Platform-specific uops would go here */
    SWS_UOP_TYPE_NB,
} SwsUOpType;

typedef struct SwsShuffleUOp {
    uint8_t clear_value; /* value to clear elements with negative indices to */
    uint8_t read_size;   /* input bytes per iteration */
    uint8_t write_size;  /* output bytes per iteration */
} SwsShuffleUOp;

typedef struct SwsShuffleMask {
    int8_t mask[16];    /* shuffle index mask, or -1 to clear bytes (to `clear_value`) */
    uint8_t pixels;     /* number of pixels per iteration */
} SwsShuffleMask;

typedef struct SwsFilterUOp {
    SwsPixelType type; /* pixel type to store result as */
} SwsFilterUOp;

typedef struct SwsShiftUOp {
    uint8_t amount;
} SwsShiftUOp;

typedef struct SwsMoveUOp {
    /* The worst case number of moves (for two independent cycles) */
    #define SWS_UOP_MOVE_MAX 6
    int num_moves;

    /* This may involve a temporary register (index -1) */
    int8_t dst[SWS_UOP_MOVE_MAX]; /* destination register index */
    int8_t src[SWS_UOP_MOVE_MAX]; /* source register index */
} SwsMoveUOp;

typedef struct SwsPackUOp {
    uint8_t pattern[4]; /* bit depth pattern, from MSB to LSB */
} SwsPackUOp;

typedef struct SwsClearUOp {
    SwsCompMask one;  /* mask of coefficients equal to all 1s */
    SwsCompMask zero; /* mask of coefficients equal to all 0s */
} SwsClearUOp;

typedef struct SwsLinearUOp {
    uint32_t one;  /* mask of coefficients equal to one */
    uint32_t zero; /* mask of coefficients equal to zero */

    /* for SWS_UOP_LINEAR_FMA only */
    uint32_t exact; /* mask of coefficients whose product is exact */
} SwsLinearUOp;

#define SWS_MASK(I, J)  (1 << (5 * (I) + (J)))
#define SWS_MASK_OFF(I) SWS_MASK(I, 4)
#define SWS_MASK_ROW(I) (0x1F << (5 * (I)))
#define SWS_MASK_COL(J) (0x8421 << J)
#define SWS_MASK_DIAG4  (0x41041)

typedef struct SwsDitherUOp {
    uint8_t y_offset[4];
    uint8_t size_log2;
} SwsDitherUOp;

typedef struct SwsLut3DUOp {
    int dynamic;
} SwsLut3DUOp;

/**
 * Computes (1 << size_log2) + MAX(y_offset). The dither matrix attached to
 * the SwsUOp is always pre-padded to this number of lines.
 */
int ff_sws_dither_height(const SwsDitherUOp *dither);

typedef union SwsUOpParams {
    SwsShuffleUOp   shuffle; /* for SWS_UOP_RW_SHUFFLE */
    SwsFilterUOp    filter;  /* for SWS_UOP_READ_*_FV/FH */
    SwsShiftUOp     shift;
    SwsMoveUOp      move; /* for SWS_UOP_PERMUTE and SWS_UOP_COPY */
    SwsPackUOp      pack;
    SwsClearUOp     clear;
    SwsLinearUOp    lin;
    SwsDitherUOp    dither;
    SwsLut3DUOp     lut3d;
} SwsUOpParams;

typedef struct SwsUOp {
    /* These fields uniquely identify the uop implementation */
    SwsPixelType type;
    SwsUOpType uop;
    SwsCompMask mask;
    SwsUOpParams par;

    /* Constant data for this uop; not part of the unique identifier */
    union {
        SwsFilterWeights *kernel;   /* refstruct */
        SwsPixel *ptr;              /* refstruct */
        SwsPixel scalar;
        SwsPixel vec4[4];
        SwsPixel mat4[4][5];        /* row major */
        SwsShuffleMask shuffle;     /* for SWS_UOP_RW_SHUFFLE */
        const SwsLut3D *lut3d;      /* for SWS_UOP_LUT_3D; refstruct */
        void *opaque;               /* reserved for internal use */
    } data;
} SwsUOp;

/**
 * Compare two SwsUOps for equality (excluding constant data).
 */
int ff_sws_uop_cmp(const SwsUOp *a, const SwsUOp *b);

static inline int ff_sws_uop_cmp_v(const void *a, const void *b)
{
    return ff_sws_uop_cmp(a, b);
}

/**
 * Generate a unique name for a SwsUOp.
 */
#define SWS_UOP_NAME_MAX 64
void ff_sws_uop_name(const SwsUOp *op, char buf[SWS_UOP_NAME_MAX]);

typedef struct SwsUOpList {
    SwsUOp *ops;
    int num_ops;

    /* Additional metadata for implementations */
    SwsCompMask planes_in;  /* mask of planes read from */
    SwsCompMask planes_out; /* mask of planes written to */
    int pixel_size_max;     /* size of largest pixel type seen in any uop */
} SwsUOpList;

SwsUOpList *ff_sws_uop_list_alloc(void);
void ff_sws_uop_list_free(SwsUOpList **ops);
void ff_sws_uop_list_remove_at(SwsUOpList *uops, int index, int count);

/* Takes over ownership of `uop` and sets it to {0}, even on failure. */
int ff_sws_uop_list_append(SwsUOpList *uops, SwsUOp *uop);

/**
 * Called internally by ff_sws_ops_translate().
 */
int ff_sws_uop_list_optimize(SwsContext *ctx, SwsUOpFlags flags, SwsUOpList *uops);

/**
 * Translate a list of operations down to micro-ops, which can be further
 * optimized and then directly executed by backends.
 *
 * Return 0 or a negative error code.
 */
int ff_sws_ops_translate(SwsContext *ctx, const SwsOpList *ops,
                         SwsUOpFlags flags, SwsUOpList *uops);

/**
 * Compute a shuffle mask for `pshufb`-style ASM functions, by repeating
 * the shuffle pattern for as many groups as will fit.
 *
 * @param uop         An operation of type SWS_UOP_RW_SHUFFLE.
 * @param shuffle     The output shuffle index mask (or -1 to clear bytes).
 * @param size        The maximum size (in bytes) of the output shuffle mask.
 *
 * @return the number of groups on success, or a negative error code.
 *
 * @note The shuffle mask is already pre-expanded to fill up to 16 bytes,
 *       so this is only needed for larger shuffle instructions (e.g. vpermb).
 */
int ff_sws_shuffle_mask(const SwsUOp *uop, int8_t shuffle[], int size);

#endif
