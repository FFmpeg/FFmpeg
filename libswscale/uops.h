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
int ff_sws_pixel_type_size(SwsPixelType type) av_const;
bool ff_sws_pixel_type_is_int(SwsPixelType type) av_const;

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
    SWS_UOP_FLAG_NONE = 0,
    SWS_UOP_FLAG_FMA  = (1 << 0), /* platform supports FMA ops */
    SWS_UOP_FLAG_MOVE = (1 << 1), /* platform supports SWS_UOP_MOVE */
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

    /* Data rearrangement uops; mask = non-trivial and needed components */
    SWS_UOP_PERMUTE,         /* rearrange components (no duplicates) */
    SWS_UOP_COPY,            /* copy/duplicate components */
    SWS_UOP_MOVE,            /* series of register-register assignments */

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

    /* Platform-specific uops would go here */
    SWS_UOP_TYPE_NB,
} SwsUOpType;

typedef struct SwsFilterUOp {
    SwsPixelType type; /* pixel type to store result as */
} SwsFilterUOp;

typedef struct SwsShiftUOp {
    uint8_t amount;
} SwsShiftUOp;

typedef struct SwsSwizzleUOp {
    uint8_t in[4]; /* input component for each output component */
} SwsSwizzleUOp;

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

typedef struct SwsDitherUOp {
    uint8_t y_offset[4];
    uint8_t size_log2;
} SwsDitherUOp;

/**
 * Computes (1 << size_log2) + MAX(y_offset). The dither matrix attached to
 * the SwsUOp is always pre-padded to this number of lines.
 */
int ff_sws_dither_height(const SwsDitherUOp *dither);

typedef union SwsUOpParams {
    SwsFilterUOp    filter; /* for SWS_UOP_READ_*_FV/FH */
    SwsShiftUOp     shift;
    SwsSwizzleUOp   swizzle;
    SwsMoveUOp      move;
    SwsPackUOp      pack;
    SwsClearUOp     clear;
    SwsLinearUOp    lin;
    SwsDitherUOp    dither;
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
} SwsUOpList;

SwsUOpList *ff_sws_uop_list_alloc(void);
void ff_sws_uop_list_free(SwsUOpList **ops);

/* Takes over ownership of `uop` and sets it to {0}, even on failure. */
int ff_sws_uop_list_append(SwsUOpList *uops, SwsUOp *uop);

/**
 * Translate a list of operations down to micro-ops, which can be further
 * optimized and then directly executed by backends.
 *
 * Return 0 or a negative error code.
 */
int ff_sws_ops_translate(SwsContext *ctx, const SwsOpList *ops,
                         SwsUOpFlags flags, SwsUOpList *uops);

/**
 * Generate a set of boilerplate C preprocessor macros for describing and
 * programmatically iterating over all possible SwsUOps.
 *
 * This function can be quite slow as it iterates over every possible
 * combination of pixel formats and flags.
 *
 * Returns 0 or a negative error code. On success, an allocated string is
 * returned via `out_str`, and must be av_free()'d by the caller.
 */
int ff_sws_uops_macros_gen(char **out_str);

#endif
