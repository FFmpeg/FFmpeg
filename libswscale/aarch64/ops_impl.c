/*
 * Copyright (C) 2026 Ramiro Polla
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
 * This file is used both by sws_ops_aarch64 to generate ops_entries.c and
 * by the standalone build-time tool that generates the static assembly
 * functions (aarch64/ops_asmgen). Therefore, it must not depend on internal
 * FFmpeg libraries.
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#include "libavutil/attributes.h"

/**
 * NOTE: ops_asmgen contains header redefinitions to provide av_assert0
 * while not depending on internal FFmpeg libraries.
 */
#include "libavutil/avassert.h"

#include "ops_impl.h"

/*********************************************************************/
static const char pixel_types[AARCH64_PIXEL_TYPE_NB][32] = {
    [AARCH64_PIXEL_U8 ] = "AARCH64_PIXEL_U8",
    [AARCH64_PIXEL_U16] = "AARCH64_PIXEL_U16",
    [AARCH64_PIXEL_U32] = "AARCH64_PIXEL_U32",
    [AARCH64_PIXEL_F32] = "AARCH64_PIXEL_F32",
};

static const char *aarch64_pixel_type(SwsAArch64PixelType fmt)
{
    if (fmt >= AARCH64_PIXEL_TYPE_NB) {
        av_assert0(!"Invalid pixel type!");
        return NULL;
    }
    return pixel_types[fmt];
}

static const char pixel_type_names[AARCH64_PIXEL_TYPE_NB][4] = {
    [AARCH64_PIXEL_U8 ] = "u8",
    [AARCH64_PIXEL_U16] = "u16",
    [AARCH64_PIXEL_U32] = "u32",
    [AARCH64_PIXEL_F32] = "f32",
};

static const char *aarch64_pixel_type_name(SwsAArch64PixelType fmt)
{
    if (fmt >= AARCH64_PIXEL_TYPE_NB) {
        av_assert0(!"Invalid pixel type!");
        return NULL;
    }
    return pixel_type_names[fmt];
}

/*********************************************************************/
static const char op_types[AARCH64_SWS_OP_TYPE_NB][32] = {
    [AARCH64_SWS_OP_NONE          ] = "AARCH64_SWS_OP_NONE",
    [AARCH64_SWS_OP_PROCESS       ] = "AARCH64_SWS_OP_PROCESS",
    [AARCH64_SWS_OP_PROCESS_RETURN] = "AARCH64_SWS_OP_PROCESS_RETURN",
    [AARCH64_SWS_OP_READ_BIT      ] = "AARCH64_SWS_OP_READ_BIT",
    [AARCH64_SWS_OP_READ_NIBBLE   ] = "AARCH64_SWS_OP_READ_NIBBLE",
    [AARCH64_SWS_OP_READ_PACKED   ] = "AARCH64_SWS_OP_READ_PACKED",
    [AARCH64_SWS_OP_READ_PLANAR   ] = "AARCH64_SWS_OP_READ_PLANAR",
    [AARCH64_SWS_OP_WRITE_BIT     ] = "AARCH64_SWS_OP_WRITE_BIT",
    [AARCH64_SWS_OP_WRITE_NIBBLE  ] = "AARCH64_SWS_OP_WRITE_NIBBLE",
    [AARCH64_SWS_OP_WRITE_PACKED  ] = "AARCH64_SWS_OP_WRITE_PACKED",
    [AARCH64_SWS_OP_WRITE_PLANAR  ] = "AARCH64_SWS_OP_WRITE_PLANAR",
    [AARCH64_SWS_OP_SWAP_BYTES    ] = "AARCH64_SWS_OP_SWAP_BYTES",
    [AARCH64_SWS_OP_SWIZZLE       ] = "AARCH64_SWS_OP_SWIZZLE",
    [AARCH64_SWS_OP_UNPACK        ] = "AARCH64_SWS_OP_UNPACK",
    [AARCH64_SWS_OP_PACK          ] = "AARCH64_SWS_OP_PACK",
    [AARCH64_SWS_OP_LSHIFT        ] = "AARCH64_SWS_OP_LSHIFT",
    [AARCH64_SWS_OP_RSHIFT        ] = "AARCH64_SWS_OP_RSHIFT",
    [AARCH64_SWS_OP_CLEAR         ] = "AARCH64_SWS_OP_CLEAR",
    [AARCH64_SWS_OP_CONVERT       ] = "AARCH64_SWS_OP_CONVERT",
    [AARCH64_SWS_OP_EXPAND        ] = "AARCH64_SWS_OP_EXPAND",
    [AARCH64_SWS_OP_MIN           ] = "AARCH64_SWS_OP_MIN",
    [AARCH64_SWS_OP_MAX           ] = "AARCH64_SWS_OP_MAX",
    [AARCH64_SWS_OP_SCALE         ] = "AARCH64_SWS_OP_SCALE",
    [AARCH64_SWS_OP_LINEAR        ] = "AARCH64_SWS_OP_LINEAR",
    [AARCH64_SWS_OP_DITHER        ] = "AARCH64_SWS_OP_DITHER",
};

