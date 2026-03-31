/**
 * Copyright (C) 2026 Lynne
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

#ifndef SWSCALE_VULKAN_SPVASM_H
#define SWSCALE_VULKAN_SPVASM_H

#include <stdint.h>
#include <string.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/avassert.h"
#include <spirv/unified1/spirv.h>
#include <spirv/unified1/GLSL.std.450.h>

/* COUNT_ARGS: counts variadic macro arguments, including zero.
 * The sentinel 0 is prepended so the compound literal is never empty,
 * making this valid in strict C11. */
#define COUNT_ARGS_IMPL(...) (sizeof((int[]){__VA_ARGS__}) / sizeof(int))
#define COUNT_ARGS(...) (COUNT_ARGS_IMPL(0, __VA_ARGS__) - 1)

/* SPI_ARGS: produces a (const int *, int) pair for the _arr helper functions.
 * When __VA_ARGS__ is empty the array literal is (int[]){0} and nb is 0,
 * so the pointer exists but is never dereferenced. */
#define SPI_ARGS(...) ((int[]){0, __VA_ARGS__}) + 1, COUNT_ARGS(__VA_ARGS__)

typedef struct SPICtx {
    uint8_t *dst;
    int dst_size;
    int off;
    int overwrite;

    int bool_type_id;
    int id;
} SPICtx;

static inline void spi_write_u32(SPICtx *spi, uint32_t v)
{
    if ((spi->off + 4) > spi->dst_size) {
        spi->overwrite += 4;
    } else {
        AV_WL32(&spi->dst[spi->off], v);
        spi->off += 4;
    }
}

static void spi_put_str(SPICtx *spi, const char *str)
{
    if ((spi->off + strlen(str) + 4) > spi->dst_size) {
        spi->overwrite += strlen(str) + 4;
        return;
    }

    for (int i = 0; i < strlen(str) + 1; i++)
        spi->dst[spi->off++] = str[i];
    int padding = (4 - (spi->off & 3)) & 3;
    for (int i = 0; i < padding; i++)
        spi->dst[spi->off++] = 0;
}

static inline void spi_init(SPICtx *spi, uint8_t *spv_buf, int buf_len)
{
    spi->id = 1;
    spi->off = 0;
    spi->overwrite = 0;
    spi->dst = spv_buf;
    spi->dst_size = buf_len;
    spi_write_u32(spi, SpvMagicNumber);
    spi_write_u32(spi, (1 << 16) | (6 << 8)); /* version */
    spi_write_u32(spi, 0); /* generator */
    spi_write_u32(spi, 0); /* last bound ID + 1, rewritten */
    spi_write_u32(spi, 0); /* schema */
}

static inline int spi_end(SPICtx *spi)
{
    if (spi->overwrite)
        return -spi->overwrite;
    AV_WL32(spi->dst + 3*4, spi->id);
    return spi->off;
}

static inline int spi_reserve(SPICtx *spi, int len)
{
    int off = spi->off;
    if ((off + len) > spi->dst_size) {
        spi->overwrite += len;
        return 0;
    }
    spi->off += len;
    return off;
}

static inline void spi_OpCapability(SPICtx *spi, SpvCapability capability)
{
    spi_write_u32(spi, (2 << 16) | 17);
    spi_write_u32(spi, capability);
}

static inline void spi_OpMemoryModel(SPICtx *spi, SpvAddressingModel addressing_model,
                                     SpvMemoryModel memory_model)
{
    spi_write_u32(spi, (3 << 16) | 14);
    spi_write_u32(spi, addressing_model);
    spi_write_u32(spi, memory_model);
}

static int spi_get_id(SPICtx *spi)
{
    return spi->id++;
}

static int spi_strl(const char *str)
{
    return FFALIGN(strlen(str) + 1, 4) >> 2;
}

/* Template: Single source */
static inline void spi_op_1src(SPICtx *spi, int code, int src1_id)
{
    spi_write_u32(spi, (2 << 16) | code);
    spi_write_u32(spi, src1_id);
}
#define OP_1SRC(name, code)                               \
static inline void spi_ ## name(SPICtx *spi, int src1_id) \
{                                                         \
    spi_op_1src(spi, code, src1_id);                      \
}

