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

#ifndef SWSCALE_OPS_H
#define SWSCALE_OPS_H

#include <assert.h>
#include <stdbool.h>
#include <stdalign.h>

#include "libavutil/bprint.h"

#include "graph.h"
#include "filters.h"

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

typedef enum SwsOpType {
    SWS_OP_INVALID = 0,

    /* Defined for all types; but implemented for integers only */
    SWS_OP_READ,            /* gather raw pixels from planes */
    SWS_OP_WRITE,           /* write raw pixels to planes */
    SWS_OP_SWAP_BYTES,      /* swap byte order (for differing endianness) */
    SWS_OP_SWIZZLE,         /* rearrange channel order, or duplicate channels */

    /* Bit manipulation operations. Defined for integers only. */
    SWS_OP_UNPACK,          /* split tightly packed data into components */
    SWS_OP_PACK,            /* compress components into tightly packed data */
    SWS_OP_LSHIFT,          /* logical left shift of raw pixel values */
    SWS_OP_RSHIFT,          /* right shift of raw pixel values */

    /* Generic arithmetic. Defined and implemented for all types */
    SWS_OP_CLEAR,           /* clear pixel values */
    SWS_OP_CONVERT,         /* convert (cast) between formats */
    SWS_OP_MIN,             /* numeric minimum */
    SWS_OP_MAX,             /* numeric maximum */
    SWS_OP_SCALE,           /* multiplication by scalar */

    /* Floating-point only arithmetic operations. */
    SWS_OP_LINEAR,          /* generalized linear affine transform */
    SWS_OP_DITHER,          /* add dithering noise */

    /* Filtering operations. Always output floating point. */
    SWS_OP_FILTER_H,        /* horizontal filtering */
    SWS_OP_FILTER_V,        /* vertical filtering */

    SWS_OP_TYPE_NB,
} SwsOpType;

const char *ff_sws_op_type_name(SwsOpType op);

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

/* Compute SwsCompMask from values with denominator != 0 */
SwsCompMask ff_sws_comp_mask_q4(const AVRational q[4]);

typedef enum SwsCompFlags {
    SWS_COMP_GARBAGE = 1 << 0, /* contents are undefined / garbage data */
    SWS_COMP_EXACT   = 1 << 1, /* value is an exact integer */
    SWS_COMP_ZERO    = 1 << 2, /* known to be a constant zero */
    SWS_COMP_SWAPPED = 1 << 3, /* byte order is swapped */
} SwsCompFlags;

typedef struct SwsComps {
    SwsCompFlags flags[4]; /* knowledge about (output) component contents */
    bool unused[4]; /* which input components are definitely unused */

    /* Keeps track of the known possible value range, or {0, 0} for undefined
     * or (unknown range) floating point inputs */
    AVRational min[4], max[4];
} SwsComps;

typedef struct SwsReadWriteOp {
    /**
     * Examples:
     *   rgba      = 4x u8 packed
     *   yuv444p   = 3x u8
     *   rgb565    = 1x u16   <- use SWS_OP_UNPACK to unpack
     *   monow     = 1x u8 (frac 3)
     *   rgb4      = 1x u8 (frac 1)
     */
    uint8_t elems; /* number of elements (of type `op.type`) to read/write */
    uint8_t frac;  /* fractional pixel step factor (log2) */
    bool packed;   /* read multiple elements from a single plane */

    /**
     * Filter kernel to apply to each plane while sampling. Currently, only
     * one shared filter kernel is supported for all planes. (Optional)
     *
     * Note: As with SWS_OP_FILTER_*, if a filter kernel is in use, the read
     * operation will always output floating point values.
     */
    SwsOpType filter;         /* some value of SWS_OP_FILTER_* */
    SwsFilterWeights *kernel; /* (refstruct) */
} SwsReadWriteOp;

typedef struct SwsPackOp {
    /**
     * Packed bits are assumed to be LSB-aligned within the underlying
     * integer type; i.e. (msb) 0 ... X Y Z W (lsb).
     */
    uint8_t pattern[4]; /* bit depth pattern, from MSB to LSB */
} SwsPackOp;

typedef struct SwsSwizzleOp {
    /**
     * Input component for each output component:
     *   Out[x] := In[swizzle.in[x]]
     */
    union {
        uint32_t mask;
        uint8_t in[4];
        struct { uint8_t x, y, z, w; };
    };
} SwsSwizzleOp;

#define SWS_SWIZZLE(X,Y,Z,W) ((SwsSwizzleOp) { .in = {X, Y, Z, W} })
SwsCompMask ff_sws_comp_mask_swizzle(SwsCompMask mask, SwsSwizzleOp swiz);

typedef struct SwsShiftOp {
    uint8_t amount; /* number of bits to shift */
} SwsShiftOp;

typedef struct SwsClearOp {
    SwsCompMask mask;    /* mask of components to clear */
    AVRational value[4]; /* value to set */
} SwsClearOp;