static const char *aarch64_op_type(SwsAArch64OpType op)
{
    if (op == AARCH64_SWS_OP_NONE || op >= AARCH64_SWS_OP_TYPE_NB) {
        av_assert0(!"Invalid op type!");
        return NULL;
    }
    return op_types[op];
}

static const char op_type_names[AARCH64_SWS_OP_TYPE_NB][16] = {
    [AARCH64_SWS_OP_NONE          ] = "none",
    [AARCH64_SWS_OP_PROCESS       ] = "process",
    [AARCH64_SWS_OP_PROCESS_RETURN] = "process_return",
    [AARCH64_SWS_OP_READ_BIT      ] = "read_bit",
    [AARCH64_SWS_OP_READ_NIBBLE   ] = "read_nibble",
    [AARCH64_SWS_OP_READ_PACKED   ] = "read_packed",
    [AARCH64_SWS_OP_READ_PLANAR   ] = "read_planar",
    [AARCH64_SWS_OP_WRITE_BIT     ] = "write_bit",
    [AARCH64_SWS_OP_WRITE_NIBBLE  ] = "write_nibble",
    [AARCH64_SWS_OP_WRITE_PACKED  ] = "write_packed",
    [AARCH64_SWS_OP_WRITE_PLANAR  ] = "write_planar",
    [AARCH64_SWS_OP_SWAP_BYTES    ] = "swap_bytes",
    [AARCH64_SWS_OP_SWIZZLE       ] = "swizzle",
    [AARCH64_SWS_OP_UNPACK        ] = "unpack",
    [AARCH64_SWS_OP_PACK          ] = "pack",
    [AARCH64_SWS_OP_LSHIFT        ] = "lshift",
    [AARCH64_SWS_OP_RSHIFT        ] = "rshift",
    [AARCH64_SWS_OP_CLEAR         ] = "clear",
    [AARCH64_SWS_OP_CONVERT       ] = "convert",
    [AARCH64_SWS_OP_EXPAND        ] = "expand",
    [AARCH64_SWS_OP_MIN           ] = "min",
    [AARCH64_SWS_OP_MAX           ] = "max",
    [AARCH64_SWS_OP_SCALE         ] = "scale",
    [AARCH64_SWS_OP_LINEAR        ] = "linear",
    [AARCH64_SWS_OP_DITHER        ] = "dither",
};

static const char *aarch64_op_type_name(SwsAArch64OpType op)
{
    if (op == AARCH64_SWS_OP_NONE || op >= AARCH64_SWS_OP_TYPE_NB) {
        av_assert0(!"Invalid op type!");
        return NULL;
    }
    return op_type_names[op];
}

/*********************************************************************/
/*
 * Helper string concatenation function that does not depend on the
 * FFmpeg libraries, so it may be used standalone.
 */
av_printf_format(3, 4)
static void buf_appendf(char **pbuf, size_t *prem, const char *fmt, ...)
{
    char *buf = *pbuf;
    size_t rem = *prem;
    if (!rem)
        return;

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, rem, fmt, ap);
    va_end(ap);

    if (n > 0) {
        if (n < rem) {
            buf += n;
            rem -= n;
        } else {
            buf += rem - 1;
            rem = 0;
        }
        *pbuf = buf;
        *prem = rem;
    }
}