/* Template: Single source, untyped result */
static inline int spi_op_untypedres_1src(SPICtx *spi, int code, int id,
                                         int src1_id)
{
    spi_write_u32(spi, (3 << 16) | code);
    spi_write_u32(spi, id);
    spi_write_u32(spi, src1_id);
    return id;
}
#define OP_UNTYPEDRES1SRC(name, code)                                   \
static inline int spi_ ## name(SPICtx *spi, int src1_id)                \
{                                                                       \
    return spi_op_untypedres_1src(spi, code, spi_get_id(spi), src1_id); \
}

/* Template: Single source, typed result */
static inline int spi_op_res_1src(SPICtx *spi, int code, int id, int type_id,
                                  int src1_id)
{
    spi_write_u32(spi, (4 << 16) | code);
    spi_write_u32(spi, type_id);
    spi_write_u32(spi, id);
    spi_write_u32(spi, src1_id);
    return id;
}
#define OP_RES1SRC(name, code)                                            \
static inline int spi_ ## name(SPICtx *spi, int type_id, int src1_id)     \
{                                                                         \
    return spi_op_res_1src(spi, code, spi_get_id(spi), type_id, src1_id); \
}

/* Template: Two sources */
static inline void spi_op_2src(SPICtx *spi, int code, int src1_id, int src2_id)
{
    spi_write_u32(spi, (3 << 16) | code);
    spi_write_u32(spi, src1_id);
    spi_write_u32(spi, src2_id);
}
#define OP_2SRC(name, code)                                            \
static inline void spi_ ## name(SPICtx *spi, int src1_id, int src2_id) \
{                                                                      \
    spi_op_2src(spi, code, src1_id, src2_id);                          \
}

/* Template: Two sources, untyped result */
static inline int spi_op_untypedres_2src(SPICtx *spi, int code, int id,
                                         int src1_id, int src2_id)
{
    spi_write_u32(spi, (4 << 16) | code);
    spi_write_u32(spi, id);
    spi_write_u32(spi, src1_id);
    spi_write_u32(spi, src2_id);
    return id;
}
#define OP_UNTYPEDRES2SRC(name, code)                                 \
static inline int spi_ ## name(SPICtx *spi, int src1_id, int src2_id) \
{                                                                     \
    return spi_op_untypedres_2src(spi, code, spi_get_id(spi),         \
                           src1_id, src2_id);                         \
}

/* Template: Two sources, typed result */
static inline int spi_op_res_2src(SPICtx *spi, int code, int id, int type_id,
                                  int src1_id, int src2_id)
{
    spi_write_u32(spi, (5 << 16) | code);
    spi_write_u32(spi, type_id);
    spi_write_u32(spi, id);
    spi_write_u32(spi, src1_id);
    spi_write_u32(spi, src2_id);
    return id;
}
#define OP_RES2SRC(name, code)                                                     \
static inline int spi_ ## name(SPICtx *spi, int type_id, int src1_id, int src2_id) \
{                                                                                  \
    return spi_op_res_2src(spi, code, spi_get_id(spi), type_id,                    \
                           src1_id, src2_id);                                      \
}

/* Template: Single source, typed result, list */
static inline int spi_op_res_1src_list(SPICtx *spi, int code, int id, int type_id,
                                       int src1_id, const int *args, int nb_args)
{
    spi_write_u32(spi, ((4 + nb_args) << 16) | code);
    spi_write_u32(spi, type_id);
    spi_write_u32(spi, id);
    spi_write_u32(spi, src1_id);
    for (int i = 0; i < nb_args; i++)
        spi_write_u32(spi, args[i]);
    return id;
}

/* Template: No sources, untyped result, list */
static inline void spi_op_untypedres_list(SPICtx *spi, int code,
                                          int id, const int *args, int nb_args)
{
    spi_write_u32(spi, ((2 + nb_args) << 16) | code);
    spi_write_u32(spi, id);
    for (int i = 0; i < nb_args; i++)
        spi_write_u32(spi, args[i]);
}

/* Template: Two sources, list */
static inline void spi_op_2src_list(SPICtx *spi, int code,
                                    int src1_id, int src2_id,
                                    const int *args, int nb_args)
{
    spi_write_u32(spi, ((3 + nb_args) << 16) | code);
    spi_write_u32(spi, src1_id);
    spi_write_u32(spi, src2_id);
    for (int i = 0; i < nb_args; i++)
        spi_write_u32(spi, args[i]);
}

