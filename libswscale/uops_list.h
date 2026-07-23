/**
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

#ifndef SWSCALE_UOPS_LIST_H
#define SWSCALE_UOPS_LIST_H

// ENTRY(OP, ABBR)
#define UOPS_LIST(ENTRY)                                             \
    ENTRY(SWS_UOP_INVALID,              "invalid")                   \
    ENTRY(SWS_UOP_READ_PLANAR,          "read_planar")               \
    ENTRY(SWS_UOP_READ_PLANAR_FH,       "read_planar_fh")            \
    ENTRY(SWS_UOP_READ_PLANAR_FV,       "read_planar_fv")            \
    ENTRY(SWS_UOP_READ_PLANAR_FV_FMA,   "read_planar_fv_fma")        \
    ENTRY(SWS_UOP_READ_PACKED,          "read_packed")               \
    ENTRY(SWS_UOP_READ_NIBBLE,          "read_nibble")               \
    ENTRY(SWS_UOP_READ_BIT,             "read_bit")                  \
    ENTRY(SWS_UOP_READ_PALETTE,         "read_palette")              \
    ENTRY(SWS_UOP_WRITE_PLANAR,         "write_planar")              \
    ENTRY(SWS_UOP_WRITE_PACKED,         "write_packed")              \
    ENTRY(SWS_UOP_WRITE_NIBBLE,         "write_nibble")              \
    ENTRY(SWS_UOP_WRITE_BIT,            "write_bit")                 \
    ENTRY(SWS_UOP_RW_SHUFFLE,           "rw_shuffle")                \
    ENTRY(SWS_UOP_PERMUTE,              "permute")                   \
    ENTRY(SWS_UOP_COPY,                 "copy")                      \
    ENTRY(SWS_UOP_SWAP_BYTES,           "swap_bytes")                \
    ENTRY(SWS_UOP_EXPAND_BIT,           "expand_bit")                \
    ENTRY(SWS_UOP_EXPAND_PAIR,          "expand_pair")               \
    ENTRY(SWS_UOP_EXPAND_QUAD,          "expand_quad")               \
    ENTRY(SWS_UOP_TO_U8,                "to_u8")                     \
    ENTRY(SWS_UOP_TO_U16,               "to_u16")                    \
    ENTRY(SWS_UOP_TO_U32,               "to_u32")                    \
    ENTRY(SWS_UOP_TO_F32,               "to_f32")                    \
    ENTRY(SWS_UOP_SCALE,                "scale")                     \
    ENTRY(SWS_UOP_LINEAR,               "linear")                    \
    ENTRY(SWS_UOP_LINEAR_FMA,           "linear_fma")                \
    ENTRY(SWS_UOP_ADD,                  "add")                       \
    ENTRY(SWS_UOP_MIN,                  "min")                       \
    ENTRY(SWS_UOP_MAX,                  "max")                       \
    ENTRY(SWS_UOP_UNPACK,               "unpack")                    \
    ENTRY(SWS_UOP_PACK,                 "pack")                      \
    ENTRY(SWS_UOP_LSHIFT,               "lshift")                    \
    ENTRY(SWS_UOP_RSHIFT,               "rshift")                    \
    ENTRY(SWS_UOP_CLEAR,                "clear")                     \
    ENTRY(SWS_UOP_DITHER,               "dither")                    \
    ENTRY(SWS_UOP_LUT_3D,               "lut_3d")                    \

#endif /* SWSCALE_UOPS_LIST_H */
