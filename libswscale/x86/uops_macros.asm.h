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

/**
 * WARNING: This file is preprocessed by the C compiler to generate the NASM
 * source file uops_macros.gen.asm. It must *NOT* include any headers that
 * may inadvertently declare any C syntax. The following header only declares
 * further macros, and is therefore safe.
 */
#include "../uops_macros.h"

/**
 * NASM expects one statement per source line, but the C preprocessor expands
 * multi-line macros into a single line. This NASM macro allows us to work
 * around that by expanding its arguments into multiple lines.
 */
%macro MULTILINE 0-*
 %rep %0
    %1
  %rotate 1
 %endrep
%endmacro

/* Used to suppress "trailing empty parameter" warnings */
%macro dummy 0
%endmacro

#define DECL_OP_MACRO(...) {DECL_OP MACRO __VA_ARGS__},
#define DEF_MACRO(UOP, TYPE)                            \
    %define DECL_##TYPE##_##UOP(MACRO)                  \
    MULTILINE SWS_FOR_##TYPE##_##UOP(DECL_OP_MACRO)     \
    dummy

#define DEF_ALL_MACROS(TYPE)                \
    MULTILINE                               \
    {DEF_MACRO(READ_BIT,            TYPE)}, \
    {DEF_MACRO(READ_NIBBLE,         TYPE)}, \
    {DEF_MACRO(READ_PACKED,         TYPE)}, \
    {DEF_MACRO(READ_PLANAR,         TYPE)}, \
    {DEF_MACRO(READ_PLANAR_FH,      TYPE)}, \
    {DEF_MACRO(READ_PLANAR_FV,      TYPE)}, \
    {DEF_MACRO(READ_PLANAR_FV_FMA,  TYPE)}, \
    {DEF_MACRO(WRITE_BIT,           TYPE)}, \
    {DEF_MACRO(WRITE_NIBBLE,        TYPE)}, \
    {DEF_MACRO(WRITE_PACKED,        TYPE)}, \
    {DEF_MACRO(WRITE_PLANAR,        TYPE)}, \
    {DEF_MACRO(MOVE,                TYPE)}, \
    {DEF_MACRO(SWAP_BYTES,          TYPE)}, \
    {DEF_MACRO(EXPAND_BIT,          TYPE)}, \
    {DEF_MACRO(EXPAND_PAIR,         TYPE)}, \
    {DEF_MACRO(EXPAND_QUAD,         TYPE)}, \
    {DEF_MACRO(TO_U8,               TYPE)}, \
    {DEF_MACRO(TO_U16,              TYPE)}, \
    {DEF_MACRO(TO_U32,              TYPE)}, \
    {DEF_MACRO(TO_F32,              TYPE)}, \
    {DEF_MACRO(SCALE,               TYPE)}, \
    {DEF_MACRO(LINEAR_FMA,          TYPE)}, \
    {DEF_MACRO(ADD,                 TYPE)}, \
    {DEF_MACRO(MIN,                 TYPE)}, \
    {DEF_MACRO(MAX,                 TYPE)}, \
    {DEF_MACRO(UNPACK,              TYPE)}, \
    {DEF_MACRO(PACK,                TYPE)}, \
    {DEF_MACRO(LSHIFT,              TYPE)}, \
    {DEF_MACRO(RSHIFT,              TYPE)}, \
    {DEF_MACRO(CLEAR,               TYPE)}, \
    {DEF_MACRO(DITHER,              TYPE)}

DEF_ALL_MACROS(U8)
DEF_ALL_MACROS(U16)
DEF_ALL_MACROS(U32)
DEF_ALL_MACROS(F32)