/* Template: Two sources, typed result, list */
static inline int spi_op_res_2src_list(SPICtx *spi, int code, int id, int type_id,
                                       int src1_id, int src2_id,
                                       const int *args, int nb_args)
{
    spi_write_u32(spi, ((5 + nb_args) << 16) | code);
    spi_write_u32(spi, type_id);
    spi_write_u32(spi, id);
    spi_write_u32(spi, src1_id);
    spi_write_u32(spi, src2_id);
    for (int i = 0; i < nb_args; i++)
        spi_write_u32(spi, args[i]);
    return id;
}

/* Template: Three sources, list */
static inline void spi_op_3src_list(SPICtx *spi, int code, int src1_id,
                                    int src2_id, int src3_id,
                                    const int *args, int nb_args)
{
    spi_write_u32(spi, ((4 + nb_args) << 16) | code);
    spi_write_u32(spi, src1_id);
    spi_write_u32(spi, src2_id);
    spi_write_u32(spi, src3_id);
    for (int i = 0; i < nb_args; i++)
        spi_write_u32(spi, args[i]);
}

OP_RES1SRC(OpImageQuerySize, 104)

OP_RES1SRC(OpConvertFToU, 109)
OP_RES1SRC(OpConvertFToS, 110)
OP_RES1SRC(OpConvertSToF, 111)
OP_RES1SRC(OpConvertUToF, 112)
OP_RES1SRC(OpBitcast, 124)

OP_RES1SRC(OpAny, 154)

#define spi_OpConstantComposite(spi, res_type, src, ...)           \
    spi_op_res_1src_list(spi, 44, spi_get_id(spi), res_type, src,  \
                         SPI_ARGS(__VA_ARGS__))

#define spi_OpAccessChain(spi, res_type, ptr_id, ...)                \
    spi_op_res_1src_list(spi, 65, spi_get_id(spi), res_type, ptr_id, \
                         SPI_ARGS(__VA_ARGS__))

#define spi_OpCompositeConstruct(spi, res_type, src, ...)          \
    spi_op_res_1src_list(spi, 80, spi_get_id(spi), res_type, src,  \
                         SPI_ARGS(__VA_ARGS__))

#define spi_OpCompositeExtract(spi, res_type, src, ...)            \
    spi_op_res_1src_list(spi, 81, spi_get_id(spi), res_type, src,  \
                         SPI_ARGS(__VA_ARGS__))

OP_RES2SRC(OpIAdd, 128)
OP_RES2SRC(OpFAdd, 129)
OP_RES2SRC(OpIMul, 132)
OP_RES2SRC(OpFMul, 133)
OP_RES2SRC(OpMatrixTimesVector, 145)
OP_RES2SRC(OpDot, 148)

OP_RES2SRC(OpIEqual, 170)
OP_RES2SRC(OpINotEqual, 171)
OP_RES2SRC(OpUGreaterThan, 172)
OP_RES2SRC(OpSGreaterThan, 173)
OP_RES2SRC(OpUGreaterThanEqual, 174)
OP_RES2SRC(OpSGreaterThanEqual, 175)
OP_RES2SRC(OpULessThan, 176)
OP_RES2SRC(OpSLessThan, 177)
OP_RES2SRC(OpULessThanEqual, 178)
OP_RES2SRC(OpSLessThanEqual, 179)

OP_RES2SRC(OpShiftRightLogical, 194)
OP_RES2SRC(OpShiftLeftLogical, 196)

OP_RES2SRC(OpBitwiseAnd, 199)
OP_1SRC(OpReturnValue, 254)

#define spi_OpExtInst(spi, res_type, instr_id, set_id, ...)                    \
    spi_op_res_2src_list(spi, 12, spi_get_id(spi), res_type, instr_id, set_id, \
                         SPI_ARGS(__VA_ARGS__))

#define spi_OpTypeStruct(spi, id, ...)                               \
    spi_op_untypedres_list(spi, 30, id,                              \
                           SPI_ARGS(__VA_ARGS__))