/*********************************************************************/
/**
 * The following structure is used to describe one field from
 * SwsAArch64OpImplParams. This will be used to serialize the parameter
 * structure, generate function names and lookup strings, and compare
 * two sets of parameters.
 */

typedef struct ParamField {
    const char *name;
    size_t offset;
    size_t size;
    void (*print_str)(char **pbuf, size_t *prem, void *p);
    void (*print_val)(char **pbuf, size_t *prem, void *p);
    int (*cmp_val)(void *pa, void *pb);
} ParamField;

#define PARAM_FIELD(name) #name, offsetof(SwsAArch64OpImplParams, name), sizeof(((SwsAArch64OpImplParams *) 0)->name)

static void print_op_name(char **pbuf, size_t *prem, void *p)
{
    SwsAArch64OpType op = *(SwsAArch64OpType *) p;
    buf_appendf(pbuf, prem, "_%s", aarch64_op_type_name(op));
}

static void print_op_val(char **pbuf, size_t *prem, void *p)
{
    SwsAArch64OpType op = *(SwsAArch64OpType *) p;
    buf_appendf(pbuf, prem, "%s", aarch64_op_type(op));
}

static int cmp_op(void *pa, void *pb)
{
    int64_t ia = (int64_t) *((SwsAArch64OpType *) pa);
    int64_t ib = (int64_t) *((SwsAArch64OpType *) pb);
    int64_t diff = ia - ib;
    if (diff)
        return diff < 0 ? -1 : 1;
    return 0;
}

static void print_pixel_name(char **pbuf, size_t *prem, void *p)
{
    SwsAArch64PixelType type = *(SwsAArch64PixelType *) p;
    buf_appendf(pbuf, prem, "_%s", aarch64_pixel_type_name(type));
}

static void print_pixel_val(char **pbuf, size_t *prem, void *p)
{
    SwsAArch64PixelType type = *(SwsAArch64PixelType *) p;
    buf_appendf(pbuf, prem, "%s", aarch64_pixel_type(type));
}

static int cmp_pixel(void *pa, void *pb)
{
    int64_t ia = (int64_t) *((SwsAArch64PixelType *) pa);
    int64_t ib = (int64_t) *((SwsAArch64PixelType *) pb);
    int64_t diff = ia - ib;
    if (diff)
        return diff < 0 ? -1 : 1;
    return 0;
}

static void print_u8_name(char **pbuf, size_t *prem, void *p)
{
    uint8_t val = *(uint8_t *) p;
    buf_appendf(pbuf, prem, "_%u", val);
}

static void print_u8_val(char **pbuf, size_t *prem, void *p)
{
    uint8_t val = *(uint8_t *) p;
    buf_appendf(pbuf, prem, "%u", val);
}

static int cmp_u8(void *pa, void *pb)
{
    int64_t ia = (int64_t) *((uint8_t *) pa);
    int64_t ib = (int64_t) *((uint8_t *) pb);
    int64_t diff = ia - ib;
    if (diff)
        return diff < 0 ? -1 : 1;
    return 0;
}

static void print_u16_name(char **pbuf, size_t *prem, void *p)
{
    uint16_t val = *(uint16_t *) p;
    buf_appendf(pbuf, prem, "_%04x", val);
}

static void print_u16_val(char **pbuf, size_t *prem, void *p)
{
    uint16_t val = *(uint16_t *) p;
    buf_appendf(pbuf, prem, "0x%04x", val);
}

static int cmp_u16(void *pa, void *pb)
{
    int64_t ia = (int64_t) *((uint16_t *) pa);
    int64_t ib = (int64_t) *((uint16_t *) pb);
    int64_t diff = ia - ib;
    if (diff)
        return diff < 0 ? -1 : 1;
    return 0;
}

static void print_u40_name(char **pbuf, size_t *prem, void *p)
{
    uint64_t val = *(uint64_t *) p;
    buf_appendf(pbuf, prem, "_%010" PRIx64, val);
}

static void print_u40_val(char **pbuf, size_t *prem, void *p)
{
    uint64_t val = *(uint64_t *) p;
    buf_appendf(pbuf, prem, "0x%010" PRIx64 "ULL", val);
}