typedef struct SwsConvertOp {
    SwsPixelType to; /* type of pixel to convert to */
    bool expand; /* if true, integers are expanded to the full range */
} SwsConvertOp;

typedef struct SwsClampOp {
    AVRational limit[4]; /* per-component min/max value */
} SwsClampOp;

typedef struct SwsScaleOp {
    AVRational factor; /* scalar multiplication factor */
} SwsScaleOp;

typedef struct SwsDitherOp {
    AVRational *matrix; /* tightly packed dither matrix (refstruct) */
    AVRational min, max; /* minimum/maximum value in `matrix` */
    int size_log2; /* size (in bits) of the dither matrix */
    int8_t y_offset[4]; /* row offset for each component, or -1 for ignored */
} SwsDitherOp;

typedef struct SwsLinearOp {
    /**
     * Generalized 5x5 affine transformation:
     *   [ Out.x ] = [ A B C D E ]
     *   [ Out.y ] = [ F G H I J ] * [ x y z w 1 ]
     *   [ Out.z ] = [ K L M N O ]
     *   [ Out.w ] = [ P Q R S T ]
     *
     * The mask keeps track of which components differ from an identity matrix.
     * There may be more efficient implementations of particular subsets, for
     * example the common subset of {A, E, G, J, M, O} can be implemented with
     * just three fused multiply-add operations.
     */
    AVRational m[4][5];
    uint32_t mask; /* m[i][j] <-> 1 << (5 * i + j) */
} SwsLinearOp;

#define SWS_MASK(I, J)  (1 << (5 * (I) + (J)))
#define SWS_MASK_OFF(I) SWS_MASK(I, 4)
#define SWS_MASK_ROW(I) (0x1F << (5 * (I)))
#define SWS_MASK_COL(J) (0x8421 << J)

enum {
    SWS_MASK_ALL   = (1 << 20) - 1,
    SWS_MASK_LUMA  = SWS_MASK(0, 0) | SWS_MASK_OFF(0),
    SWS_MASK_ALPHA = SWS_MASK(3, 3),

    SWS_MASK_DIAG3 = SWS_MASK(0, 0)  | SWS_MASK(1, 1)  | SWS_MASK(2, 2),
    SWS_MASK_OFF3  = SWS_MASK_OFF(0) | SWS_MASK_OFF(1) | SWS_MASK_OFF(2),
    SWS_MASK_MAT3  = SWS_MASK(0, 0)  | SWS_MASK(0, 1)  | SWS_MASK(0, 2) |
                     SWS_MASK(1, 0)  | SWS_MASK(1, 1)  | SWS_MASK(1, 2) |
                     SWS_MASK(2, 0)  | SWS_MASK(2, 1)  | SWS_MASK(2, 2),

    SWS_MASK_DIAG4 = SWS_MASK_DIAG3  | SWS_MASK(3, 3),
    SWS_MASK_OFF4  = SWS_MASK_OFF3   | SWS_MASK_OFF(3),
    SWS_MASK_MAT4  = SWS_MASK_ALL & ~SWS_MASK_OFF4,
};

/* Helper function to compute the correct mask */
uint32_t ff_sws_linear_mask(SwsLinearOp);

typedef struct SwsFilterOp {
    SwsFilterWeights *kernel; /* filter kernel (refstruct) */
} SwsFilterOp;

typedef struct SwsOp {
    SwsOpType op;      /* operation to perform */
    SwsPixelType type; /* pixel type to operate on */
    union {
        SwsLinearOp     lin;
        SwsReadWriteOp  rw;
        SwsPackOp       pack;
        SwsSwizzleOp    swizzle;
        SwsShiftOp      shift;
        SwsClearOp      clear;
        SwsConvertOp    convert;
        SwsClampOp      clamp;
        SwsScaleOp      scale;
        SwsDitherOp     dither;
        SwsFilterOp     filter;
    };

    /**
     * Metadata about the operation's input/output components. Discarded
     * and regenerated automatically by `ff_sws_op_list_update_comps()`.
     *
     * Note that backends may rely on the presence and accuracy of this
     * metadata for all operations, during ff_sws_ops_compile().
     */
    SwsComps comps;
} SwsOp;

#define SWS_OP_NEEDED(op, idx) (!((op)->comps.flags[idx] & SWS_COMP_GARBAGE))

/* Compute SwsCompMask from a mask of needed components */
SwsCompMask ff_sws_comp_mask_needed(const SwsOp *op);

/**
 * Describe an operation in human-readable form.
 */
void ff_sws_op_desc(AVBPrint *bp, const SwsOp *op);

/**
 * Frees any allocations associated with an SwsOp and sets it to {0}.
 */
void ff_sws_op_uninit(SwsOp *op);

/**
 * Apply an operation to an AVRational. No-op for read/write operations.
 */
void ff_sws_apply_op_q(const SwsOp *op, AVRational x[4]);