#define spi_OpDecorate(spi, target, deco, ...)                 \
    spi_op_2src_list(spi, 71, target, deco,                    \
                     SPI_ARGS(__VA_ARGS__))

#define spi_OpMemberDecorate(spi, type, target, deco, ...)     \
    spi_op_3src_list(spi, 72, type, target, deco,              \
                     SPI_ARGS(__VA_ARGS__))

#define spi_OpVectorShuffle(spi, res_type, src1, src2, ...)              \
    spi_op_res_2src_list(spi, 79, spi_get_id(spi), res_type, src1, src2, \
                         SPI_ARGS(__VA_ARGS__))

#define spi_OpCompositeInsert(spi, res_type, src1, src2, ...)            \
    spi_op_res_2src_list(spi, 82, spi_get_id(spi), res_type, src1, src2, \
                         SPI_ARGS(__VA_ARGS__))

static inline int spi_OpEntryPoint(SPICtx *spi, SpvExecutionModel execution_model,
                                   const char *name, const int *args, int nb_args)
{
    spi_write_u32(spi, (((3 + nb_args) + spi_strl(name)) << 16) | 15);
    spi_write_u32(spi, execution_model);
    spi_write_u32(spi, spi->id);
    spi_put_str(spi, name);
    for (int i = 0; i < nb_args; i++)
        spi_write_u32(spi, args[i]);
    return spi_get_id(spi);
}

static inline void spi_OpName(SPICtx *spi, int target_id, const char *name)
{
    spi_write_u32(spi, ((2 + spi_strl(name)) << 16) | 5);
    spi_write_u32(spi, target_id);
    spi_put_str(spi, name);
}

static inline void spi_OpExtension(SPICtx *spi, const char *name)
{
    spi_write_u32(spi, ((1 + spi_strl(name)) << 16) | 10);
    spi_put_str(spi, name);
}

static inline int spi_OpExtInstImport(SPICtx *spi, const char *name)
{
    spi_write_u32(spi, ((2 + spi_strl(name)) << 16) | 11);
    spi_write_u32(spi, spi->id);
    spi_put_str(spi, name);
    return spi_get_id(spi);
}

static inline void spi_OpExecutionMode(SPICtx *spi, int entry_point_id,
                                       SpvExecutionMode mode, int *s, int nb_s)
{
    spi_write_u32(spi, ((3 + nb_s) << 16) | 16);
    spi_write_u32(spi, entry_point_id);
    spi_write_u32(spi, mode);
    for (int i = 0; i < nb_s; i++)
        spi_write_u32(spi, s[i]);
}

static inline int spi_OpUndef(SPICtx *spi, int type_id)
{
    spi_write_u32(spi, (3 << 16) | 1);
    spi_write_u32(spi, type_id);
    spi_write_u32(spi, spi->id);
    return spi_get_id(spi);
}

static inline int spi_OpTypeVoid(SPICtx *spi)
{
    spi_write_u32(spi, (2 << 16) | 19);
    spi_write_u32(spi, spi->id);
    return spi_get_id(spi);
}

static inline int spi_OpTypeBool(SPICtx *spi)
{
    spi_write_u32(spi, (2 << 16) | 20);
    spi_write_u32(spi, spi->id);
    spi->bool_type_id = spi->id;
    return spi_get_id(spi);
}

OP_UNTYPEDRES2SRC(OpTypeInt, 21)
OP_UNTYPEDRES1SRC(OpTypeFloat, 22)
OP_UNTYPEDRES2SRC(OpTypeVector, 23)
OP_UNTYPEDRES2SRC(OpTypeMatrix, 24)

static inline int spi_OpTypeFloatEnc(SPICtx *spi, int width,
                                     SpvFPEncoding floating_point_encoding)
{
    spi_write_u32(spi, (4 << 16) | 22);
    spi_write_u32(spi, spi->id);
    spi_write_u32(spi, width);
    spi_write_u32(spi, floating_point_encoding);
    return spi_get_id(spi);
}