static int cmp_u40(void *pa, void *pb)
{
    int64_t ia = (int64_t) *((uint64_t *) pa);
    int64_t ib = (int64_t) *((uint64_t *) pb);
    int64_t diff = ia - ib;
    if (diff)
        return diff < 0 ? -1 : 1;
    return 0;
}

/*********************************************************************/
static const ParamField field_op               = { PARAM_FIELD(op),               print_op_name,    print_op_val,    cmp_op };
static const ParamField field_mask             = { PARAM_FIELD(mask),             print_u16_name,   print_u16_val,   cmp_u16 };
static const ParamField field_type             = { PARAM_FIELD(type),             print_pixel_name, print_pixel_val, cmp_pixel };
static const ParamField field_block_size       = { PARAM_FIELD(block_size),       print_u8_name,    print_u8_val,    cmp_u8 };
static const ParamField field_shift            = { PARAM_FIELD(shift),            print_u8_name,    print_u8_val,    cmp_u8 };
static const ParamField field_swizzle          = { PARAM_FIELD(swizzle),          print_u16_name,   print_u16_val,   cmp_u16 };
static const ParamField field_pack             = { PARAM_FIELD(pack),             print_u16_name,   print_u16_val,   cmp_u16 };
static const ParamField field_to_type          = { PARAM_FIELD(to_type),          print_pixel_name, print_pixel_val, cmp_pixel };
static const ParamField field_linear_mask      = { PARAM_FIELD(linear.mask),      print_u40_name,   print_u40_val,   cmp_u40 };
static const ParamField field_linear_fmla      = { PARAM_FIELD(linear.fmla),      print_u8_name,    print_u8_val,    cmp_u8 };
static const ParamField field_dither_y_offset  = { PARAM_FIELD(dither.y_offset),  print_u16_name,   print_u16_val,   cmp_u16 };
static const ParamField field_dither_size_log2 = { PARAM_FIELD(dither.size_log2), print_u8_name,    print_u8_val,    cmp_u8 };

/* Fields needed to uniquely identify each SwsAArch64OpType. */
#define MAX_LEVELS 8
static const ParamField *op_fields[AARCH64_SWS_OP_TYPE_NB][MAX_LEVELS] = {
    [AARCH64_SWS_OP_PROCESS       ] = { &field_op,                                                                                  &field_mask },
    [AARCH64_SWS_OP_PROCESS_RETURN] = { &field_op,                                                                                  &field_mask },
    [AARCH64_SWS_OP_READ_BIT      ] = { &field_op,                                                  &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_READ_NIBBLE   ] = { &field_op,                                                  &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_READ_PACKED   ] = { &field_op,                                                  &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_READ_PLANAR   ] = { &field_op,                                                  &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_WRITE_BIT     ] = { &field_op,                                                  &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_WRITE_NIBBLE  ] = { &field_op,                                                  &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_WRITE_PACKED  ] = { &field_op,                                                  &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_WRITE_PLANAR  ] = { &field_op,                                                  &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_SWAP_BYTES    ] = { &field_op,                                                  &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_SWIZZLE       ] = { &field_op, &field_swizzle,                                  &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_UNPACK        ] = { &field_op, &field_pack,                                     &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_PACK          ] = { &field_op, &field_pack,                                     &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_LSHIFT        ] = { &field_op, &field_shift,                                    &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_RSHIFT        ] = { &field_op, &field_shift,                                    &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_CLEAR         ] = { &field_op,                                                  &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_CONVERT       ] = { &field_op, &field_to_type,                                  &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_EXPAND        ] = { &field_op, &field_to_type,                                  &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_MIN           ] = { &field_op,                                                  &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_MAX           ] = { &field_op,                                                  &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_SCALE         ] = { &field_op,                                                  &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_LINEAR        ] = { &field_op, &field_linear_mask,     &field_linear_fmla,      &field_block_size, &field_type, &field_mask },
    [AARCH64_SWS_OP_DITHER        ] = { &field_op, &field_dither_y_offset, &field_dither_size_log2, &field_block_size, &field_type, &field_mask },
};