/**
 * Helper struct for representing a list of operations.
 */
typedef struct SwsOpList {
    SwsOp *ops;
    int num_ops;

    /* Metadata associated with this operation list */
    SwsFormat src, dst;

    /* Input/output plane indices */
    uint8_t plane_src[4], plane_dst[4];

    /**
     * Source component metadata associated with pixel values from each
     * corresponding component (in plane/memory order, i.e. not affected by
     * `plane_src`). Lets the optimizer know additional information about
     * the value range and/or pixel data to expect.
     *
     * The default value of {0} is safe to pass in the case that no additional
     * information is known.
     */
    SwsComps comps_src;
} SwsOpList;

SwsOpList *ff_sws_op_list_alloc(void);
void ff_sws_op_list_free(SwsOpList **ops);

/**
 * Returns a duplicate of `ops`, or NULL on OOM.
 */
SwsOpList *ff_sws_op_list_duplicate(const SwsOpList *ops);

/**
 * Returns the input operation for a given op list, or NULL if there is none
 * (e.g. for a pure CLEAR-only operation list).
 *
 * This will always be an op of type SWS_OP_READ.
 */
const SwsOp *ff_sws_op_list_input(const SwsOpList *ops);

/**
 * Returns the output operation for a given op list, or NULL if there is none.
 *
 * This will always be an op of type SWS_OP_WRITE.
 */
const SwsOp *ff_sws_op_list_output(const SwsOpList *ops);

/**
 * Returns whether an op list represents a true no-op operation, i.e. may be
 * eliminated entirely from an execution graph.
 */
bool ff_sws_op_list_is_noop(const SwsOpList *ops);

/**
 * Returns the size of the largest pixel type used in `ops`.
 */
int ff_sws_op_list_max_size(const SwsOpList *ops);

/**
 * These will take over ownership of `op` and set it to {0}, even on failure.
 */
int ff_sws_op_list_append(SwsOpList *ops, SwsOp *op);
int ff_sws_op_list_insert_at(SwsOpList *ops, int index, SwsOp *op);

void ff_sws_op_list_remove_at(SwsOpList *ops, int index, int count);

/**
 * Print out the contents of an operation list.
 */
void ff_sws_op_list_print(void *log_ctx, int log_level, int log_level_extra,
                          const SwsOpList *ops);

/**
 * Infer + propagate known information about components. Called automatically
 * when needed by the optimizer and compiler.
 */
void ff_sws_op_list_update_comps(SwsOpList *ops);

/**
 * Fuse compatible and eliminate redundant operations, as well as replacing
 * some operations with more efficient alternatives.
 */
int ff_sws_op_list_optimize(SwsOpList *ops);

enum SwsOpCompileFlags {
    /* Automatically optimize the operations when compiling */
    SWS_OP_FLAG_OPTIMIZE = 1 << 0,
};

/**
 * Resolves an operation list to a graph pass. The first and last operations
 * must be a read/write respectively. `flags` is a list of SwsOpCompileFlags.
 *
 * Takes over ownership of `ops` and sets it to NULL, even on failure.
 *
 * Note: `ops` may be modified by this function.
 */
int ff_sws_compile_pass(SwsGraph *graph, SwsOpList **ops, int flags,
                        SwsPass *input, SwsPass **output);

/**
 * Helper function to enumerate over all possible (optimized) operation lists,
 * under the current set of options in `ctx`, and run the given callback on
 * each list.
 *
 * @param src_fmt If set (not AV_PIX_FMT_NONE), constrain the source format
 * @param dst_fmt If set (not AV_PIX_FMT_NONE), constrain the destination format
 * @return 0 on success, the return value if cb() < 0, or a negative error code
 *
 * @note `ops` belongs to ff_sws_enum_op_lists(), but may be mutated by `cb`.
 * @see ff_sws_enum_ops()
 */
int ff_sws_enum_op_lists(SwsContext *ctx, void *opaque,
                         enum AVPixelFormat src_fmt, enum AVPixelFormat dst_fmt,
                         int (*cb)(SwsContext *ctx, void *opaque, SwsOpList *ops));

/**
 * Helper function to enumerate over all possible operations, under the current
 * set of options in `ctx`, and run the given callback on each operation.
 *
 * @param src_fmt If set (not AV_PIX_FMT_NONE), constrain the source format
 * @param dst_fmt If set (not AV_PIX_FMT_NONE), constrain the destination format
 * @return 0 on success, the return value if cb() < 0, or a negative error code
 *
 * @note May contain duplicates. `op` belongs to ff_sws_enum_ops(), but may be
 *       mutated by `cb`.
 * @see ff_sws_num_op_lists()
 */
int ff_sws_enum_ops(SwsContext *ctx, void *opaque,
                    enum AVPixelFormat src_fmt, enum AVPixelFormat dst_fmt,
                    int (*cb)(SwsContext *ctx, void *opaque, SwsOp *op));

#endif