static inline int spi_OpTypeImage(SPICtx *spi, int sampled_type_id, SpvDim dim,
                                  int depth, int arrayed, int ms, int sampled,
                                  SpvImageFormat image_format)
{
    spi_write_u32(spi, (9 << 16) | 25);
    spi_write_u32(spi, spi->id);
    spi_write_u32(spi, sampled_type_id);
    spi_write_u32(spi, dim - 1);
    spi_write_u32(spi, depth);
    spi_write_u32(spi, arrayed);
    spi_write_u32(spi, ms);
    spi_write_u32(spi, sampled);
    spi_write_u32(spi, image_format);
    /* TODO: if implementing kernel mode, write an access qualifier here */
    return spi_get_id(spi);
}

static inline int spi_OpTypeArray(SPICtx *spi, int element_type_id, int id,
                                  int length_id)
{
    spi_write_u32(spi, (4 << 16) | 28);
    spi_write_u32(spi, id);
    spi_write_u32(spi, element_type_id);
    spi_write_u32(spi, length_id);
    return id;
}

static inline int spi_OpTypeRuntimeArray(SPICtx *spi, int element_type_id)
{
    spi_write_u32(spi, (3 << 16) | 29);
    spi_write_u32(spi, spi->id);
    spi_write_u32(spi, element_type_id);
    return spi_get_id(spi);
}

static inline int spi_OpTypePointer(SPICtx *spi, SpvStorageClass storage_class,
                                    int type_id)
{
    spi_write_u32(spi, (4 << 16) | 32);
    spi_write_u32(spi, spi->id);
    spi_write_u32(spi, storage_class);
    spi_write_u32(spi, type_id);
    return spi_get_id(spi);
}

static inline int spi_OpTypeFunction(SPICtx *spi, int return_type_id,
                                     const int *args, int nb_args)
{
    spi_write_u32(spi, ((3 + nb_args) << 16) | 33);
    spi_write_u32(spi, spi->id);
    spi_write_u32(spi, return_type_id);
    for (int i = 0; i < nb_args; i++)
        spi_write_u32(spi, args[i]);
    return spi_get_id(spi);
}

static inline void spi_OpFunction(SPICtx *spi, int fn_id, int result_type_id,
                                  SpvFunctionControlMask function_control,
                                  int function_type_id)
{
    spi_write_u32(spi, (5 << 16) | 54);
    spi_write_u32(spi, result_type_id);
    spi_write_u32(spi, fn_id);
    spi_write_u32(spi, function_control);
    spi_write_u32(spi, function_type_id);
}

static inline int spi_OpLabel(SPICtx *spi, int label_id)
{
    spi_write_u32(spi, (2 << 16) | 248);
    spi_write_u32(spi, label_id);
    return label_id;
}

static inline void spi_OpReturn(SPICtx *spi)
{
    spi_write_u32(spi, (1 << 16) | 253);
}

static inline void spi_OpFunctionEnd(SPICtx *spi)
{
    spi_write_u32(spi, (1 << 16) | 56);
}

static inline int spi_OpVariable(SPICtx *spi, int var_id, int ptr_type_id,
                                 SpvStorageClass storage_class, int initializer_id)
{
    spi_write_u32(spi, ((4 + !!initializer_id) << 16) | 59);
    spi_write_u32(spi, ptr_type_id);
    spi_write_u32(spi, var_id);
    spi_write_u32(spi, storage_class);
    if (initializer_id)
        spi_write_u32(spi, initializer_id);
    return var_id;
}

static inline int spi_OpConstantTrue(SPICtx *spi)
{
    spi_write_u32(spi, (3 << 16) | 41);
    spi_write_u32(spi, spi->bool_type_id);
    spi_write_u32(spi, spi->id);
    return spi_get_id(spi);
}

static inline int spi_OpConstantFalse(SPICtx *spi)
{
    spi_write_u32(spi, (3 << 16) | 42);
    spi_write_u32(spi, spi->bool_type_id);
    spi_write_u32(spi, spi->id);
    return spi_get_id(spi);
}

static inline int spi_OpConstantUInt(SPICtx *spi, int type_id, uint32_t val)
{
    spi_write_u32(spi, (4 << 16) | 43);
    spi_write_u32(spi, type_id);
    spi_write_u32(spi, spi->id);
    spi_write_u32(spi, val);
    return spi_get_id(spi);
}

static inline int spi_OpConstantUInt64(SPICtx *spi, int type_id, uint64_t val)
{
    spi_write_u32(spi, (5 << 16) | 43);
    spi_write_u32(spi, type_id);
    spi_write_u32(spi, spi->id);
    spi_write_u32(spi, val & UINT32_MAX);
    spi_write_u32(spi, val >> 32);
    return spi_get_id(spi);
}

static inline int spi_OpConstantInt(SPICtx *spi, int type_id, int val)
{
    uint32_t vu = (union { int i; uint32_t u; }){val}.u;
    return spi_OpConstantUInt(spi, type_id, vu);
}

static inline int spi_OpConstantInt64(SPICtx *spi, int type_id, int64_t val)
{
    uint64_t vu = (union { int64_t i; uint64_t u; }){val}.u;
    return spi_OpConstantUInt64(spi, type_id, vu);
}

static inline int spi_OpConstantFloat(SPICtx *spi, int type_id, float val)
{
    uint32_t vu = (union { float f; uint32_t u; }){val}.u;
    return spi_OpConstantUInt(spi, type_id, vu);
}

static inline int spi_OpConstantDouble(SPICtx *spi, int type_id, double val)
{
    uint64_t vu = (union { double d; uint64_t u; }){val}.u;
    return spi_OpConstantUInt64(spi, type_id, vu);
}

static inline int spi_OpLoad(SPICtx *spi, int result_type_id, int ptr_id,
                             SpvMemoryAccessMask memory_access, int align)
{
    int is_aligned = !!(memory_access & SpvMemoryAccessAlignedMask);
    spi_write_u32(spi, ((5 + is_aligned) << 16) | 61);
    spi_write_u32(spi, result_type_id);
    spi_write_u32(spi, spi->id);
    spi_write_u32(spi, ptr_id);
    spi_write_u32(spi, memory_access);
    if (is_aligned)
        spi_write_u32(spi, align);
    return spi_get_id(spi);
}

static inline void spi_OpStore(SPICtx *spi, int ptr_id, int obj_id,
                               SpvMemoryAccessMask memory_access, int align)
{
    int is_aligned = !!(memory_access & SpvMemoryAccessAlignedMask);
    spi_write_u32(spi, ((4 + is_aligned) << 16) | 62);
    spi_write_u32(spi, ptr_id);
    spi_write_u32(spi, obj_id);
    spi_write_u32(spi, memory_access);
    if (is_aligned)
        spi_write_u32(spi, align);
}

static inline void spi_OpSelectionMerge(SPICtx *spi, int merge_block,
                                        SpvSelectionControlMask selection_control)
{
    spi_write_u32(spi, (3 << 16) | 247);
    spi_write_u32(spi, merge_block);
    spi_write_u32(spi, selection_control);
}

static inline void spi_OpBranchConditional(SPICtx *spi, int cond_id,
                                           int true_label, int false_label,
                                           uint32_t branch_weights)
{
    spi_write_u32(spi, ((4 + 2*(!!branch_weights)) << 16) | 250);
    spi_write_u32(spi, cond_id);
    spi_write_u32(spi, true_label);
    spi_write_u32(spi, false_label);
    if (branch_weights) {
        spi_write_u32(spi, branch_weights >> 16);
        spi_write_u32(spi, branch_weights & UINT16_MAX);
    }
}

static inline int spi_OpImageRead(SPICtx *spi, int result_type_id, int img_id,
                                  int pos_id, SpvImageOperandsMask image_operands)
{
    spi_write_u32(spi, (5 + (!!image_operands) << 16) | 98);
    spi_write_u32(spi, result_type_id);
    spi_write_u32(spi, spi->id);
    spi_write_u32(spi, img_id);
    spi_write_u32(spi, pos_id);
    if (image_operands)
        spi_write_u32(spi, image_operands);
    return spi_get_id(spi);
}

static inline void spi_OpImageWrite(SPICtx *spi, int img_id, int pos_id,
                                    int src_id, SpvImageOperandsMask image_operands)
{
    spi_write_u32(spi, (4 + (!!image_operands) << 16) | 99);
    spi_write_u32(spi, img_id);
    spi_write_u32(spi, pos_id);
    spi_write_u32(spi, src_id);
    if (image_operands)
        spi_write_u32(spi, image_operands);
}

#endif /* SWSCALE_VULKAN_SPVASM_H */
